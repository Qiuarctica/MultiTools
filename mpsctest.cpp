#include "mpsc.h"
#include "test_suit.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

using namespace stest;

void test_single_thread() {
  PRINT_INFO("MPSC single-thread functionality test");
  MPSCQueue<int, 8> q;
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
  ASSERT(!q.pop(v)); // empty queue

  // fill queue (capacity depends on implementation)
  size_t max_items = q.capacity() - 1; // Leave one slot for safety
  for (size_t i = 0; i < max_items; ++i) {
    ASSERT(q.push(static_cast<int>(i)));
  }
  ASSERT(!q.push(100)); // queue full

  // drain all
  for (size_t i = 0; i < max_items; ++i) {
    ASSERT(q.pop(v));
    ASSERT_EQ(v, static_cast<int>(i));
  }

  ASSERT(!q.pop(v));
  ASSERT(q.empty());
  PRINT_INFO("Single-thread test passed");
}

void test_writer_reader_semantics() {
  PRINT_INFO("MPSC Writer/Reader semantics test");
  MPSCQueue<int, 8> q;

  // Writer semantics test
  ASSERT(q.push([](int *ptr) { *ptr = 42; }));
  ASSERT(q.push([](int *ptr) { *ptr = 100; }));

  // Reader semantics test
  int sum = 0;
  ASSERT(q.pop([&sum](const int *ptr) { sum += *ptr; }));
  ASSERT(q.pop([&sum](const int *ptr) { sum += *ptr; }));

  ASSERT_EQ(sum, 142); // 42 + 100
  ASSERT(q.empty());

  PRINT_INFO("Writer/Reader semantics test passed");
}

// FIFO 顺序验证
void test_fifo_correctness() {
  PRINT_INFO("MPSC FIFO order correctness test");
  MPSCQueue<int, 64> q;

  // Test 1: Simple sequential FIFO
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

  // Test 2: Interleaved operations
  std::vector<int> all_pushed;
  std::vector<int> all_popped;

  for (int round = 0; round < 5; ++round) {
    // Push 3 values
    for (int i = 0; i < 3; ++i) {
      int value = round * 3 + i + 1; // 1,2,3, then 4,5,6, etc.
      ASSERT(q.push(value));
      all_pushed.push_back(value);
    }

    // Pop 2 values
    for (int i = 0; i < 2; ++i) {
      ASSERT(q.pop(v));
      all_popped.push_back(v);
    }
  }

  // Pop remaining values
  while (q.pop(v)) {
    all_popped.push_back(v);
  }

  // Verify FIFO order
  ASSERT_EQ(all_pushed.size(), all_popped.size());
  for (size_t i = 0; i < all_pushed.size(); ++i) {
    ASSERT_EQ(all_pushed[i], all_popped[i]);
  }

  ASSERT(q.empty());
  PRINT_INFO("FIFO order correctness test passed");
}

// 边界条件测试
void test_boundary_conditions() {
  PRINT_INFO("MPSC boundary conditions test");
  MPSCQueue<int, 16> q;

  // Test 1: Fill and empty repeatedly
  const size_t usable_capacity =
      q.capacity() - 1; // Account for implementation details

  for (int cycle = 0; cycle < 5; ++cycle) {
    // Fill to near capacity
    size_t pushed = 0;
    for (size_t i = 0; i < usable_capacity; ++i) {
      if (q.push(cycle * 1000 + i)) {
        ++pushed;
      } else {
        break; // Queue full
      }
    }

    ASSERT(pushed > 0); // Should be able to push at least something

    // Empty completely
    size_t popped = 0;
    int v;
    while (q.pop(v)) {
      ASSERT_EQ(v, cycle * 1000 + popped);
      ++popped;
    }

    ASSERT_EQ(pushed, popped);
    ASSERT(q.empty());
  }

  PRINT_INFO("Boundary conditions test passed");
}

// 数据完整性验证
void test_data_integrity() {
  PRINT_INFO("MPSC data integrity test");

  struct TestData {
    int id;
    char data[32];
    uint32_t checksum;

    TestData(int id = 0) : id(id) {
      snprintf(data, sizeof(data), "Data_%d", id);
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

  MPSCQueue<TestData, 32> q;

  // Push structured data
  std::vector<TestData> input_data;
  for (int i = 0; i < 15; ++i) {
    TestData td(i);
    ASSERT(td.is_valid());
    input_data.push_back(td);
    ASSERT(q.push(td));
  }

  // Pop and verify
  TestData output;
  for (size_t i = 0; i < input_data.size(); ++i) {
    ASSERT(q.pop(output));
    ASSERT(output.is_valid());
    ASSERT_EQ(output.id, input_data[i].id);
    ASSERT_EQ(strcmp(output.data, input_data[i].data), 0);
  }

  PRINT_INFO("Data integrity test passed");
}

void test_single_producer() {
  PRINT_INFO("MPSC single producer test");
  constexpr size_t N = 100000;
  MPSCQueue<int, 1024> q;
  std::atomic<bool> done{false};

  std::thread producer([&] {
    for (int i = 0; i < N; ++i) {
      while (!q.push(i)) {
        std::this_thread::yield();
      }
    }
    done = true;
  });

  std::thread consumer([&] {
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

  producer.join();
  consumer.join();
  PRINT_INFO("Single producer test passed");
}

void test_multi_producer_basic() {
  PRINT_INFO("MPSC multi-producer basic test");
  constexpr int num_producers = 4;
  constexpr int items_per_producer = 1000;
  constexpr int total_items = num_producers * items_per_producer;

  MPSCQueue<int, 1024> q;
  std::vector<std::thread> producers;
  std::unordered_set<int> received_values;
  std::mutex received_mutex;

  // Start producer threads
  for (int p = 0; p < num_producers; ++p) {
    producers.emplace_back([&, p]() {
      for (int i = 0; i < items_per_producer; ++i) {
        int value = p * items_per_producer + i;
        while (!q.push(value)) {
          std::this_thread::yield();
        }
      }
    });
  }

  // Consumer thread
  std::thread consumer([&]() {
    int value;
    int count = 0;
    while (count < total_items) {
      if (q.pop(value)) {
        std::lock_guard<std::mutex> lock(received_mutex);
        received_values.insert(value);
        ++count;
      } else {
        std::this_thread::yield();
      }
    }
  });

  // Wait for all threads
  for (auto &t : producers) {
    t.join();
  }
  consumer.join();

  // Verify results
  ASSERT_EQ(received_values.size(), total_items);

  // Check all values received correctly
  for (int i = 0; i < total_items; ++i) {
    ASSERT(received_values.count(i) == 1);
  }

  PRINT_INFO("Multi-producer basic test passed");
}

// 多生产者顺序验证
void test_multi_producer_ordering() {
  PRINT_INFO("MPSC multi-producer ordering test");
  constexpr int num_producers = 3;
  constexpr int items_per_producer = 1000;

  struct IntPair {
    int first;
    int second;

    IntPair() = default;
    IntPair(int f, int s) : first(f), second(s) {}
  };

  MPSCQueue<IntPair, 512> q; // pair<producer_id, sequence>
  std::vector<std::thread> producers;
  std::vector<std::vector<int>> producer_sequences(num_producers);
  std::mutex sequences_mutex;

  // Start producer threads
  for (int p = 0; p < num_producers; ++p) {
    producers.emplace_back([&, p]() {
      for (int i = 0; i < items_per_producer; ++i) {
        while (!q.push(IntPair(p, i))) {
          std::this_thread::yield();
        }
      }
    });
  }

  // Consumer thread
  std::thread consumer([&]() {
    IntPair value;
    int total_consumed = 0;

    while (total_consumed < num_producers * items_per_producer) {
      if (q.pop(value)) {
        std::lock_guard<std::mutex> lock(sequences_mutex);
        producer_sequences[value.first].push_back(value.second);
        ++total_consumed;
      } else {
        std::this_thread::yield();
      }
    }
  });

  // Wait for completion
  for (auto &t : producers) {
    t.join();
  }
  consumer.join();

  // Verify each producer's sequence is maintained
  for (int p = 0; p < num_producers; ++p) {
    ASSERT_EQ(producer_sequences[p].size(), items_per_producer);

    // Check if sequence is in order
    for (size_t i = 0; i < producer_sequences[p].size(); ++i) {
      ASSERT_EQ(producer_sequences[p][i], static_cast<int>(i));
    }
  }

  PRINT_INFO("Multi-producer ordering test passed");
}

void test_multi_producer_stress() {
  PRINT_INFO("MPSC multi-producer stress test");
  constexpr int num_producers = 4;
  constexpr int items_per_producer = 50000;
  constexpr int total_items = num_producers * items_per_producer;

  MPSCQueue<int, 1024> q;
  std::vector<std::thread> producers;
  std::atomic<int> total_produced{0};
  std::atomic<int> total_consumed{0};

  Timer timer;
  timer.reset();

  // Start producer threads
  for (int p = 0; p < num_producers; ++p) {
    producers.emplace_back([&, p]() {
      int local_produced = 0;
      for (int i = 0; i < items_per_producer; ++i) {
        int value = p * items_per_producer + i;

        // Limited retry to avoid deadlock
        int retry_count = 0;
        while (!q.push(value)) {
          std::this_thread::yield();
          if (++retry_count > 10000) {
            PRINT_ERROR("Producer {} timed out pushing {}", p, value);
            return;
          }
        }
        ++local_produced;
      }
      total_produced.fetch_add(local_produced);
    });
  }

  // Consumer thread
  std::thread consumer([&]() {
    int value;
    int local_consumed = 0;
    while (local_consumed < total_items) {
      if (q.pop(value)) {
        ++local_consumed;
      } else {
        std::this_thread::yield();
      }
    }
    total_consumed.store(local_consumed);
  });

  // Wait for producers
  for (auto &t : producers) {
    t.join();
  }

  consumer.join();

  auto elapsed = timer.elapsed_ms();

  // Verify results
  ASSERT_EQ(total_produced.load(), total_items);
  ASSERT_EQ(total_consumed.load(), total_items);

  PRINT_INFO("Stress test results:");
  PRINT_INFO("Producers: {}", num_producers);
  PRINT_INFO("Total items: {}", total_items);
  PRINT_INFO("Time: {}ms", elapsed);
  PRINT_INFO("Throughput: {} Mops/s", total_items / elapsed / 1000.0);

  PRINT_INFO("Multi-producer stress test passed");
}

// 竞争条件压力测试
void test_race_conditions_intensive() {
  PRINT_INFO("MPSC intensive race conditions test");
  constexpr int iterations = 500;
  constexpr int stress_multiplier = 5;

  for (int iter = 0; iter < iterations; ++iter) {
    MPSCQueue<int, 32> q;
    constexpr int num_producers = 4;
    constexpr int items_per_producer = 50 * stress_multiplier;

    std::vector<std::thread> producers;
    std::atomic<int> produced_count{0};
    std::atomic<int> consumed_count{0};
    std::atomic<bool> error_flag{false};

    // Start producers
    for (int p = 0; p < num_producers; ++p) {
      producers.emplace_back([&, p]() {
        int local_produced = 0;
        for (int i = 0; i < items_per_producer; ++i) {
          int value = p * items_per_producer + i;

          int retry = 0;
          while (!q.push(value)) {
            ++retry;
            if (retry % 1000 == 0) {
              std::this_thread::yield();
            }
          }

          ++local_produced;
        }
        produced_count.fetch_add(local_produced);
      });
    }

    // Consumer
    std::thread consumer([&]() {
      int value;
      int local_consumed = 0;
      const int target = num_producers * items_per_producer;

      while (local_consumed < target && !error_flag.load()) {
        if (q.pop(value)) {
          ++local_consumed;
        } else {
          std::this_thread::yield();
        }
      }
      consumed_count.store(local_consumed);
    });

    for (auto &t : producers) {
      t.join();
    }
    consumer.join();

    if (error_flag.load()) {
      PRINT_ERROR("Race condition test iteration {} failed due to timeout",
                  iter);
      break;
    }

    ASSERT_EQ(produced_count.load(), num_producers * items_per_producer);
    ASSERT_EQ(consumed_count.load(), num_producers * items_per_producer);

    if ((iter + 1) % 100 == 0) {
      PRINT_INFO("Completed {} iterations", iter + 1);
    }
  }

  PRINT_INFO("Intensive race conditions test passed ({} iterations)",
             iterations);
}

template <int num_producers>
void test_performance_mpsc_(MPSCQueue<int, 1024, num_producers> &q,
                            const size_t N) {
  std::vector<std::thread> producers;

  // Start producer threads
  for (int p = 0; p < num_producers; ++p) {
    producers.emplace_back([&, p]() {
      const size_t items_per_producer = N / num_producers;
      for (size_t i = 0; i < items_per_producer; ++i) {
        int value = p * items_per_producer + i;
        while (!q.push(value)) {
          std::this_thread::yield();
        }
      }
    });
  }

  // Consumer thread
  std::thread consumer([&]() {
    int value;
    size_t consumed = 0;
    while (consumed < N) {
      if (q.pop(value)) {
        ++consumed;
      } else {
        std::this_thread::yield();
      }
    }
  });

  // Wait for completion
  for (auto &t : producers) {
    t.join();
  }
  consumer.join();
}

void test_performance() {
  PRINT_INFO("MPSC performance test");
  constexpr size_t iter = 50;
  constexpr size_t N = 1'000'000;

  MPSCQueue<int, 1024, 1> q1;
  MPSCQueue<int, 1024, 2> q2;
  MPSCQueue<int, 1024, 4> q4;
  MPSCQueue<int, 1024, 8> q8;

  struct TestConfig {
    std::string name;
    std::function<void()> test_func;
  };

  std::vector<TestConfig> configs = {
      {"MPSC<1P>", [&]() { test_performance_mpsc_<1>(q1, N); }},
      {"MPSC<2P>", [&]() { test_performance_mpsc_<2>(q2, N); }},
      {"MPSC<4P>", [&]() { test_performance_mpsc_<4>(q4, N); }},
      {"MPSC<8P>", [&]() { test_performance_mpsc_<8>(q8, N); }}};

  for (const auto &config : configs) {
    PRINT_INFO("{} performance test", config.name);
    auto avg = Timer::measure(config.test_func, iter);
    double throughput = N * 2 / avg / 1; // Mops/s (push + pop)
    PRINT_INFO("{} throughput: {} Mops/s", config.name, throughput);
  }
}

void test_concurrent_enqueue_dequeue() {
  PRINT_INFO("MPSC concurrent enqueue/dequeue test");
  constexpr int num_producers = 6;
  constexpr int operations_per_producer = 100000; // 🔧 使用确定数量而非时间
  constexpr int total_expected = num_producers * operations_per_producer;

  MPSCQueue<int, 256, 6> q;
  std::atomic<int> total_produced{0};
  std::atomic<int> total_consumed{0};
  std::vector<std::thread> producers;

  Timer timer;
  timer.reset();

  // Start producer threads
  for (int p = 0; p < num_producers; ++p) {
    producers.emplace_back([&, p]() {
      int local_produced = 0;
      int value = p * 1000000;

      // 🔧 生产固定数量的数据
      for (int i = 0; i < operations_per_producer; ++i) {
        while (!q.push(value++)) {
          std::this_thread::yield();
        }
        ++local_produced;

        if (local_produced % 1000 == 0) {
          std::this_thread::yield();
        }
      }

      total_produced.fetch_add(local_produced);
    });
  }

  // Consumer thread
  std::thread consumer([&]() {
    int value;
    int local_consumed = 0;

    // 🔧 消费固定数量的数据
    while (local_consumed < total_expected) {
      if (q.pop(value)) {
        ++local_consumed;
      } else {
        std::this_thread::yield();
      }
    }

    total_consumed.store(local_consumed);
  });

  // Wait for completion
  for (auto &t : producers) {
    t.join();
  }
  consumer.join();

  auto elapsed = timer.elapsed_ms();

  PRINT_INFO("Concurrent test results:");
  PRINT_INFO("Runtime: {}ms", elapsed);
  PRINT_INFO("Producers: {}", num_producers);
  PRINT_INFO("Operations per producer: {}", operations_per_producer);
  PRINT_INFO("Total produced: {}", total_produced.load());
  PRINT_INFO("Total consumed: {}", total_consumed.load());
  PRINT_INFO("Production rate: {} ops/ms",
             total_produced.load() / double(elapsed));

  // 🔧 严格验证
  ASSERT_EQ(total_produced.load(), total_expected);
  ASSERT_EQ(total_consumed.load(), total_expected);

  PRINT_INFO("Concurrent enqueue/dequeue test passed");
}

void test_race_conditions() {
  PRINT_INFO("MPSC race condition test");
  constexpr int iterations = 1000;

  for (int iter = 0; iter < iterations; ++iter) {
    MPSCQueue<int, 16, 3> q;
    constexpr int num_producers = 3;
    constexpr int items_per_producer = 10;

    std::vector<std::thread> producers;
    std::atomic<int> produced_count{0};
    std::atomic<int> consumed_count{0};

    // Start producers
    for (int p = 0; p < num_producers; ++p) {
      producers.emplace_back([&, p]() {
        for (int i = 0; i < items_per_producer; ++i) {
          int value = p * items_per_producer + i;
          while (!q.push(value)) {
            std::this_thread::yield();
          }
          produced_count.fetch_add(1);
        }
      });
    }

    // Consumer
    std::thread consumer([&]() {
      int value;
      while (consumed_count < num_producers * items_per_producer) {
        if (q.pop(value)) {
          consumed_count.fetch_add(1);
        } else {
          std::this_thread::yield();
        }
      }
    });

    for (auto &t : producers) {
      t.join();
    }
    consumer.join();

    ASSERT_EQ(produced_count.load(), num_producers * items_per_producer);
    ASSERT_EQ(consumed_count.load(), num_producers * items_per_producer);
  }

  PRINT_INFO("Race condition test passed ({} iterations)", iterations);
}

void test_different_types() {
  PRINT_INFO("MPSC different types test");

  // Test struct
  struct TestData {
    int id;
    double value;
    bool operator==(const TestData &other) const {
      return id == other.id && value == other.value;
    }
  };

  MPSCQueue<TestData, 16> q;

  TestData input{42, 3.14};
  ASSERT(q.push(input));

  TestData output;
  ASSERT(q.pop(output));
  ASSERT(output == input);

  // Test Writer/Reader semantics
  ASSERT(q.push([](TestData *ptr) {
    ptr->id = 100;
    ptr->value = 2.71;
  }));

  ASSERT(q.pop([](const TestData *ptr) {
    ASSERT_EQ(ptr->id, 100);
    ASSERT_NEAR(ptr->value, 2.71, 0.001);
  }));

  PRINT_INFO("Different types test passed");
}

// 批量操作测试（如果实现了的话）
void test_bulk_operations() {
  PRINT_INFO("MPSC bulk operations test");

  // 这个测试假设MPSCQueue支持批量操作
  // 如果不支持，可以跳过或注释掉
  /*
  MPSCQueue<int, 64> q;

  // Test bulk push if available
  std::vector<int> input_data = {1, 2, 3, 4, 5};
  // size_t pushed = q.push_bulk(input_data.data(), input_data.size());
  // ASSERT_EQ(pushed, input_data.size());

  // Test bulk pop if available
  std::vector<int> output_data(10);
  // size_t popped = q.pop_bulk(output_data.data(), output_data.size());
  // ASSERT_EQ(popped, input_data.size());
  */

  PRINT_INFO("Bulk operations test skipped (not implemented)");
}

int main() {
  try {
    PRINT_INFO("Starting MPSC queue test suite");

    test_single_thread();
    test_writer_reader_semantics();
    test_fifo_correctness();
    test_boundary_conditions();
    test_data_integrity();

    test_single_producer();
    test_multi_producer_basic();
    test_multi_producer_ordering();
    test_multi_producer_stress();
    test_race_conditions();
    test_race_conditions_intensive();
    test_concurrent_enqueue_dequeue();

    test_different_types();
    test_bulk_operations();

    test_performance();

    PRINT_INFO("All MPSC tests completed successfully");
    return 0;
  } catch (const std::exception &e) {
    PRINT_ERROR("Test failed: {}", e.what());
    return 1;
  }
}