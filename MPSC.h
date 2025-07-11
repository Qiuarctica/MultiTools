#pragma once

#include "defs.h"
#include "spsc.h"
#include <array>
#include <atomic>
#include <cstddef>

template <typename T, size_t Capacity, size_t MaxProducers = 4>
class MPSCQueue {
  static_assert(IS_POWDER_OF_TWO_AND_GREATER_THAN_ZERO(Capacity));
  using SPSCType = SPSCQueue<T, Capacity, true, true>;

private:
  alignas(CacheLineSize) std::array<SPSCType, MaxProducers> queues_;
  alignas(CacheLineSize) std::atomic<size_t> producer_counter_{0};
  alignas(CacheLineSize) mutable std::atomic<size_t> consumer_round_robin_{0};

  thread_local static size_t my_queue_id_;
  thread_local static bool queue_assigned_;

public:
  using value_type = T;

  MPSCQueue() = default;
  ~MPSCQueue() = default;
  MPSCQueue(const MPSCQueue &) = delete;
  MPSCQueue &operator=(const MPSCQueue &) = delete;

  template <Writer<T> W> bool push_with_writer(W writer) noexcept {
    if (!queue_assigned_) [[unlikely]] {
      my_queue_id_ = producer_counter_.fetch_add(1, std::memory_order_relaxed) %
                     MaxProducers;
      queue_assigned_ = true;
    }

    return queues_[my_queue_id_].push_with_writer(writer);
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
    const size_t start_idx =
        consumer_round_robin_.load(std::memory_order_relaxed);

    for (size_t i = 0; i < MaxProducers; ++i) {
      const size_t queue_idx = (start_idx + i) % MaxProducers;
      if (queues_[queue_idx].pop(reader)) {
        consumer_round_robin_.store((queue_idx + 1) % MaxProducers,
                                    std::memory_order_relaxed);
        return true;
      }
    }
    return false;
  }

  bool pop(T &value) noexcept {
    return pop([&value](const T *buffer) { value = *buffer; });
  }

  template <Reader<T> R> size_t pop_bulk(R reader, size_t max_items) noexcept {
    size_t total_popped = 0;
    const size_t start_idx =
        consumer_round_robin_.load(std::memory_order_relaxed);

    for (size_t round = 0; round < MaxProducers && total_popped < max_items;
         ++round) {
      const size_t queue_idx = (start_idx + round) % MaxProducers;
      auto &queue = queues_[queue_idx];

      const size_t batch_size = std::min(max_items - total_popped, size_t(32));
      for (size_t i = 0; i < batch_size; ++i) {
        if (queue.pop_with_reader(reader)) {
          ++total_popped;
        } else {
          break;
        }
      }

      if (total_popped > 0) {
        consumer_round_robin_.store((queue_idx + 1) % MaxProducers,
                                    std::memory_order_relaxed);
      }
    }

    return total_popped;
  }

  bool empty() const noexcept {
    for (const auto &queue : queues_) {
      if (!queue.empty())
        return false;
    }
    return true;
  }

  size_t size() const noexcept {
    size_t total = 0;
    for (const auto &queue : queues_) {
      total += queue.size();
    }
    return total;
  }
};

// thread_local 变量定义
template <typename T, size_t Capacity, size_t MaxProducers>
thread_local size_t MPSCQueue<T, Capacity, MaxProducers>::my_queue_id_ = 0;

template <typename T, size_t Capacity, size_t MaxProducers>
thread_local bool MPSCQueue<T, Capacity, MaxProducers>::queue_assigned_ = false;