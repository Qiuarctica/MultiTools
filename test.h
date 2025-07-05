#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <type_traits>
#include <unistd.h>
#include <vector>

// ======================
// 高精度计时工具
// ======================

class Timer {
public:
  Timer() : start_time_(Clock::now()) {}

  void reset() { start_time_ = Clock::now(); }

  double elapsed_ms() const {
    return std::chrono::duration_cast<Ms>(Clock::now() - start_time_).count();
  }

  double elapsed_us() const {
    return std::chrono::duration_cast<Us>(Clock::now() - start_time_).count();
  }

  double elapsed_ns() const {
    return std::chrono::duration_cast<Ns>(Clock::now() - start_time_).count();
  }

  template <typename Func>
  static double measure(Func &&func, int iterations = 1) {
    Timer t;
    for (int i = 0; i < iterations; ++i) {
      func();
    }
    return t.elapsed_us() / iterations;
  }

private:
  using Clock = std::chrono::high_resolution_clock;
  using Ms = std::chrono::duration<double, std::milli>;
  using Us = std::chrono::duration<double, std::micro>;
  using Ns = std::chrono::duration<double, std::nano>;

  std::chrono::time_point<Clock> start_time_;
};

// 计时宏
#define TIMED_BLOCK(name)                                                      \
  for (Timer _timer; _timer.elapsed_ms() == 0;)                                \
  std::cout << "[" << name << "] 耗时: " << _timer.elapsed_ms() << " ms\n"

#define TIMED_SCOPE(name)                                                      \
  Timer _timer_##name;                                                         \
  std::cout << "[" << #name << "] 开始...\n";                                  \
  struct TimerGuard_##name {                                                   \
    ~TimerGuard_##name() {                                                     \
      std::cout << "[" << #name                                                \
                << "] 结束, 耗时: " << _timer_##name.elapsed_ms() << " ms\n";  \
    }                                                                          \
  } _guard_##name

// ======================
// 内存测量工具
// ======================

inline size_t get_current_rss() {
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS pmc;
  GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
  return pmc.WorkingSetSize;
#else
  long rss = 0;
  FILE *fp = fopen("/proc/self/statm", "r");
  if (fp) {
    if (fscanf(fp, "%*s%ld", &rss) != 1)
      rss = 0;
    fclose(fp);
  }
  return rss * sysconf(_SC_PAGESIZE);
#endif
}

// 内存测量宏
#define MEASURE_MEMORY_START(name)                                             \
  size_t _mem_start_##name = get_current_rss();                                \
  size_t _mem_peak_##name = _mem_start_##name

#define MEASURE_MEMORY_UPDATE(name)                                            \
  do {                                                                         \
    size_t current = get_current_rss();                                        \
    if (current > _mem_peak_##name)                                            \
      _mem_peak_##name = current;                                              \
  } while (0)

#define MEASURE_MEMORY_END(name)                                               \
  do {                                                                         \
    size_t current = get_current_rss();                                        \
    if (current > _mem_peak_##name)                                            \
      _mem_peak_##name = current;                                              \
    std::cout << "[" << #name << "] 内存使用: "                            \
              << (_mem_peak_##name - _mem_start_##name) / 1024                 \
              << " KB (峰值)\n";                                               \
  } while (0)

// ======================
// 增强型断言工具
// ======================

template <typename T> struct is_container {
private:
  template <typename U>
  static auto test(int) -> decltype(std::declval<U>().begin(),
                                    std::declval<U>().end(), std::true_type{});

  template <typename> static std::false_type test(...);

public:
  static constexpr bool value = decltype(test<T>(0))::value;
};

// 容器打印（支持 vector, list, array 等）
template <typename Container>
typename std::enable_if<is_container<Container>::value, std::ostream &>::type
operator<<(std::ostream &os, const Container &c) {
  os << "{";
  for (auto it = c.begin(); it != c.end(); ++it) {
    if (it != c.begin())
      os << ", ";
    os << *it;
  }
  os << "}";
  return os;
}

// 基础断言宏
#define ASSERT(cond)                                                           \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "\n\033[1;31m[断言失败]\033[0m " << #cond                   \
                << "\n  文件: " << __FILE__ << "\n  行号: " << __LINE__        \
                << "\n";                                                       \
      std::abort();                                                            \
    }                                                                          \
  } while (0)

// 带消息的断言
#define ASSERT_MSG(cond, msg)                                                  \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "\n\033[1;31m[断言失败]\033[0m " << #cond                   \
                << "\n  原因: " << msg << "\n  文件: " << __FILE__             \
                << "\n  行号: " << __LINE__ << "\n";                           \
      std::abort();                                                            \
    }                                                                          \
  } while (0)

// 相等断言（支持容器）
#define ASSERT_EQ(a, b)                                                        \
  do {                                                                         \
    auto _a_val = (a);                                                         \
    auto _b_val = (b);                                                         \
    if (!(_a_val == _b_val)) {                                                 \
      std::cerr << "\n\033[1;31m[断言失败]\033[0m " << #a << " == " << #b      \
                << "\n  实际: " << _a_val << "\n  期望: " << _b_val            \
                << "\n  文件: " << __FILE__ << "\n  行号: " << __LINE__        \
                << "\n";                                                       \
      std::abort();                                                            \
    }                                                                          \
  } while (0)

// 浮点数近似相等断言
#define ASSERT_NEAR(a, b, epsilon)                                             \
  do {                                                                         \
    auto _a_val = (a);                                                         \
    auto _b_val = (b);                                                         \
    auto diff = std::abs(_a_val - _b_val);                                     \
    if (diff > epsilon) {                                                      \
      std::cerr << "\n\033[1;31m[断言失败]\033[0m " << #a << " ≈ " << #b       \
                << "\n  实际: " << _a_val << "\n  期望: " << _b_val            \
                << "\n  差值: " << diff << " > " << epsilon                    \
                << "\n  文件: " << __FILE__ << "\n  行号: " << __LINE__        \
                << "\n";                                                       \
      std::abort();                                                            \
    }                                                                          \
  } while (0)

// ======================
// 实用 IO 工具
// ======================

// 带颜色的输出
#define PRINT_INFO(msg)                                                        \
  std::cout << "\033[1;34m[信息]\033[0m " << msg << std::endl

#define PRINT_WARNING(msg)                                                     \
  std::cout << "\033[1;33m[警告]\033[0m " << msg << std::endl

#define PRINT_ERROR(msg)                                                       \
  std::cerr << "\033[1;31m[错误]\033[0m " << msg << std::endl

#define PRINT_SUCCESS(msg)                                                     \
  std::cout << "\033[1;32m[成功]\033[0m " << msg << std::endl

// 二进制文件读写
inline bool write_binary_file(const std::string &filename, const void *data,
                              size_t size) {
  std::ofstream file(filename, std::ios::binary);
  if (!file)
    return false;
  file.write(static_cast<const char *>(data), size);
  return file.good();
}

inline std::vector<char> read_binary_file(const std::string &filename) {
  std::ifstream file(filename, std::ios::binary | std::ios::ate);
  if (!file)
    return {};

  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<char> buffer(size);
  if (!file.read(buffer.data(), size))
    return {};
  return buffer;
}