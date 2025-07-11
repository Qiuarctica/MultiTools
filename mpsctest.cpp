#include "MPSC.h"
#include "test_suit.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

using namespace stest;

// 启用断言
#define STEST_ENABLE_ASSERT 1

void test_single_thread() {
  PRINT_INFO("MPSC 单线程功能测试");
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
  ASSERT(!q.pop(v)); // 空队列

  // 填满队列 (capacity = 7)
  for (int i = 0; i < 7; ++i) {
    ASSERT(q.push(i));
  }
  ASSERT(!q.push(100)); // 队列已满
  ASSERT_EQ(q.size(), 7u);

  // 全部弹出
  for (int i = 0; i < 7; ++i) {
    ASSERT(q.pop(v));
    ASSERT_EQ(v, i);
  }

  ASSERT(!q.pop(v));
  ASSERT(q.empty());
  PRINT_INFO("✅ MPSC 单线程功能测试通过");
}

void test_writer_reader_semantics() {
  PRINT_INFO("MPSC Writer/Reader 语义测试");
  MPSCQueue<int, 8> q;

  // Writer 语义测试
  ASSERT(q.push([](int *ptr) { *ptr = 42; }));
  ASSERT(q.push([](int *ptr) { *ptr = 100; }));

  // Reader 语义测试
  int sum = 0;
  ASSERT(q.pop([&sum](const int *ptr) { sum += *ptr; }));
  ASSERT(q.pop([&sum](const int *ptr) { sum += *ptr; }));

  ASSERT_EQ(sum, 142); // 42 + 100
  ASSERT(q.empty());

  PRINT_INFO("✅ MPSC Writer/Reader 语义测试通过");
}

void test_single_producer() {
  PRINT_INFO("MPSC 单生产者测试");
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
  PRINT_INFO("✅ MPSC 单生产者测试通过");
}

void test_multi_producer_basic() {
  PRINT_INFO("MPSC 多生产者基础测试");
  constexpr int num_producers = 4;
  constexpr int items_per_producer = 1000;
  constexpr int total_items = num_producers * items_per_producer;

  MPSCQueue<int, 1024> q;
  std::vector<std::thread> producers;
  std::unordered_set<int> received_values;
  std::mutex received_mutex;

  // 启动生产者线程
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

  // 消费者线程
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

  // 等待所有线程完成
  for (auto &t : producers) {
    t.join();
  }
  consumer.join();

  // 验证结果
  ASSERT_EQ(received_values.size(), total_items);

  // 检查所有值都被正确接收
  for (int i = 0; i < total_items; ++i) {
    ASSERT(received_values.count(i) == 1);
  }

  PRINT_INFO("✅ MPSC 多生产者基础测试通过");
}

void test_multi_producer_stress() {
  PRINT_INFO("MPSC 多生产者压力测试");
  constexpr int num_producers = 4;
  constexpr int items_per_producer = 50000;
  constexpr int total_items = num_producers * items_per_producer;

  MPSCQueue<int, 1024> q;
  std::vector<std::thread> producers;
  std::atomic<int> total_produced{0};
  std::atomic<int> total_consumed{0};

  Timer timer;
  timer.reset();

  // 启动生产者线程
  for (int p = 0; p < num_producers; ++p) {
    producers.emplace_back([&, p]() {
      int local_produced = 0;
      for (int i = 0; i < items_per_producer; ++i) {
        int value = p * items_per_producer + i;

        // 有限重试避免死锁
        int retry_count = 0;
        while (!q.push(value)) {
          std::this_thread::yield();
          if (++retry_count > 10000) {
            PRINT_ERROR("生产者 {} 在推送 {} 时超时", p, value);
            return;
          }
        }
        ++local_produced;
      }
      total_produced.fetch_add(local_produced);
    });
  }

  // 消费者线程
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

  // 等待生产者完成
  for (auto &t : producers) {
    t.join();
  }

  consumer.join();

  auto elapsed = timer.elapsed_ms();

  // 验证结果
  ASSERT_EQ(total_produced.load(), total_items);
  ASSERT_EQ(total_consumed.load(), total_items);

  PRINT_INFO("压力测试结果:");
  PRINT_INFO("生产者数量: {}", num_producers);
  PRINT_INFO("总项目数: {}", total_items);
  PRINT_INFO("耗时: {}ms", elapsed);
  PRINT_INFO("生产速率: {} Mops/s", total_items / elapsed / 1000.0);

  PRINT_INFO("✅ MPSC 多生产者压力测试通过");
}

template <int num_producers>
void test_performance_mpsc_(MPSCQueue<int, 1024, num_producers> &q,
                            const size_t N) {
  std::vector<std::thread> producers;

  // 启动生产者线程
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

  // 消费者线程
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

  // 等待完成
  for (auto &t : producers) {
    t.join();
  }
  consumer.join();
}

void test_performance() {
  PRINT_INFO("MPSC 性能测试");
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
      {"MPSC<1生产者>", [&]() { test_performance_mpsc_<1>(q1, N); }},
      {"MPSC<2生产者>", [&]() { test_performance_mpsc_<2>(q2, N); }},
      {"MPSC<4生产者>", [&]() { test_performance_mpsc_<4>(q4, N); }},
      {"MPSC<8生产者>", [&]() { test_performance_mpsc_<8>(q8, N); }}};

  for (const auto &config : configs) {
    PRINT_INFO("{} 性能测试", config.name);
    auto avg = Timer::measure(config.test_func, iter);
    double throughput = N * 2 / avg / 1; // Mops/s (push + pop)
    PRINT_INFO("{} 吞吐: {} Mops/s", config.name, throughput);
  }
}

void test_concurrent_enqueue_dequeue() {
  PRINT_INFO("MPSC 并发入队出队测试");
  constexpr int num_producers = 6;
  constexpr int test_duration_ms = 5000;

  MPSCQueue<int, 256, 6> q;
  std::atomic<bool> stop_flag{false};
  std::atomic<int> total_produced{0};
  std::atomic<int> total_consumed{0};
  std::vector<std::thread> producers;

  // 启动生产者线程
  for (int p = 0; p < num_producers; ++p) {
    producers.emplace_back([&, p]() {
      int local_produced = 0;
      int value = p * 1000000;
      while (!stop_flag.load()) {
        if (q.push(value++)) {
          ++local_produced;
        }
        if (local_produced % 100 == 0) {
          std::this_thread::yield();
        }
      }
      total_produced.fetch_add(local_produced);
    });
  }

  // 消费者线程
  std::thread consumer([&]() {
    int value;
    int local_consumed = 0;
    while (!stop_flag.load()) {
      if (q.pop(value)) {
        ++local_consumed;
      } else {
        std::this_thread::yield();
      }
    }

    // 处理剩余数据
    while (q.pop(value)) {
      ++local_consumed;
    }

    total_consumed.store(local_consumed);
  });

  // 运行指定时间
  std::this_thread::sleep_for(std::chrono::milliseconds(test_duration_ms));
  stop_flag.store(true);

  // 等待完成
  for (auto &t : producers) {
    t.join();
  }
  consumer.join();

  PRINT_INFO("并发测试结果:");
  PRINT_INFO("运行时间: {}ms", test_duration_ms);
  PRINT_INFO("生产者数量: {}", num_producers);
  PRINT_INFO("总生产: {}", total_produced.load());
  PRINT_INFO("总消费: {}", total_consumed.load());
  PRINT_INFO("生产速率: {} ops/ms",
             total_produced.load() / double(test_duration_ms));

  // 验证数据一致性
  ASSERT_EQ(total_produced.load(), total_consumed.load());

  PRINT_INFO("✅ MPSC 并发入队出队测试通过");
}

void test_race_conditions() {
  PRINT_INFO("MPSC 竞争条件测试");
  constexpr int iterations = 1000;

  for (int iter = 0; iter < iterations; ++iter) {
    MPSCQueue<int, 16, 3> q;
    constexpr int num_producers = 3;
    constexpr int items_per_producer = 10;

    std::vector<std::thread> producers;
    std::atomic<int> produced_count{0};
    std::atomic<int> consumed_count{0};

    // 启动生产者
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

    // 消费者
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

  PRINT_INFO("✅ MPSC 竞争条件测试通过 ({} 次迭代)", iterations);
}

void test_different_types() {
  PRINT_INFO("MPSC 不同类型测试");

  // 测试 struct
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

  // 测试 Writer/Reader 语义
  ASSERT(q.push([](TestData *ptr) {
    ptr->id = 100;
    ptr->value = 2.71;
  }));

  ASSERT(q.pop([](const TestData *ptr) {
    ASSERT_EQ(ptr->id, 100);
    ASSERT_NEAR(ptr->value, 2.71, 0.001);
  }));

  PRINT_INFO("✅ MPSC 不同类型测试通过");
}

int main() {
  try {
    PRINT_INFO("🚀 开始 MPSC 队列测试套件");

    test_single_thread();
    test_writer_reader_semantics();
    test_single_producer();
    test_multi_producer_basic();
    test_multi_producer_stress();
    test_performance();
    test_concurrent_enqueue_dequeue();
    test_race_conditions();
    test_different_types();

    PRINT_INFO("🎉 所有 MPSC 测试完成！");
    return 0;
  } catch (const std::exception &e) {
    PRINT_ERROR("测试失败: {}", e.what());
    return 1;
  }
}