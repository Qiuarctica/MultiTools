#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <queue>
#include <type_traits>

template <class T>
concept Queue =
    requires(T q, typename T::value_type v, typename T::value_type &out_v,
             typename T::value_type *data) {
      // 检查 push 是否能同时接受左值和右值
      { q.push(v) } -> std::convertible_to<bool>;
      { q.push(std::move(v)) } -> std::convertible_to<bool>;

      // 检查 pop 是否能接受一个左值引用作为输出参数
      { q.pop(out_v) } -> std::convertible_to<bool>;

      // 检查 size 和 empty
      { q.size() } -> std::convertible_to<size_t>;
      { q.empty() } -> std::convertible_to<bool>;

      // 检查 push_bulk
      { q.push_bulk(data, 0) } -> std::convertible_to<size_t>;
      // 检查 pop_bulk
      { q.pop_bulk(data, 0) } -> std::convertible_to<size_t>;
    };

static constexpr size_t CacheLineSize = 64;

template <bool Align> struct ProducerStateImpl;
// 有缓存行对齐
template <> struct ProducerStateImpl<true> {
  std::atomic<size_t> head = 0;
  size_t cached_tail = 0;
  char padding[CacheLineSize - sizeof(std::atomic<size_t>) - sizeof(size_t)] = {
      0};
} __attribute__((aligned(CacheLineSize)));
// 没有缓存行对齐
template <> struct ProducerStateImpl<false> {
  std::atomic<size_t> head = 0;
  size_t cached_tail = 0;
};

template <bool Align> struct ConsumerStateImpl;
// 有缓存行对齐
template <> struct ConsumerStateImpl<true> {
  std::atomic<size_t> tail = 0;
  size_t cached_head = 0;
  char padding[CacheLineSize - sizeof(std::atomic<size_t>) - sizeof(size_t)] = {
      0};
} __attribute__((aligned(CacheLineSize)));
// 没有缓存行对齐
template <> struct ConsumerStateImpl<false> {
  std::atomic<size_t> tail = 0;
  size_t cached_head = 0;
};

template <typename T, size_t Capacity, bool EnableCache = true,
          bool EnableAlign = true>
class SPSCQueue {
  static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                "Capacity must be a power of two and at least 2");
  static_assert(std::is_trivially_copyable_v<T>,
                "T must be trivially copyable");

  ProducerStateImpl<EnableAlign> prod_{};
  ConsumerStateImpl<EnableAlign> cons_{};
  alignas(CacheLineSize) std::array<T, Capacity> buffer_;

public:
  using value_type = T;
  SPSCQueue() = default;
  ~SPSCQueue() = default;
  SPSCQueue(const SPSCQueue &) = delete;
  SPSCQueue &operator=(const SPSCQueue &) = delete;

  template <typename U> bool push(U &&value) noexcept {
    const size_t head = prod_.head.load(std::memory_order_relaxed);
    const size_t next_head = nextIndex(head);
    // 快速路径检查
    if constexpr (EnableCache) {
      if (next_head == prod_.cached_tail) [[unlikely]] {
        prod_.cached_tail = cons_.tail.load(std::memory_order_acquire);
        if (next_head == prod_.cached_tail) {
          return false; // 队列已满
        }
      }
    } else {
      if (available_space(head) == 0) [[unlikely]] {
        return false; // 队列已满
      }
    }

    buffer_[head] = std::forward<U>(value);
    // 发布操作
    prod_.head.store(next_head, std::memory_order_release);
    return true;
  }

  bool pop(T &value) noexcept {
    const size_t tail = cons_.tail.load(std::memory_order_relaxed);
    // 快速路径
    if constexpr (EnableCache) {
      if (tail == cons_.cached_head) [[unlikely]] {
        cons_.cached_head = prod_.head.load(std::memory_order_acquire);
        if (tail == cons_.cached_head) {
          return false; // 队列为空
        }
      }
    } else {
      if (tail == prod_.head.load(std::memory_order_acquire)) [[unlikely]] {
        return false; // 队列为空
      }
    }
    // 读取数据
    value = buffer_[tail];
    cons_.tail.store(nextIndex(tail), std::memory_order_release);
    return true;
  }

  size_t push_bulk(const T *data, size_t count) noexcept {
    const size_t head = prod_.head.load(std::memory_order_relaxed);
    size_t tail;
    // 检查可用空间
    if constexpr (EnableCache) {
      tail = prod_.cached_tail;
    } else {
      tail = cons_.tail.load(std::memory_order_acquire);
    }
    size_t available = available_space(head, tail);
    if constexpr (EnableCache) {
      if (available < count) [[unlikely]] {
        tail = cons_.tail.load(std::memory_order_acquire);
        prod_.cached_tail = tail;
        available = available_space(head, tail);
      }
    }
    const size_t to_write = std::min(count, available);
    if (to_write == 0) [[unlikely]] {
      return 0;
    }
    // 检查是否跨越了环形缓冲区的边界
    const size_t end_of_buffer = Capacity - head;
    if (to_write <= end_of_buffer) [[likely]] {
      std::memcpy(&buffer_[head], data, to_write * sizeof(T));
    } else {
      const size_t part1 = end_of_buffer;
      std::memcpy(&buffer_[head], data, part1 * sizeof(T));
      const size_t part2 = to_write - part1;
      std::memcpy(&buffer_[0], data + part1, part2 * sizeof(T));
    }

    // 更新 head 指针并发布
    prod_.head.store(nextIndex(head, to_write), std::memory_order_release);

    return to_write;
  }

  size_t pop_bulk(T *data, size_t count) noexcept {
    const size_t tail = cons_.tail.load(std::memory_order_relaxed);
    size_t head;
    // 检查可用数据
    if constexpr (EnableCache) {
      head = cons_.cached_head;
    } else {
      head = prod_.head.load(std::memory_order_acquire);
    }
    size_t available = available_space(tail, head + 1);
    if constexpr (EnableCache) {
      if (available < count) [[unlikely]] {
        head = prod_.head.load(std::memory_order_acquire);
        cons_.cached_head = head;
        available = available_space(tail, head + 1);
      }
    }
    const size_t to_read = std::min(count, available);
    if (to_read == 0) [[unlikely]] {
      return 0;
    }
    // 检查是否跨越了环形缓冲区的边界
    const size_t end_of_buffer = Capacity - tail;
    if (to_read <= end_of_buffer) [[likely]] {
      std::memcpy(data, &buffer_[tail], to_read * sizeof(T));
    } else {
      const size_t part1 = end_of_buffer;
      std::memcpy(data, &buffer_[tail], part1 * sizeof(T));
      const size_t part2 = to_read - part1;
      std::memcpy(data + part1, &buffer_[0], part2 * sizeof(T));
    }
    // 更新 tail 指针并发布
    cons_.tail.store(nextIndex(tail, to_read), std::memory_order_release);
    return to_read;
  }

  bool empty() const noexcept {
    return prod_.head.load(std::memory_order_acquire) ==
           cons_.tail.load(std::memory_order_acquire);
  }

  size_t size() const noexcept {
    const size_t head = prod_.head.load(std::memory_order_acquire);
    const size_t tail = cons_.tail.load(std::memory_order_acquire);
    return (head >= tail) ? (head - tail) : (Capacity - tail + head);
  }

  size_t capacity() const noexcept {
    return Capacity - 1; // 保留一个位置用于区分满和空
  }

private:
  inline size_t nextIndex(size_t idx) const noexcept {
    return (idx + 1) & (Capacity - 1);
  }
  inline size_t nextIndex(size_t idx, size_t offset) const noexcept {
    return (idx + offset) & (Capacity - 1);
  }
  inline size_t available_space(size_t head) const noexcept {
    const size_t tail = cons_.tail.load(std::memory_order_acquire);
    return (Capacity + tail - head - 1) & (Capacity - 1);
  }
  inline size_t available_space(size_t head, size_t tail) const noexcept {
    return (Capacity + tail - head - 1) & (Capacity - 1);
  }
};
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