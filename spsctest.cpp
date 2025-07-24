#include "spsc.h"
#include "raomeng_spsc.h"
#include "test_suit.h"
#include <atomic>
#include <thread>
#include <vector>

using namespace stest;

void test_single_thread() {
  PRINT_INFO("Single-thread functionality test");
  SPSCQueue<int, 8> q;
  int v;

  ASSERT(q.empty());
  ASSERT_EQ(q.size(), 0u);

  // push & pop
  ASSERT(q.push(1));
  ASSERT(q.push(2));
  ASSERT(q.pop(v));
  ASSERT_EQ(v, 1);
  ASSERT(q.pop(v));
  ASSERT_EQ(v, 2);
  ASSERT(!q.pop(v)); // empty

  // fill queue
  for (int i = 0; i < 7; ++i)
    ASSERT(q.push(i));
  ASSERT(!q.push(100)); // full
  ASSERT_EQ(q.size(), 7u);

  // drain all
  for (int i = 0; i < 7; ++i) {
    ASSERT(q.pop(v));
    ASSERT_EQ(v, i);
  }

  ASSERT(!q.pop(v));
  ASSERT(q.empty());
  PRINT_INFO("Single-thread functionality test passed");
}

void test_bulk() {
  PRINT_INFO("Bulk operations test");
  SPSCQueue<int, 8> q;
  int arr[5] = {10, 20, 30, 40, 50};
  ASSERT_EQ(q.push_bulk(arr, 5), 5u);
  int v;
  for (int i = 0; i < 5; ++i) {
    ASSERT(q.pop(v));
    ASSERT_EQ(v, arr[i]);
  }
  ASSERT(q.empty());

  int arr2[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  ASSERT_EQ(q.push_bulk(arr2, 10), 7u);
  for (int i = 0; i < 7; ++i) {
    ASSERT(q.pop(v));
    ASSERT_EQ(v, arr2[i]);
  }
  ASSERT(!q.pop(v)); // queue empty
  PRINT_INFO("Bulk operations test passed");
}

void test_cache_align_switch() {
  PRINT_INFO("Cache/alignment options test");
  int v;

  // Cache disabled
  SPSCQueue<int, 8, false, true> q1;
  ASSERT_MSG(q1.push(1), "Cache-disabled queue push failed");
  ASSERT_MSG(q1.pop(v) && v == 1, "Cache-disabled queue pop failed");

  // Alignment disabled
  SPSCQueue<int, 8, true, false> q2;
  ASSERT_MSG(q2.push(2), "Alignment-disabled queue push failed");
  ASSERT_MSG(q2.pop(v) && v == 2, "Alignment-disabled queue pop failed");

  // Both disabled
  SPSCQueue<int, 8, false, false> q3;
  ASSERT_MSG(q3.push(3), "Both-disabled queue push failed");
  ASSERT_MSG(q3.pop(v) && v == 3, "Both-disabled queue pop failed");

  PRINT_INFO("Cache/alignment options test passed");
}

// 🔧 修复：FIFO顺序验证
void test_fifo_correctness() {
  PRINT_INFO("FIFO order correctness test");
  SPSCQueue<int, 16> q;

  // Test 1: Simple FIFO
  std::vector<int> input = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  for (int val : input) {
    ASSERT(q.push(val));
  }

  std::vector<int> output;
  int v;
  while (q.pop(v)) {
    output.push_back(v);
  }

  ASSERT_EQ(input.size(), output.size());
  for (size_t i = 0; i < input.size(); ++i) {
    ASSERT_EQ(input[i], output[i]);
  }

  // Test 2: Interleaved push/pop
  std::vector<int> remaining_values; // 记录应该剩余的值

  for (int round = 0; round < 5; ++round) {
    // Push 3 values
    for (int i = 0; i < 3; ++i) {
      int value = round * 10 + i;
      ASSERT(q.push(value));
    }

    // Pop 2 values (verify they're correct)
    for (int i = 0; i < 2; ++i) {
      ASSERT(q.pop(v));
      ASSERT_EQ(v, round * 10 + i);
    }

    // The remaining value should be round * 10 + 2
    remaining_values.push_back(round * 10 + 2);
    q.pop(v); // Pop the last value to maintain FIFO order
  }

  ASSERT(q.empty());
  PRINT_INFO("FIFO order correctness test passed");
}

// 🔧 新增：边界条件测试
void test_boundary_conditions() {
  PRINT_INFO("Boundary conditions test");
  SPSCQueue<int, 4> q; // Small queue for easier boundary testing

  // Test 1: Fill and empty repeatedly
  for (int cycle = 0; cycle < 10; ++cycle) {
    // Fill completely
    for (int i = 0; i < 3; ++i) { // capacity - 1
      ASSERT(q.push(cycle * 100 + i));
    }
    ASSERT(!q.push(999)); // Should fail - queue full
    ASSERT_EQ(q.size(), 3u);

    // Empty completely
    int v;
    for (int i = 0; i < 3; ++i) {
      ASSERT(q.pop(v));
      ASSERT_EQ(v, cycle * 100 + i);
    }
    ASSERT(!q.pop(v)); // Should fail - queue empty
    ASSERT(q.empty());
  }

  // Test 2: Wrap-around behavior
  for (int i = 0; i < 20; ++i) { // More than queue capacity
    ASSERT(q.push(i));
    int v;
    ASSERT(q.pop(v));
    ASSERT_EQ(v, i);
  }

  PRINT_INFO("Boundary conditions test passed");
}

// 🔧 新增：数据完整性验证
void test_data_integrity() {
  PRINT_INFO("Data integrity test");

  struct TestData {
    int id;
    char data[64];
    uint32_t checksum;

    TestData(int id = 0) : id(id) {
      snprintf(data, sizeof(data), "TestData_%d", id);
      checksum = calculate_checksum();
    }

    uint32_t calculate_checksum() const {
      uint32_t sum = id;
      for (size_t i = 0; i < strlen(data); ++i) {
        sum = sum * 31 + data[i];
      }
      return sum;
    }

    bool is_valid() const { return checksum == calculate_checksum(); }
  };

  SPSCQueue<TestData, 32> q;

  // Push structured data
  std::vector<TestData> input_data;
  for (int i = 0; i < 20; ++i) {
    TestData td(i);
    ASSERT(td.is_valid());
    input_data.push_back(td);
    ASSERT(q.push(td));
  }

  // Pop and verify
  TestData output(0);
  for (size_t i = 0; i < input_data.size(); ++i) {
    ASSERT(q.pop(output));
    ASSERT(output.is_valid());
    ASSERT_EQ(output.id, input_data[i].id);
    ASSERT_EQ(strcmp(output.data, input_data[i].data), 0);
  }

  PRINT_INFO("Data integrity test passed");
}

// 🔧 新增：多线程数据一致性验证
template <bool EnableCache = true, bool EnableAlign = true>
void test_multithread_data_consistency_() {
  constexpr size_t N = 100000;
  SPSCQueue<int, 1024, EnableCache, EnableAlign> q;
  std::atomic<bool> done{false};
  std::vector<int> consumed_values;
  std::mutex consumed_mutex;

  std::thread prod([&] {
    for (int i = 0; i < N; ++i) {
      while (!q.push(i))
        std::this_thread::yield();
    }
    done = true;
  });

  std::thread cons([&] {
    int v;
    while (!done || !q.empty()) {
      if (q.pop(v)) {
        std::lock_guard<std::mutex> lock(consumed_mutex);
        consumed_values.push_back(v);
      } else {
        std::this_thread::yield();
      }
    }
  });

  prod.join();
  cons.join();

  // Verify all values received and in order
  ASSERT_EQ(consumed_values.size(), N);
  for (size_t i = 0; i < N; ++i) {
    ASSERT_EQ(consumed_values[i], static_cast<int>(i));
  }
}

void test_multithread_data_consistency() {
  PRINT_INFO("Multi-thread data consistency test");

  Timer timer;
  timer.reset();
  test_multithread_data_consistency_<true, true>();
  PRINT_INFO("Cache=ON, Align=ON consistency verified in {}ms",
             timer.elapsed_ms());

  timer.reset();
  test_multithread_data_consistency_<true, false>();
  PRINT_INFO("Cache=ON, Align=OFF consistency verified in {}ms",
             timer.elapsed_ms());

  timer.reset();
  test_multithread_data_consistency_<false, true>();
  PRINT_INFO("Cache=OFF, Align=ON consistency verified in {}ms",
             timer.elapsed_ms());

  timer.reset();
  test_multithread_data_consistency_<false, false>();
  PRINT_INFO("Cache=OFF, Align=OFF consistency verified in {}ms",
             timer.elapsed_ms());

  PRINT_INFO("Multi-thread data consistency test passed");
}

// 🔧 新增：竞争条件测试
void test_race_conditions() {
  PRINT_INFO("Race conditions test");
  constexpr int iterations = 1000;
  constexpr size_t test_size = 1000;

  for (int iter = 0; iter < iterations; ++iter) {
    SPSCQueue<int, 128> q;
    std::atomic<int> producer_count{0};
    std::atomic<int> consumer_count{0};
    std::atomic<bool> done{false};

    std::thread producer([&]() {
      for (int i = 0; i < test_size; ++i) {
        while (!q.push(i)) {
          std::this_thread::yield();
        }
        producer_count.fetch_add(1);
      }
      done = true;
    });

    std::thread consumer([&]() {
      int value;
      while (!done || !q.empty()) {
        if (q.pop(value)) {
          consumer_count.fetch_add(1);
        } else {
          std::this_thread::yield();
        }
      }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(producer_count.load(), test_size);
    ASSERT_EQ(consumer_count.load(), test_size);
    ASSERT(q.empty());
  }

  PRINT_INFO("Race conditions test passed ({} iterations)", iterations);
}

// 🔧 新增：批量操作正确性验证
void test_bulk_correctness() {
  PRINT_INFO("Bulk operations correctness test");
  SPSCQueue<int, 64> q;

  // Test push_bulk correctness
  std::vector<int> input_data;
  for (int i = 0; i < 50; ++i) {
    input_data.push_back(i * 2); // Even numbers
  }

  // Push in chunks
  size_t total_pushed = 0;
  size_t chunk_size = 7; // Irregular chunk size
  for (size_t offset = 0; offset < input_data.size(); offset += chunk_size) {
    size_t remaining = input_data.size() - offset;
    size_t to_push = std::min(chunk_size, remaining);
    size_t pushed = q.push_bulk(input_data.data() + offset, to_push);
    total_pushed += pushed;

    if (pushed < to_push) {
      // Queue became full, should be able to pop and continue
      int v;
      while (q.pop(v)) {
        // Drain some data
      }
    }
  }

  // Verify all data in queue
  std::vector<int> output_data;
  int v;
  while (q.pop(v)) {
    output_data.push_back(v);
  }

  // Check FIFO order maintained in bulk operations
  ASSERT(output_data.size() > 0);
  for (size_t i = 1; i < output_data.size(); ++i) {
    ASSERT(output_data[i] > output_data[i - 1]); // Should be in order
  }

  PRINT_INFO("Bulk operations correctness test passed");
}

// 🔧 新增：压力测试
void test_stress() {
  PRINT_INFO("Stress test");
  constexpr size_t stress_cycles = 10;
  constexpr size_t ops_per_cycle = 50000;

  for (size_t cycle = 0; cycle < stress_cycles; ++cycle) {
    SPSCQueue<size_t, 256> q;
    std::atomic<bool> producer_done{false};
    std::atomic<size_t> total_consumed{0};

    std::thread producer([&]() {
      for (size_t i = 0; i < ops_per_cycle; ++i) {
        while (!q.push(i)) {
          // Busy wait with occasional yield
          if (i % 1000 == 0) {
            std::this_thread::yield();
          }
        }
      }
      producer_done = true;
    });

    std::thread consumer([&]() {
      size_t value;
      size_t count = 0;
      while (!producer_done || !q.empty()) {
        if (q.pop(value)) {
          ASSERT_EQ(value, count); // Verify order
          ++count;
        } else if (count % 1000 == 0) {
          std::this_thread::yield();
        }
      }
      total_consumed = count;
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(total_consumed.load(), ops_per_cycle);
  }

  PRINT_INFO("Stress test passed ({} cycles)", stress_cycles);
}

template <bool EnableCache = true, bool EnableAlign = true>
void test_multithread_spsc_() {
  constexpr size_t N = 1000000;
  SPSCQueue<int, 1024, EnableAlign, EnableCache> q;
  std::atomic<bool> done{false};

  std::thread prod([&] {
    for (int i = 0; i < N; ++i) {
      while (!q.push(i))
        std::this_thread::yield();
    }
    done = true;
  });

  std::thread cons([&] {
    int expect = 0, v;
    while (!done || expect < N) {
      if (q.pop(v)) {
        ASSERT_EQ(v, expect);
        ++expect;
      } else {
        std::this_thread::yield();
      }
    }
  });

  prod.join();
  cons.join();
}

void test_multithread_spsc() {
  PRINT_INFO("SPSC multi-thread correctness test");

  Timer timer;
  timer.reset();
  test_multithread_spsc_<true, true>();
  PRINT_INFO("Cache=ON, Align=ON time: {}ms", timer.elapsed_ms());

  timer.reset();
  test_multithread_spsc_<true, false>();
  PRINT_INFO("Cache=ON, Align=OFF time: {}ms", timer.elapsed_ms());

  timer.reset();
  test_multithread_spsc_<false, true>();
  PRINT_INFO("Cache=OFF, Align=ON time: {}ms", timer.elapsed_ms());

  timer.reset();
  test_multithread_spsc_<false, false>();
  PRINT_INFO("Cache=OFF, Align=OFF time: {}ms", timer.elapsed_ms());

  PRINT_INFO("SPSC multi-thread test passed");
}

template <typename Q> void test_performance_spsc_(Q &q, const size_t N) {
  for (size_t i = 0; i < N; ++i) {
    while (!q.push(i)) {
    }
    int v;
    while (!q.pop(v)) {
    }
  }
}

template <typename Q> void test_raomeng_spsc_(Q &q, const size_t N) {
  for (size_t i = 0; i < N; ++i) {
    while (!q.tryPush([&](int *p) { *p = i; })) {
    }
    int v;
    while (!q.tryPop([&](const int *val) { v = *val; })) {
    }
  }
}

void test_performance() {
  PRINT_INFO("Performance test (single-thread loop)");
  constexpr size_t iter = 100;
  constexpr size_t N = 10'000'000;

  struct TestConfig {
    std::string name;
    std::function<void()> test_func;
  };

  SPSCQueue<int, 1024> q;
  SPSCQueue<int, 1024, true, false> q_no_align;
  SPSCQueue<int, 1024, false, true> q_no_cache;
  SPSCQueue<int, 1024, false, false> q_no_cache_no_align;
  SPSCQueueOPT<int, 1024> raomeng_q;

  std::vector<TestConfig> configs = {
      {"SPSCQueue<Cache,Align>", [&]() { test_performance_spsc_(q, N); }},
      {"SPSCQueue<Cache,NoAlign>",
       [&]() { test_performance_spsc_(q_no_align, N); }},
      {"SPSCQueue<NoCache,Align>",
       [&]() { test_performance_spsc_(q_no_cache, N); }},
      {"SPSCQueue<NoCache,NoAlign>",
       [&]() { test_performance_spsc_(q_no_cache_no_align, N); }},
      {"SPSCQueueOPT", [&]() { test_raomeng_spsc_(raomeng_q, N); }}};

  for (const auto &config : configs) {
    PRINT_INFO("{} performance test", config.name);
    auto avg = Timer::measure(config.test_func, iter);
    double throughput = N * 2 / avg / 1; // Mops/s
    PRINT_INFO("{} throughput: {} Mops/s", config.name, throughput);
  }
}

template <typename Q> void test_performance_mt_(Q &q, const size_t N) {
  std::atomic<bool> done{false};
  std::thread prod([&] {
    for (int i = 0; i < N; ++i) {
      while (!q.push(i))
        std::this_thread::yield();
    }
    done = true;
  });

  std::thread cons([&] {
    int v, cnt = 0;
    while (!done || cnt < N) {
      if (q.pop(v))
        ++cnt;
      else
        std::this_thread::yield();
    }
  });

  prod.join();
  cons.join();
}

template <typename Q> void test_performance_mt_raomeng_(Q &q, const size_t N) {
  std::atomic<bool> done{false};
  std::thread prod([&] {
    for (int i = 0; i < N; ++i) {
      while (!q.tryPush([&](int *p) { *p = i; }))
        std::this_thread::yield();
    }
    done = true;
  });

  std::thread cons([&] {
    int v, cnt = 0;
    while (!done || cnt < N) {
      if (q.tryPop([&](const int *val) { v = *val; }))
        ++cnt;
      else
        std::this_thread::yield();
    }
  });

  prod.join();
  cons.join();
}

template <size_t cap = 1024, size_t N = 10'000'000, size_t iter = 100>
void test_performance_mt() {
  PRINT_INFO("SPSC multi-thread performance test - cap={}, N={}, iter={}", cap,
             N, iter);

  struct MTTestConfig {
    std::string name;
    std::function<void()> test_func;
  };

  SPSCQueue<int, cap> q;
  SPSCQueue<int, cap, true, false> q_no_align;
  SPSCQueue<int, cap, false, true> q_no_cache;
  SPSCQueue<int, cap, false, false> q_nocache_no_align;
  SPSCQueueOPT<int, cap> raomeng_q;

  std::vector<MTTestConfig> configs = {
      {"SPSCQueue<Cache,Align>", [&]() { test_performance_mt_(q, N); }},
      {"SPSCQueue<Cache,NoAlign>",
       [&]() { test_performance_mt_(q_no_align, N); }},
      {"SPSCQueue<NoCache,Align>",
       [&]() { test_performance_mt_(q_no_cache, N); }},
      {"SPSCQueue<NoCache,NoAlign>",
       [&]() { test_performance_mt_(q_nocache_no_align, N); }},
      {"SPSCQueueOPT", [&]() { test_performance_mt_raomeng_(raomeng_q, N); }}};

  for (const auto &config : configs) {
    auto avg = Timer::measure(config.test_func, iter);
    double throughput = N * 2 / avg / 1; // Mops/s
    PRINT_INFO("{} throughput: {} Mops/s", config.name, throughput);
  }
}

template <size_t BATCH, typename Q>
void test_bulk_multithread_(Q &q, const size_t N) {
  std::atomic<bool> done{false};

  std::thread prod([&] {
    int arr[BATCH];
    for (int i = 0; i < N; i += BATCH) {
      int this_batch = std::min(BATCH, N - i);
      for (int j = 0; j < this_batch; ++j)
        arr[j] = i + j;
      size_t pushed = 0;
      while (pushed < this_batch)
        pushed += q.push_bulk(arr + pushed, this_batch - pushed);
    }
    done = true;
  });

  std::thread cons([&] {
    int arr[BATCH];
    int cnt = 0;
    while (!done || cnt < N) {
      size_t popped = q.pop_bulk(arr, BATCH);
      for (size_t j = 0; j < popped; ++j) {
        ASSERT_EQ(arr[j], cnt);
        ++cnt;
      }
      if (popped == 0)
        std::this_thread::yield();
    }
  });

  prod.join();
  cons.join();
}

template <size_t cap = 1024, size_t N = 10'000'000, size_t BATCH = 32,
          size_t iter = 100>
void test_bulk_multithread() {
  PRINT_INFO("SPSC bulk multi-thread test - cap={}, N={}, BATCH={}, iter={}",
             cap, N, BATCH, iter);

  struct BulkTestConfig {
    std::string name;
    std::function<void()> test_func;
  };

  SPSCQueue<int, cap> q;
  SPSCQueue<int, cap, true, false> q_no_align;
  SPSCQueue<int, cap, false, true> q_no_cache;
  SPSCQueue<int, cap, false, false> q_no_cache_no_align;

  std::vector<BulkTestConfig> configs = {
      {"SPSCQueue<Cache,Align>",
       [&]() { test_bulk_multithread_<BATCH>(q, N); }},
      {"SPSCQueue<Cache,NoAlign>",
       [&]() { test_bulk_multithread_<BATCH>(q_no_align, N); }},
      {"SPSCQueue<NoCache,Align>",
       [&]() { test_bulk_multithread_<BATCH>(q_no_cache, N); }},
      {"SPSCQueue<NoCache,NoAlign>",
       [&]() { test_bulk_multithread_<BATCH>(q_no_cache_no_align, N); }}};

  for (const auto &config : configs) {
    auto avg = Timer::measure(config.test_func, iter);
    double throughput = N * 2 / avg / 1; // Mops/s
    PRINT_INFO("{} bulk throughput: {} Mops/s", config.name, throughput);
  }
}

int main() {
  try {
    PRINT_INFO("Starting SPSC queue test suite");

    test_single_thread();
    test_bulk();
    test_cache_align_switch();
    test_fifo_correctness();
    test_boundary_conditions();
    test_data_integrity();
    test_bulk_correctness();

    test_multithread_spsc();
    test_multithread_data_consistency();
    test_race_conditions();
    test_stress();

    // 🔧 性能测试
    test_performance();
    test_performance_mt();
    test_bulk_multithread<1024, 10'000'000, 32, 100>();
    test_bulk_multithread<1024, 10'000'000, 64, 100>();

    PRINT_INFO("All SPSC tests completed successfully");
    return 0;
  } catch (const std::exception &e) {
    PRINT_ERROR("Test failed: {}", e.what());
    return 1;
  }
}