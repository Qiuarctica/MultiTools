#pragma once

#include "../utils/defs.h"
#include <array>
#include <atomic>
#include <vector>

// SIZE无用，用于统一测试接口
template <typename T, size_t Capacity, size_t SIZE = 0> class MPSCQueue {
private:
  struct Slot {
    alignas(CacheLineSize) T data;
  };

  alignas(CacheLineSize) std::array<Slot, Capacity> buffer_;
  alignas(CacheLineSize) std::atomic<size_t> tail_{0}; // 生产者尾部
  alignas(CacheLineSize) std::atomic<size_t> head_{0}; // 消费者头部

  // 使用序列号数组替代每个slot的状态
  alignas(CacheLineSize) std::array<std::atomic<size_t>, Capacity> sequences_;

  static thread_local size_t tls_cached_head_;

public:
  MPSCQueue() {
    tls_cached_head_ = 0;
    // 初始化序列号
    for (size_t i = 0; i < Capacity; ++i) {
      sequences_[i].store(i, std::memory_order_relaxed);
    }
  }

  template <Writer<T> W> bool push_with_writer(W writer) noexcept {
    size_t pos = tail_.load(std::memory_order_relaxed);

    for (;;) {
      size_t index = pos & (Capacity - 1);
      size_t seq = sequences_[index].load(std::memory_order_acquire);

      if (seq == pos) [[likely]] {
        if (tail_.compare_exchange_weak(pos, pos + 1,
                                        std::memory_order_relaxed)) {
          writer(&buffer_[index].data);
          sequences_[index].store(pos + 1, std::memory_order_release);
          return true;
        }
      } else if (seq < pos) {
        size_t current_head = tls_cached_head_;
        if (pos - current_head >= Capacity) [[unlikely]] {
          current_head = head_.load(std::memory_order_acquire);
          tls_cached_head_ = current_head;
          if (pos - current_head >= Capacity) [[unlikely]] {
            return false; // 队列满
          }
        }
        pos = tail_.load(std::memory_order_relaxed);
      } else {
        pos = tail_.load(std::memory_order_relaxed);
      }
    }
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
    size_t pos = head_.load(std::memory_order_relaxed);
    size_t index = pos & (Capacity - 1);
    size_t seq = sequences_[index].load(std::memory_order_acquire);

    // 检查是否有数据可读
    if (seq == pos + 1) [[likely]] {
      reader(&buffer_[index].data);
      // 标记为可写入
      sequences_[index].store(pos + Capacity, std::memory_order_release);
      head_.store(pos + 1, std::memory_order_release);
      return true;
    }

    return false; // 没有数据
  }

  bool pop(T &value) noexcept {
    return pop([&value](const T *buffer) { value = *buffer; });
  }

  size_t size() const noexcept {
    return tail_.load(std::memory_order_acquire) -
           head_.load(std::memory_order_acquire);
  }

  bool empty() const noexcept { return size() == 0; }
  size_t capacity() const noexcept { return Capacity + 1; }
};

template <typename T, size_t Capacity, size_t SIZE>
thread_local size_t MPSCQueue<T, Capacity, SIZE>::tls_cached_head_ = 0;