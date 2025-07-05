#include "spsc.h"
#include "test.h"
#include <iostream>
#include <thread>

void test_single_thread() {
  PRINT_INFO << "单线程功能测试\n";
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
  PRINT_SUCCESS << "单线程功能测试通过\n";
}

void test_bulk() {
  PRINT_INFO << "批量 push_bulk 测试\n";
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
  PRINT_SUCCESS << "批量 push_bulk 测试通过\n";
}

void test_cache_align_switch() {
  PRINT_INFO << "缓存/对齐开关测试\n";
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

  PRINT_SUCCESS << "缓存/对齐开关测试通过\n";
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
  PRINT_INFO << "SPSC 多线程正确性测试\n";
  test_multithread_spsc_<true, true>();
  test_multithread_spsc_<true, false>();
  test_multithread_spsc_<false, true>();
  test_multithread_spsc_<false, false>();
  PRINT_SUCCESS << "SPSC 多线程测试通过\n";
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
  PRINT_INFO << "性能测试（单线程循环）\n";
  constexpr size_t iter = 100;
  constexpr size_t N = 10'000'000;

  PRINT_INFO << "SPSCQueue<Cache,Align> 性能测试\n";
  SPSCQueue<int, 1024> q;
  auto avg = Timer::measure([&]() { test_performance_spsc_(q, N); }, iter);
  std::cout << "SPSCQueue<Cache,Align> 吞吐: " << (N * 2 / avg / 1)
            << " Mops/s\n";

  PRINT_INFO << "SPSCQueue<Cache,NoAlign> 性能测试\n";
  SPSCQueue<int, 1024, true, false> q_no_align;
  avg = Timer::measure([&]() { test_performance_spsc_(q_no_align, N); }, iter);
  std::cout << "SPSCQueue<Cache,NoAlign> 吞吐: " << (N * 2 / avg / 1)
            << " Mops/s\n";

  PRINT_INFO << "SPSCQueue<NoCache,Align> 性能测试\n";
  SPSCQueue<int, 1024, false, true> q_no_cache;
  avg = Timer::measure([&]() { test_performance_spsc_(q_no_cache, N); }, iter);
  std::cout << "SPSCQueue<NoCache,Align> 吞吐: " << (N * 2 / avg / 1)
            << " Mops/s\n";

  PRINT_INFO << "SPSCQueue<NoCache,NoAlign> 性能测试\n";
  SPSCQueue<int, 1024, false, false> q_no_cache_no_align;
  avg = Timer::measure(
      [&]() { test_performance_spsc_(q_no_cache_no_align, N); }, iter);
  std::cout << "SPSCQueue<NoCache,NoAlign> 吞吐: " << (N * 2 / avg / 1)
            << " Mops/s\n";

  // PRINT_INFO << "Original线程安全队列性能测试\n";
  // OriginMultipleSafeQueue<int> origin_q;
  // avg = Timer::measure([&]() { test_performance_spsc_(origin_q, N); }, iter);
  // std::cout << "Original线程安全队列 吞吐: " << (N * 2 / avg / 1)
  //           << " Mops/s\n";
  // PRINT_SUCCESS << "性能测试通过\n";
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

template <size_t cap = 1024, size_t N = 10'000'000, size_t iter = 100>
void test_performance_mt() {
  PRINT_INFO << "SPSC多线程性能测试 , cap = " << cap << ", N = " << N
             << ", iter = " << iter << "\n";
  SPSCQueue<int, cap> q;
  auto avg = Timer::measure([&]() { test_performance_mt_(q, N); }, iter);
  std::cout << "SPSCQueue<Cache , Align> " << " 吞吐: " << (N * 2 / avg / 1)
            << " Mops/s\n";
  SPSCQueue<int, cap, true, false> q_no_align;
  avg = Timer::measure([&]() { test_performance_mt_(q_no_align, N); }, iter);
  std::cout << "SPSCQueue<Cache , NoAlign> " << " 吞吐: " << (N * 2 / avg / 1)
            << " Mops/s\n";
  SPSCQueue<int, cap, false, true> q_no_cache;
  avg = Timer::measure([&]() { test_performance_mt_(q_no_cache, N); }, iter);
  std::cout << "SPSCQueue<NoCache , Align> " << " 吞吐: " << (N * 2 / avg / 1)
            << " Mops/s\n";
  SPSCQueue<int, cap, false, false> q_nocache_no_align;
  avg = Timer::measure([&]() { test_performance_mt_(q_nocache_no_align, N); },
                       iter);
  std::cout << "SPSCQueue<NoCache , NoAlign> " << " 吞吐: " << (N * 2 / avg / 1)
            << " Mops/s\n";
  // OriginMultipleSafeQueue<int> origin_q;
  // avg = Timer::measure([&]() { test_performance_mt_(origin_q, N); }, iter);
  // std::cout << "Origin安全队列 " << "吞吐: " << (N * 2 / avg / 1)
  //           << " Mops/s\n";
}

template <size_t BATCH, Queue Q>
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
  PRINT_INFO << "SPSC批量多线程测试 , cap = " << cap << ", N = " << N
             << ", BATCH = " << BATCH << ", iter = " << iter << "\n";
  SPSCQueue<int, cap> q;
  auto avg =
      Timer::measure([&]() { test_bulk_multithread_<BATCH>(q, N); }, iter);
  std::cout << "SPSCQueue<Cache,Align> 批量吞吐: " << (N * 2 / avg / 1)
            << " Mops/s\n";

  SPSCQueue<int, cap, true, false> q_no_align;
  avg = Timer::measure([&]() { test_bulk_multithread_<BATCH>(q_no_align, N); },
                       iter);
  std::cout << "SPSCQueue<Cache,NoAlign> 批量吞吐: " << (N * 2 / avg / 1)
            << " Mops/s\n";

  SPSCQueue<int, cap, false, true> q_no_cache;
  avg = Timer::measure([&]() { test_bulk_multithread_<BATCH>(q_no_cache, N); },
                       iter);
  std::cout << "SPSCQueue<NoCache,Align> 批量吞吐: " << (N * 2 / avg / 1)
            << " Mops/s\n";

  SPSCQueue<int, cap, false, false> q_no_cache_no_align;
  avg = Timer::measure(
      [&]() { test_bulk_multithread_<BATCH>(q_no_cache_no_align, N); }, iter);
  std::cout << "SPSCQueue<NoCache,NoAlign> 批量吞吐: " << (N * 2 / avg / 1)
            << " Mops/s\n";

  // OriginMultipleSafeQueue<int> origin_q;
  // avg = Timer::measure([&]() { test_bulk_multithread_<BATCH>(origin_q, N); },
  //                      iter);
  // std::cout << "Original线程安全队列  批量吞吐: " << (N * 2 / avg / 1)
  //           << " Mops/s\n";
}

int main() {
  test_single_thread();
  test_bulk();
  test_cache_align_switch();
  test_multithread_spsc();
  test_performance();
  test_performance_mt();
  test_bulk_multithread<1024, 10'000'000, 32, 100>();
  test_bulk_multithread<1024, 10'000'000, 64, 100>();
  return 0;
}