#include "seq_mpsc.h"
#include "../utils/test_suit.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <ratio>
#include <thread>
#include <vector>
void test_basic_in_order() {
  PRINT_INFO("=== 基本顺序测试 ===");

  MPSCQueue<int, 16> queue;

  // 按顺序写入
  for (uint64_t i = 0; i < 10; ++i) {
    queue.push(static_cast<int>(i * 100), i);
  }

  // 按顺序读取
  int value;
  for (uint64_t i = 0; i < 10; ++i) {
    if (queue.pop(value)) {
      std::cout << "Read seq " << i << ": " << value << std::endl;
      if (value != static_cast<int>(i * 100)) {
        PRINT_ERROR("数据不匹配！期望: " + std::to_string(i * 100) +
                    ", 实际: " + std::to_string(value));
      }
    } else {
      PRINT_ERROR("读取seq " + std::to_string(i) + " 失败");
    }
  }

  PRINT_INFO("基本顺序测试完成\n");
}

void test_out_of_order_write() {
  PRINT_INFO("=== 乱序写入测试 ===");

  MPSCQueue<std::string, 16> queue;

  // 乱序写入序列：{0, 2, 1, 4, 3, 6, 5, 8, 7, 9}
  std::vector<uint64_t> write_order = {0, 2, 1, 4, 3, 6, 5, 8, 7, 9};

  std::thread writer([&queue, &write_order]() {
    for (auto seq : write_order) {
      std::string data = "Data_" + std::to_string(seq);
      queue.push(data, seq);
      std::cout << "Wrote seq " << seq << ": " << data << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  });

  std::thread reader([&queue]() {
    std::string data;
    for (uint64_t expected_seq = 0; expected_seq < 10; ++expected_seq) {
      queue.pop(data);
      std::cout << "Read in order seq " << expected_seq << ": " << data
                << std::endl;

      // 验证数据正确性
      std::string expected_data = "Data_" + std::to_string(expected_seq);
      if (data != expected_data) {
        PRINT_ERROR("数据不匹配！期望: " + expected_data + ", 实际: " + data);
      }
    }
  });

  writer.join();
  reader.join();

  PRINT_INFO("乱序写入测试完成\n");
}

void test_multiple_producers() {
  PRINT_INFO("=== 多生产者测试 ===");

  MPSCQueue<int, 64> queue;
  constexpr int NUM_PRODUCERS = 4;
  constexpr int ITEMS_PER_PRODUCER = 10;
  constexpr int TOTAL_ITEMS = NUM_PRODUCERS * ITEMS_PER_PRODUCER;

  std::vector<std::thread> producers;

  // 启动多个生产者
  for (int p = 0; p < NUM_PRODUCERS; ++p) {
    producers.emplace_back([&queue, p]() {
      // 每个生产者负责不同的序列号范围
      for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
        uint64_t seq = p * ITEMS_PER_PRODUCER + i;
        int value = static_cast<int>(seq * 1000 + p);
        queue.push(value, seq);
        std::cout << "Producer " << p << " wrote seq " << seq << ": " << value
                  << std::endl;

        // 随机延迟制造竞争
        std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 20));
      }
    });
  }

  // 单个消费者
  std::thread consumer([&queue]() {
    int value;
    for (int i = 0; i < TOTAL_ITEMS; ++i) {
      queue.pop(value);

      // 验证数据
      int expected_producer = i / ITEMS_PER_PRODUCER;
      int expected_value = i * 1000 + expected_producer;

      std::cout << "Consumer read seq " << i << ": " << value << std::endl;

      if (value != expected_value) {
        PRINT_ERROR("数据验证失败！seq: " + std::to_string(i) +
                    ", 期望: " + std::to_string(expected_value) +
                    ", 实际: " + std::to_string(value));
      }
    }
  });

  for (auto &t : producers) {
    t.join();
  }
  consumer.join();

  PRINT_INFO("多生产者测试完成\n");
}

void test_wrap_around() {
  PRINT_INFO("=== 环绕测试 ===");

  MPSCQueue<uint64_t, 8> queue; // 小容量测试环绕

  // 写入超过容量的数据
  constexpr int NUM_ITEMS = 20;

  std::thread producer([&queue]() {
    for (uint64_t i = 0; i < NUM_ITEMS; ++i) {
      queue.push(i * 111, i);
      std::cout << "Produced seq " << i << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  });

  std::thread consumer([&queue]() {
    uint64_t value;
    for (uint64_t i = 0; i < NUM_ITEMS; ++i) {
      queue.pop(value);
      std::cout << "Consumed seq " << i << ": " << value << std::endl;

      if (value != i * 111) {
        PRINT_ERROR("环绕测试数据错误！seq: " + std::to_string(i));
      }

      // 偶尔延迟以测试生产者等待
      if (i % 3 == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
      }
    }
  });

  producer.join();
  consumer.join();

  PRINT_INFO("环绕测试完成\n");
}

void test_performance() {
  PRINT_INFO("=== 性能测试 ===");

  MPSCQueue<uint64_t, 1024> queue;
  constexpr int NUM_ITEMS = 100000;

  auto start_time = std::chrono::high_resolution_clock::now();

  std::thread producer([&queue]() {
    for (uint64_t i = 0; i < NUM_ITEMS; ++i) {
      queue.push(i, i);
    }
  });

  std::thread consumer([&queue]() {
    uint64_t value;
    for (uint64_t i = 0; i < NUM_ITEMS; ++i) {
      queue.pop(value);
      if (value != i) {
        PRINT_ERROR("性能测试数据错误！");
        break;
      }
    }
  });

  producer.join();
  consumer.join();

  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration<double>(end_time - start_time);

  double throughput = NUM_ITEMS / duration.count();
  PRINT_INFO("性能测试完成");
  PRINT_INFO("处理 " + std::to_string(NUM_ITEMS) + " 项数据");
  PRINT_INFO("耗时: " + std::to_string(duration.count()) + " 秒");
  PRINT_INFO("吞吐量: " + std::to_string(throughput) + " 项/秒");
}

void test_debug_interface() {
  PRINT_INFO("=== 调试接口测试 ===");

  MPSCQueue<int, 8> queue;

  std::cout << "初始状态:" << std::endl;
  queue.debug_print();

  // 写入一些数据
  queue.push(100, 0);
  queue.push(300, 2); // 跳过seq 1

  std::cout << "\n写入seq 0,2后:" << std::endl;
  queue.debug_print();

  // 尝试读取
  int value;
  if (queue.pop(value)) {
    std::cout << "\n读取到seq 0: " << value << std::endl;
  }

  std::cout << "读取seq 0后:" << std::endl;
  queue.debug_print();

  // 现在写入seq 1
  queue.push(200, 1);
  std::cout << "\n写入seq 1后:" << std::endl;
  queue.debug_print();

  // 应该能读取seq 1
  if (queue.pop(value)) {
    std::cout << "读取到seq 1: " << value << std::endl;
  }

  // 应该能读取seq 2
  if (queue.pop(value)) {
    std::cout << "读取到seq 2: " << value << std::endl;
  }

  std::cout << "\n全部读取后:" << std::endl;
  queue.debug_print();

  PRINT_INFO("调试接口测试完成\n");
}

template <int QueueSize>
void run_perf_test(int num_producers, int items_per_producer) {
  MPSCQueue<uint64_t, QueueSize> queue;
  int total_items = num_producers * items_per_producer;

  std::atomic<bool> start_flag{false};
  std::atomic<int> ready_count{0};
  std::vector<std::thread> producers;
  std::vector<std::vector<uint64_t>> producer_latencies(num_producers);
  auto overall_start = std::chrono::high_resolution_clock::now();

  // 启动生产者
  for (int p = 0; p < num_producers; ++p) {
    producers.emplace_back([&, p]() {
      ready_count.fetch_add(1);
      while (!start_flag.load()) {
        std::this_thread::yield();
      }
      for (int i = 0; i < items_per_producer; ++i) {
        uint64_t seq = p * items_per_producer + i;
        auto start_time = std::chrono::high_resolution_clock::now();
        queue.push(seq, seq);
        auto end_time = std::chrono::high_resolution_clock::now();
        producer_latencies[p].push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end_time -
                                                                 start_time)
                .count());
      }
    });
  }

  // 启动消费者
  std::vector<uint64_t> consumer_latencies;
  std::atomic<bool> consumer_done{false};
  std::thread consumer([&]() {
    uint64_t value;
    int error_count = 0;

    for (int i = 0; i < total_items; ++i) {
      auto start_time = std::chrono::high_resolution_clock::now();
      queue.pop(value);
      auto end_time = std::chrono::high_resolution_clock::now();
      consumer_latencies.push_back(
          std::chrono::duration_cast<std::chrono::nanoseconds>(end_time -
                                                               start_time)
              .count());
      ASSERT(value == i);
    }

    if (error_count > 0) {
      std::cout << "发现 " << error_count << " 个数据错误" << std::endl;
    }
    consumer_done.store(true);
  });

  // 开始测试
  start_flag.store(true);

  // 等待完成
  for (auto &t : producers) {
    t.join();
  }
  consumer.join();

  auto overall_end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration<double>(overall_end - overall_start);

  double throughput = total_items / duration.count();
  double latency_us = (duration.count() * 1000000.0) / total_items;

  std::cout << "  队列大小: " << QueueSize << std::endl;
  std::cout << "  生产者数: " << num_producers << std::endl;
  std::cout << "  总数据量: " << total_items << std::endl;
  std::cout << "  总耗时: " << std::fixed << std::setprecision(3)
            << duration.count() << "s" << std::endl;
  std::cout << "  吞吐量: " << std::fixed << std::setprecision(0) << throughput
            << " ops/s" << std::endl;
  if (!consumer_latencies.empty()) {
    double sum = 0;
    for (const auto &lat : consumer_latencies) {
      sum += lat;
    }
    double avg_lat = sum / consumer_latencies.size();
    PRINT_INFO("Consumer - Avg: {}ns", static_cast<int64_t>(avg_lat));
  } // 打印local_latencies的平均值
  for (auto latency : producer_latencies) {
    if (!latency.empty()) {
      double sum = 0;
      for (const auto &lat : latency) {
        sum += lat;
      }
      double avg_lat = sum / latency.size();
      PRINT_INFO("Producer - Avg: {}ns", static_cast<int64_t>(avg_lat));
    }
  }
}

void test_performance_comprehensive() {
  PRINT_INFO("=== 综合性能测试 ===");

  struct TestConfig {
    int queue_size;
    int num_producers;
    int items_per_producer;
    std::string name;
  };

  std::vector<TestConfig> configs = {
      {64, 1, 50000, "单生产者-小队列"},  {1024, 1, 50000, "单生产者-大队列"},
      {64, 2, 25000, "双生产者-小队列"},  {1024, 2, 25000, "双生产者-大队列"},
      {128, 4, 12500, "四生产者-中队列"}, {1024, 8, 25000, "八生产者-大队列"}};

  for (const auto &config : configs) {
    PRINT_INFO("测试配置: " + config.name);

    if (config.queue_size == 64) {
      run_perf_test<64>(config.num_producers, config.items_per_producer);
    } else if (config.queue_size == 128) {
      run_perf_test<128>(config.num_producers, config.items_per_producer);
    } else {
      run_perf_test<1024>(config.num_producers, config.items_per_producer);
    }

    std::cout << std::endl;
  }

  PRINT_INFO("综合性能测试完成\n");
}

int main() {
  try {
    test_basic_in_order();
    test_out_of_order_write();
    test_multiple_producers();
    test_wrap_around();
    test_debug_interface();
    test_performance();
    test_performance_comprehensive(); // 新增

    PRINT_INFO("所有测试通过！");
  } catch (const std::exception &e) {
    PRINT_ERROR("测试异常: " + std::string(e.what()));
    return -1;
  }

  return 0;
}
