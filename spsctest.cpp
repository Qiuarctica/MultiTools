#include "spsc.h"
#include "test_suit.h"
#include <thread>

using namespace stest;

void test_single_thread() {
  PRINT_INFO("单线程功能测试");
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
  ASSERT(!q.pop(v)); // 空

  // 填满
  for (int i = 0; i < 7; ++i)
    ASSERT(q.push(i));
  ASSERT(!q.push(100)); // 满
  ASSERT_EQ(q.size(), 7u);

  // 全部弹出
  for (int i = 0; i < 7; ++i) {
    ASSERT(q.pop(v));
    ASSERT_EQ(v, i);
  }

  ASSERT(!q.pop(v));
  ASSERT(q.empty());
  PRINT_INFO("✅ 单线程功能测试通过");
}

void test_bulk() {
  PRINT_INFO("批量 push_bulk 测试");
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
  ASSERT(!q.pop(v)); // 队列已空
  PRINT_INFO("✅ 批量 push_bulk 测试通过");
}

void test_cache_align_switch() {
  PRINT_INFO("缓存/对齐开关测试");
  int v;

  // 关闭缓存
  SPSCQueue<int, 8, false, true> q1;
  ASSERT_MSG(q1.push(1), "缓存关闭队列 push 失败");
  ASSERT_MSG(q1.pop(v) && v == 1, "缓存关闭队列 pop 失败");

  // 关闭对齐
  SPSCQueue<int, 8, true, false> q2;
  ASSERT_MSG(q2.push(2), "对齐关闭队列 push 失败");
  ASSERT_MSG(q2.pop(v) && v == 2, "对齐关闭队列 pop 失败");

  // 全部关闭
  SPSCQueue<int, 8, false, false> q3;
  ASSERT_MSG(q3.push(3), "缓存和对齐关闭队列 push 失败");
  ASSERT_MSG(q3.pop(v) && v == 3, "缓存和对齐关闭队列 pop 失败");

  PRINT_INFO("✅ 缓存/对齐开关测试通过");
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
  PRINT_INFO("SPSC 多线程正确性测试");

  Timer timer;
  timer.reset();
  test_multithread_spsc_<true, true>();
  PRINT_INFO("Cache=ON, Align=ON 耗时: {}ms", timer.elapsed_ms());

  timer.reset();
  test_multithread_spsc_<true, false>();
  PRINT_INFO("Cache=ON, Align=OFF 耗时: {}ms", timer.elapsed_ms());

  timer.reset();
  test_multithread_spsc_<false, true>();
  PRINT_INFO("Cache=OFF, Align=ON 耗时: {}ms", timer.elapsed_ms());

  timer.reset();
  test_multithread_spsc_<false, false>();
  PRINT_INFO("Cache=OFF, Align=OFF 耗时: {}ms", timer.elapsed_ms());

  PRINT_INFO("✅ SPSC 多线程测试通过");
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

void test_performance() {
  PRINT_INFO("性能测试（单线程循环）");
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

  std::vector<TestConfig> configs = {
      {"SPSCQueue<Cache,Align>", [&]() { test_performance_spsc_(q, N); }},
      {"SPSCQueue<Cache,NoAlign>",
       [&]() { test_performance_spsc_(q_no_align, N); }},
      {"SPSCQueue<NoCache,Align>",
       [&]() { test_performance_spsc_(q_no_cache, N); }},
      {"SPSCQueue<NoCache,NoAlign>",
       [&]() { test_performance_spsc_(q_no_cache_no_align, N); }}};

  for (const auto &config : configs) {
    PRINT_INFO("{} 性能测试", config.name);
    auto avg = Timer::measure(config.test_func, iter);
    double throughput = N * 2 / avg / 1; // Mops/s
    PRINT_INFO("{} 吞吐: {} Mops/s", config.name, throughput);
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

template <size_t cap = 1024, size_t N = 10'000'000, size_t iter = 100>
void test_performance_mt() {
  PRINT_INFO("SPSC多线程性能测试 - cap={}, N={}, iter={}", cap, N, iter);

  struct MTTestConfig {
    std::string name;
    std::function<void()> test_func;
  };

  SPSCQueue<int, cap> q;
  SPSCQueue<int, cap, true, false> q_no_align;
  SPSCQueue<int, cap, false, true> q_no_cache;
  SPSCQueue<int, cap, false, false> q_nocache_no_align;

  std::vector<MTTestConfig> configs = {
      {"SPSCQueue<Cache,Align>", [&]() { test_performance_mt_(q, N); }},
      {"SPSCQueue<Cache,NoAlign>",
       [&]() { test_performance_mt_(q_no_align, N); }},
      {"SPSCQueue<NoCache,Align>",
       [&]() { test_performance_mt_(q_no_cache, N); }},
      {"SPSCQueue<NoCache,NoAlign>",
       [&]() { test_performance_mt_(q_nocache_no_align, N); }}};

  for (const auto &config : configs) {
    auto avg = Timer::measure(config.test_func, iter);
    double throughput = N * 2 / avg / 1; // Mops/s
    PRINT_INFO("{} 吞吐: {} Mops/s", config.name, throughput);
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
        std::this_thread::yield(); // 队列空时让出CPU
    }
  });

  prod.join();
  cons.join();
}

template <size_t cap = 1024, size_t N = 10'000'000, size_t BATCH = 32,
          size_t iter = 100>
void test_bulk_multithread() {
  PRINT_INFO("SPSC批量多线程测试 - cap={}, N={}, BATCH={}, iter={}", cap, N,
             BATCH, iter);

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
    PRINT_INFO("{} 批量吞吐: {} Mops/s", config.name, throughput);
  }
}

int main() {
  try {
    PRINT_INFO("🚀 开始 SPSC 队列测试套件");

    test_single_thread();
    test_bulk();
    test_cache_align_switch();
    test_multithread_spsc();
    test_performance();
    test_performance_mt();
    test_bulk_multithread<1024, 10'000'000, 32, 100>();
    test_bulk_multithread<1024, 10'000'000, 64, 100>();

    PRINT_INFO("🎉 所有测试完成！");
    return 0;
  } catch (const std::exception &e) {
    PRINT_ERROR("测试失败: {}", e.what());
    return 1;
  }
}