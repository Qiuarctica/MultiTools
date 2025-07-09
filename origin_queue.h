#pragma once

#include <mutex>
#include <queue>

// 原始线程安全队列
template <typename T> class OriginMultipleSafeQueue {
  static_assert(std::is_trivially_copyable_v<T>,
                "T must be trivially copyable");

  std::queue<T> queue_;
  std::mutex mutex_;

public:
  using value_type = T;

  OriginMultipleSafeQueue() = default;
  ~OriginMultipleSafeQueue() = default;
  OriginMultipleSafeQueue(const OriginMultipleSafeQueue &) = delete;
  OriginMultipleSafeQueue &operator=(const OriginMultipleSafeQueue &) = delete;

  bool push(const T &value) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(value);
    return true;
  }

  bool pop(T &value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
      return false;
    }
    value = queue_.front();
    queue_.pop();
    return true;
  }
  size_t size() {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }
  bool empty() {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
  }
  size_t push_bulk(const T *data, size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t pushed = 0;
    for (size_t i = 0; i < count; ++i) {
      queue_.push(data[i]);
      ++pushed;
    }
    return pushed;
  }
  size_t pop_bulk(T *data, size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t popped = 0;
    while (popped < count && !queue_.empty()) {
      data[popped++] = queue_.front();
      queue_.pop();
    }
    return popped;
  }
};