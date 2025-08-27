#pragma once

// 无锁实现的mpsc队列，且不依赖于spsc

#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "defs.h"
#include "test_suit.h"

template <typename T> struct ThreadLocal {
  alignas(CacheLineSize) T value;
};

template <typename T, size_t Capacity> class MPSCQueue {
private:
  using size_t = long long;
  enum class SlotState : uint8_t {
    Empty,
    Writing,
    Ready,
  };

  struct Slot {
    alignas(CacheLineSize) std::atomic<SlotState> state{SlotState::Empty};
    alignas(CacheLineSize) T data;
  };

  struct ProducerMetadata {
    alignas(CacheLineSize) std::atomic<size_t> tail{0};
  };

  struct ConsumerMetadata {
    alignas(CacheLineSize) std::atomic<size_t> head{0};
  };

  alignas(CacheLineSize) std::array<Slot, Capacity> buffer_;
  alignas(CacheLineSize) ProducerMetadata producer_;
  alignas(CacheLineSize) ConsumerMetadata consumer_;

  // 线程本地状态
  //   static thread_local size_t tls_cached_tail_;
  static thread_local size_t tls_cached_head_;

public:
  MPSCQueue() { tls_cached_head_ = 0; }
  template <Writer<T> W> bool push_with_writer(W writer) noexcept {
    size_t slot_id = acquire_slot();
    if (slot_id == Capacity) [[unlikely]] {
      return false;
    }
    Slot &slot = buffer_[slot_id];
    writer(&slot.data);
    slot.state.store(SlotState::Ready, std::memory_order_release);
    return true;
  }
  template <typename U>
  bool push(U &&value) noexcept
    requires(!Writer<U, T>)
  {
    return push_with_writer([value = std::forward<U>(value)](T *buffer) {
      *buffer = std::move(value);
    });
  }

  template <typename U>
  bool push(U writer) noexcept
    requires(Writer<U, T>)
  {
    return push_with_writer(writer);
  }

  template <Reader<T> R> bool pop(R reader) noexcept {
    size_t current_head = consumer_.head.load(std::memory_order_relaxed);
    size_t current_tail = producer_.tail.load(std::memory_order_acquire);

    if (current_head >= current_tail) [[unlikely]] {
      return false; // 队列为空
    }

    size_t slot_id = current_head & mask();
    Slot &slot = buffer_[slot_id];

    if (slot.state.load(std::memory_order_acquire) != SlotState::Ready)
        [[unlikely]] {
      return false; // 槽位未准备好
    }

    reader(&slot.data);
    // 标记为读取就绪
    consumer_.head.store(current_head + 1, std::memory_order_release);
    slot.state.store(SlotState::Empty, std::memory_order_release);
    return true;
  }
  bool pop(T &value) noexcept {
    return pop([&value](const T *buffer) { value = *buffer; });
  }

  size_t size() const noexcept {
    return producer_.tail.load(std::memory_order_acquire) -
           consumer_.head.load(std::memory_order_acquire);
  }

  bool empty() const noexcept { return size() == 0; }
  size_t capacity() const noexcept { return Capacity + 1; }

private:
  static constexpr size_t mask() noexcept { return Capacity - 1; }
  // 获取一个可用槽位
  size_t acquire_slot() noexcept {
    size_t current_tail = producer_.tail.load(std::memory_order_relaxed);
    size_t current_head = tls_cached_head_;

    if (current_tail - current_head >= Capacity) [[unlikely]] {
      // 检查缓存
      current_head = consumer_.head.load(std::memory_order_acquire);
      tls_cached_head_ = current_head;
      if (current_tail - current_head >= Capacity) [[unlikely]] {
        return Capacity;
      }
    }

    size_t slot_index = current_tail & mask();
    Slot &slot = buffer_[slot_index];

    // 检查槽位可用性
    SlotState expected = SlotState::Empty;
    if (!slot.state.compare_exchange_strong(
            expected, SlotState::Writing, std::memory_order_acq_rel,
            std::memory_order_acquire)) [[unlikely]] {
      return Capacity; // 槽位不可用
    }

    // 更新tail
    size_t expected_tail = current_tail;
    if (producer_.tail.compare_exchange_strong(expected_tail, current_tail + 1,
                                               std::memory_order_release,
                                               std::memory_order_relaxed)) {
      return slot_index; // 成功
    } else [[unlikely]] {
      // 失败，回滚槽位状态
      slot.state.store(SlotState::Empty, std::memory_order_release);
      return Capacity; // 失败
    }
  }
};

template <typename T, size_t Capacity>
thread_local typename MPSCQueue<T, Capacity>::size_t
    MPSCQueue<T, Capacity>::tls_cached_head_ = 0;