#pragma once

// 需要保证顺的场景下的MPSC，计划采用Ringbuffer实现，用户需要传入一个seq表示消息的顺序，队列将会严格按照seq升序进行处理，中途如果有seq丢失可能会卡住或者过一段时间跳过
// 排序采用原地算法，不会丢弃任何数据，会采用循环等待的方式进行等待。
#include "../utils/defs.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <iostream>
#include <thread>
#include <unistd.h>

// size必须为2^N
template <typename T, size_t Capacity, size_t A = 0> class MPSCQueue {
  static_assert(IS_POWDER_OF_TWO(Capacity), "size must be a power of two");
  using SequenceType = uint64_t;

  static constexpr size_t mask = Capacity - 1;
  struct Slot {
    alignas(CacheLineSize) std::atomic<SequenceType> seq; // 消息的序列号
    T data;
  };

  alignas(CacheLineSize) std::atomic<size_t> size_ = 0;
  alignas(CacheLineSize) std::atomic<size_t> global_seq{0};
  constexpr SequenceType write_mask(SequenceType seq) { return seq + 1; }

  std::array<Slot, Capacity> buffer_;
  // 消费者将严格按照顺序消费读取信息
  size_t expect_next_{0};
  // 能写入进槽位的前提是其seq等于传入seq
  bool can_write_to_slot(size_t slot_idx, SequenceType seq) {
    return buffer_[slot_idx].seq.load(std::memory_order_relaxed) == seq;
  }
  // 消费者能消费的前提是有生产者写过
  bool can_pop_slot(size_t slot_idx, SequenceType seq) {
    return buffer_[slot_idx].seq.load(std::memory_order_relaxed) ==
           write_mask(seq);
  }

public:
  MPSCQueue() {
    for (size_t i = 0; i < Capacity; ++i) {
      buffer_[i].seq.store(i, std::memory_order_relaxed);
    }
  }

  template <Writer<T> W>
  bool push_with_writer(W writer, SequenceType seq) noexcept {
    size_t slot_idx = seq & mask;
    while (!can_write_to_slot(slot_idx, seq)) {
      // 自旋等待
      std::this_thread::yield();
    }
    writer(&buffer_[slot_idx].data);
    size_.fetch_add(1, std::memory_order_relaxed);
    // 更新seq表示已经写入
    buffer_[slot_idx].seq.store(write_mask(seq), std::memory_order_release);
    return true;
  }

  template <Writer<T> W> bool push_with_writer(W writer) {
    return push_with_writer(writer,
                            global_seq.fetch_add(1, std::memory_order_relaxed));
  }

  template <Reader<T> R> bool pop(R reader) noexcept {
    // if (empty()) {
    //   return false;
    // }
    size_t expect_seq = expect_next_;
    size_t slot_idx = expect_seq & mask;
    while (!can_pop_slot(slot_idx, expect_seq)) {
      // 自旋等待
      std::this_thread::yield();
    }
    reader(&buffer_[slot_idx].data);
    size_.fetch_sub(1, std::memory_order_relaxed);
    // 更新seq表示已经读完
    buffer_[slot_idx].seq.store(expect_seq + Capacity,
                                std::memory_order_release);
    expect_next_++;
    return true;
  }

  bool pop(T &value) noexcept {
    return pop([&value](const T *buffer) { value = *buffer; });
  }

  template <typename U>
  bool push(U &&value, SequenceType seq) noexcept
    requires(!Writer<U, T>)
  {
    return push_with_writer([value = std::forward<U>(value)](
                                T *buffer) { *buffer = std::move(value); },
                            seq);
  }

  template <typename U>
  bool push(U writer, SequenceType seq) noexcept
    requires(Writer<U, T>)
  {
    return push_with_writer(writer, seq);
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

  size_t size() const { return size_.load(std::memory_order_relaxed); }
  bool empty() const noexcept {
    return size_.load(std::memory_order_relaxed) == 0;
  }
  SequenceType get_expected_next() const noexcept { return expect_next_; }
  size_t capacity() { return Capacity + 1; }
  void debug_print() const {
    std::cout << "Expected next: " << get_expected_next() << std::endl;
    std::cout << "Queue size: " << size() << std::endl;
    std::cout << "First 8 slots state:" << std::endl;
    for (size_t i = 0; i < std::min(Capacity, size_t(8)); ++i) {
      std::cout << "[" << i << "]: seq=" << buffer_[i].seq.load() << std::endl;
    }
  }
};
