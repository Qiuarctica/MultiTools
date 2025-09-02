#pragma once
#include "../utils/defs.h"
#include <array>
#include <atomic>
#include <cstring>

// 有缓存行对齐
template <typename T, size_t Capacity> class SPSCQueue {
  static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                "Capacity must be a power of two and at least 2");
  static_assert(std::is_trivially_copyable_v<T>,
                "T must be trivially copyable");

private:
  struct alignas(CacheLineSize) ProducerState {
    std::atomic<size_t> head{0};
    size_t cached_tail{0};
    char padding[CacheLineSize - sizeof(std::atomic<size_t>) - sizeof(size_t)];
  };

  // 🔧 简化的消费者状态（始终缓存行对齐）
  struct alignas(CacheLineSize) ConsumerState {
    std::atomic<size_t> tail{0};
    size_t cached_head{0};
    char padding[CacheLineSize - sizeof(std::atomic<size_t>) - sizeof(size_t)];
  };

  ProducerState prod_;
  ConsumerState cons_;
  std::array<T, Capacity> buffer_;

  static constexpr size_t mask() noexcept { return Capacity - 1; }

  constexpr size_t nextIndex(size_t idx) const noexcept {
    return (idx + 1) & mask();
  }
  constexpr size_t nextIndex(size_t idx, size_t offset) const noexcept {
    return (idx + offset) & mask();
  }
  inline size_t available_space(size_t head) const noexcept {
    return (available_space(head, cons_.tail.load(std::memory_order_acquire)));
  }
  constexpr size_t available_space(size_t head, size_t tail) const noexcept {
    return (Capacity + tail - head - 1) & mask();
  }

  void prefetch_write(T *ptr, size_t count) const noexcept {
    for (size_t i = 0; i < count; i += CacheLineSize / sizeof(T)) {
      __builtin_prefetch(ptr + i + CacheLineSize / sizeof(T), 1, 1);
    }
  }

  void prefetch_read(const T *ptr, size_t count) const noexcept {
    for (size_t i = 0; i < count; i += CacheLineSize / sizeof(T)) {
      __builtin_prefetch(ptr + i + CacheLineSize / sizeof(T), 0, 1);
    }
  }

  // 🔧 优化的内存排序（平台特定）
#ifdef __x86_64__
  static constexpr auto producer_store_order = std::memory_order_relaxed;
  static constexpr auto consumer_load_order = std::memory_order_relaxed;
#else
  static constexpr auto producer_store_order = std::memory_order_release;
  static constexpr auto consumer_load_order = std::memory_order_acquire;
#endif

public:
  using value_type = T;
  SPSCQueue() = default;
  ~SPSCQueue() = default;
  SPSCQueue(const SPSCQueue &) = delete;
  SPSCQueue &operator=(const SPSCQueue &) = delete;

  // Writer 语义的 push 操作
  template <Writer<T> W> [[gnu::hot]] bool push_with_writer(W writer) noexcept {
    const size_t head = prod_.head.load(std::memory_order_relaxed);
    const size_t next_head = nextIndex(head);

    if (next_head == prod_.cached_tail) [[unlikely]] {
      prod_.cached_tail = cons_.tail.load(std::memory_order_consume);
      if (next_head == prod_.cached_tail) [[unlikely]] {
        return false;
      }
    }

#ifdef ENABLE_PREFETCH
    __builtin_prefetch(&buffer_[head], 1, 1);
#endif

    writer(&buffer_[head]);
    std::atomic_thread_fence(std::memory_order_release);
    prod_.head.store(next_head, producer_store_order);
    return true;
  }

  template <typename U>
  bool push(U &&value) noexcept
    requires(!Writer<U, T>)
  {
    if constexpr (std::is_same_v<std::decay_t<U>, T> &&
                  std::is_trivially_copyable_v<T>) {
      // 直接优化路径，避免 lambda
      const size_t head = prod_.head.load(std::memory_order_relaxed);
      const size_t next_head = nextIndex(head);

      if (next_head == prod_.cached_tail) [[unlikely]] {
        prod_.cached_tail = cons_.tail.load(consumer_load_order);
        if (next_head == prod_.cached_tail) [[unlikely]] {
          return false;
        }
      }
#ifdef ENABLE_PREFETCH
      __builtin_prefetch(&buffer_[head], 1, 1);
#endif
      buffer_[head] = std::forward<U>(value);
      std::atomic_thread_fence(std::memory_order_release);
      prod_.head.store(next_head, producer_store_order);
      return true;
    } else {
      return push_with_writer([value = std::forward<U>(value)](T *buffer) {
        *buffer = std::move(value);
      });
    }
  }

  template <typename U>
  bool push(U writer) noexcept
    requires(Writer<U, T>)
  {
    return push_with_writer(writer);
  }

  // Reader 语义的 pop 操作
  template <Reader<T> R> [[gnu::hot]] bool pop(R reader) noexcept {
    const size_t tail = cons_.tail.load(std::memory_order_relaxed);

    if (tail == cons_.cached_head) [[unlikely]] {
      cons_.cached_head = prod_.head.load(std::memory_order_acquire);
      if (tail == cons_.cached_head) [[unlikely]] {
        return false;
      }
    }

    // 让 reader 直接读取缓冲区位置
    reader(&buffer_[tail]);
    cons_.tail.store(nextIndex(tail), std::memory_order_release);
    return true;
  }

  bool pop(T &value) noexcept {
    if constexpr (std::is_trivially_copyable_v<T>) {
      const size_t tail = cons_.tail.load(std::memory_order_relaxed);

      if (tail == cons_.cached_head) [[unlikely]] {
        cons_.cached_head = prod_.head.load(std::memory_order_acquire);
        if (tail == cons_.cached_head) [[unlikely]] {
          return false;
        }
      }

      value = buffer_[tail];
      cons_.tail.store(nextIndex(tail), std::memory_order_release);
      return true;
    } else {
      return pop([&value](const T *buffer) { value = *buffer; });
    }
  }

  // 批量 Writer 操作
  template <BulkWriter<T> W>
  size_t push_bulk(W writer, size_t max_count) noexcept {
    const size_t head = prod_.head.load(std::memory_order_relaxed);
    size_t tail;

    tail = prod_.cached_tail;

    size_t available = available_space(head, tail);
    if (available < max_count) [[unlikely]] {
      tail = cons_.tail.load(std::memory_order_acquire);
      prod_.cached_tail = tail;
      available = available_space(head, tail);
    }

    const size_t can_write = std::min(max_count, available);
    if (can_write == 0) [[unlikely]] {
      return 0;
    }

    const size_t end_of_buffer = Capacity - head;

    if (can_write <= end_of_buffer) [[likely]] {
      // 预取
      T *dest = &buffer_[head];
      for (size_t i = 0; i < can_write; i += CacheLineSize / sizeof(T)) {
        __builtin_prefetch(dest + i + CacheLineSize / sizeof(T), 1, 0);
      }
      // 连续写入
      writer(&buffer_[head], can_write, 0);
    } else {
      // 分两段写入
      const size_t part1 = end_of_buffer;
      writer(&buffer_[head], part1, 0);
      const size_t part2 = can_write - part1;
      writer(&buffer_[0], part2, part1);
    }
    prod_.head.store(nextIndex(head, can_write), std::memory_order_release);

    return can_write;
  }

  size_t push_bulk(const T *data, size_t count) noexcept {
    return push_bulk(
        [data](T *buffer, size_t n, size_t offset) {
          std::memcpy(buffer, data + offset, n * sizeof(T));
          return n;
        },
        count);
  }

  // 批量 Reader 操作
  template <BulkReader<T> R>
  size_t pop_bulk(R reader, size_t max_count) noexcept {
    const size_t tail = cons_.tail.load(std::memory_order_relaxed);
    size_t head;

    head = cons_.cached_head;

    size_t available = available_space(tail, head + 1);
    if (available < max_count) [[unlikely]] {
      head = prod_.head.load(std::memory_order_acquire);
      cons_.cached_head = head;
      available = available_space(tail, head + 1);
    }

    const size_t can_read = std::min(max_count, available);
    if (can_read == 0) [[unlikely]] {
      return 0;
    }

    const size_t end_of_buffer = Capacity - tail;

    if (can_read <= end_of_buffer) [[likely]] {
      // 预取
      const T *src = &buffer_[tail];
      for (size_t i = 0; i < can_read; i += CacheLineSize / sizeof(T)) {
        __builtin_prefetch(src + i + CacheLineSize / sizeof(T), 0, 0);
      }
      // 连续读取
      reader(&buffer_[tail], can_read, 0);
    } else {
      // 分两段读取
      const size_t part1 = end_of_buffer;
      reader(&buffer_[tail], part1, 0);
      const size_t part2 = can_read - part1;
      reader(&buffer_[0], part2, part1);
    }
    if (can_read > 0) {
      cons_.tail.store(nextIndex(tail, can_read), std::memory_order_release);
    }
    return can_read;
  }

  size_t pop_bulk(T *data, size_t count) noexcept {
    return pop_bulk(
        [data](const T *buffer, size_t n, size_t offset) {
          std::memcpy(data + offset, buffer, n * sizeof(T));
          return n;
        },
        count);
  }

  [[gnu::pure]] bool empty() const noexcept {
    return prod_.head.load(std::memory_order_acquire) ==
           cons_.tail.load(std::memory_order_acquire);
  }

  [[gnu::pure]] size_t size() const noexcept {
    const size_t head = prod_.head.load(std::memory_order_acquire);
    const size_t tail = cons_.tail.load(std::memory_order_acquire);
    return (head >= tail) ? (head - tail) : (Capacity - tail + head);
  }

  [[gnu::pure]] constexpr size_t capacity() const noexcept {
    return Capacity - 1; // 保留一个位置用于区分满和空
  }
  void warm_cache() noexcept {
    // 预热缓存，减少首次访问延迟
    prod_.cached_tail = cons_.tail.load(std::memory_order_relaxed);
    cons_.cached_head = prod_.head.load(std::memory_order_relaxed);
  }
};
