#include "spsc.h"
#include "test.h"
#include <random>
#include <thread>
#include <vector>

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
  PRINT_SUCCESS("单线程功能测试通过");
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
  PRINT_SUCCESS("批量 push_bulk 测试通过");
}

void test_cache_align_switch() {
  PRINT_INFO("缓存/对齐开关测试");
  // 关闭缓存
  SPSCQueue<int, 8, false, true> q1;
  int v;
  ASSERT(q1.push(1));
  ASSERT(q1.pop(v) && v == 1);

  // 关闭对齐
  SPSCQueue<int, 8, true, false> q2;
  ASSERT(q2.push(2));
  ASSERT(q2.pop(v) && v == 2);

  // 全部关闭
  SPSCQueue<int, 8, false, false> q3;
  ASSERT(q3.push(3));
  ASSERT(q3.pop(v) && v == 3);

  PRINT_SUCCESS("缓存/对齐开关测试通过");
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
  test_multithread_spsc_<true, true>();
  test_multithread_spsc_<true, false>();
  test_multithread_spsc_<false, true>();
  test_multithread_spsc_<false, false>();
  PRINT_SUCCESS("SPSC 多线程测试通过");
}

template <Queue Q> void test_performance_spsc_(Q &q, const size_t N) {
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
  constexpr size_t iter = 10;
  constexpr size_t N = 10'000'000;

  PRINT_INFO("SPSCQueue<Cache,Align> 性能测试");
  SPSCQueue<int, 1024> q;
  auto avg = Timer::measure([&]() { test_performance_spsc_(q, N); }, iter);
  std::cout << "SPSCQueue<Cache,Align> 吞吐: " << (N * 2 / avg / 1)
            << " Mops/s\n";

  PRINT_INFO("SPSCQueue<Cache,NoAlign> 性能测试");
  SPSCQueue<int, 1024, true, false> q_no_align;
  avg = Timer::measure([&]() { test_performance_spsc_(q_no_align, N); }, iter);
  std::cout << "SPSCQueue<Cache,NoAlign> 吞吐: " << (N * 2 / avg / 1)
            << " Mops/s\n";

  PRINT_INFO("SPSCQueue<NoCache,Align> 性能测试");
  SPSCQueue<int, 1024, false, true> q_no_cache;
  avg = Timer::measure([&]() { test_performance_spsc_(q_no_cache, N); }, iter);
  std::cout << "SPSCQueue<NoCache,Align> 吞吐: " << (N * 2 / avg / 1)
            << " Mops/s\n";

  PRINT_INFO("SPSCQueue<NoCache,NoAlign> 性能测试");
  SPSCQueue<int, 1024, false, false> q_no_cache_no_align;
  avg = Timer::measure(
      [&]() { test_performance_spsc_(q_no_cache_no_align, N); }, iter);
  std::cout << "SPSCQueue<NoCache,NoAlign> 吞吐: " << (N * 2 / avg / 1)
            << " Mops/s\n";

  PRINT_INFO("Original线程安全队列性能测试");
  OriginMultipleSafeQueue<int> origin_q;
  avg = Timer::measure([&]() { test_performance_spsc_(origin_q, N); }, iter);
  std::cout << "Original线程安全队列 吞吐: " << (N * 2 / avg / 1)
            << " Mops/s\n";
  PRINT_SUCCESS("性能测试通过");
}

template <Queue Q> void test_performance_mt_(Q &q, const size_t N) {
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

void test_performance_mt() {
  PRINT_INFO("性能测试（SPSC 多线程）");
  constexpr size_t N = 10'000'000;
  constexpr size_t iter = 10;
  SPSCQueue<int, 1024> q;
  auto avg = Timer::measure([&]() { test_performance_mt_(q, N); }, iter);
  std::cout << "SPSC多线程 " << N << " 次，耗时: " << avg
            << " ms, 吞吐: " << (N * 2 / avg / 1) << " Mops/s\n";
  SPSCQueue<int, 1024, true, false> q_no_align;
  avg = Timer::measure([&]() { test_performance_mt_(q_no_align, N); }, iter);
  std::cout << "SPSC多线程(无对齐) " << N << " 次，耗时: " << avg
            << " ms, 吞吐: " << (N * 2 / avg / 1) << " Mops/s\n";
  SPSCQueue<int, 1024, false, true> q_no_cache;
  avg = Timer::measure([&]() { test_performance_mt_(q_no_cache, N); }, iter);
  std::cout << "SPSC多线程(无缓存) " << N << " 次，耗时: " << avg
            << " ms, 吞吐: " << (N * 2 / avg / 1) << " Mops/s\n";
  SPSCQueue<int, 1024, false, false> q_nocache_no_align;
  avg = Timer::measure([&]() { test_performance_mt_(q_nocache_no_align, N); },
                       iter);
  std::cout << "SPSC多线程(无缓存无对齐) " << N << " 次，耗时: " << avg
            << " ms, 吞吐: " << (N * 2 / avg / 1) << " Mops/s\n";
  OriginMultipleSafeQueue<int> origin_q;
  avg = Timer::measure([&]() { test_performance_mt_(origin_q, N); }, iter);
  std::cout << "原始线程安全队列 " << N << " 次，耗时: " << avg
            << " ms, 吞吐: " << (N * 2 / avg / 1) << " Mops/s\n";
  PRINT_SUCCESS("性能测试（SPSC 多线程）通过");
}

void test_bulk_multithread() {
  PRINT_INFO("SPSC 批量多线程测试");
  constexpr size_t N = 1'000;
  constexpr size_t BATCH = 32;
  SPSCQueue<int, 1024> q;
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
      if (q.pop(arr[0])) {
        ++cnt;
      } else {
        std::this_thread::yield();
      }
    }
  });
  prod.join();
  cons.join();
  PRINT_SUCCESS("SPSC 批量多线程测试通过");
}

void test_extreme_pressure() {
  PRINT_INFO("极限压力测试（小容量高并发）");
  constexpr size_t N = 2'000'000;
  SPSCQueue<int, 2> q;
  std::atomic<bool> done{false};
  Timer t;
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
  double ms = t.elapsed_ms();
  std::cout << "极限压力测试 " << N << " 次，耗时: " << ms
            << " ms, 吞吐: " << (N * 2 / ms / 1000) << " Mops/s\n";
  PRINT_SUCCESS("极限压力测试通过");
}

int main() {
  test_single_thread();
  test_bulk();
  test_cache_align_switch();
  test_multithread_spsc();
  test_performance();
  test_performance_mt();
  test_bulk_multithread();
  test_extreme_pressure();
  PRINT_SUCCESS("所有测试通过！");
  return 0;
}