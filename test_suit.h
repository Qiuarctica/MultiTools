#pragma once

#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

// Macro to enable/disable assertions at compile time
#ifndef STEST_ENABLE_ASSERT
#define STEST_ENABLE_ASSERT 1
#endif

namespace stest {

// ======================
// Formatting and logging utilities
// ======================

template <typename T> std::string to_string_helper(T &&value) {
  if constexpr (std::is_arithmetic_v<std::decay_t<T>>) {
    return std::to_string(value);
  } else if constexpr (std::is_same_v<std::decay_t<T>, std::string>) {
    return value;
  } else if constexpr (std::is_same_v<std::decay_t<T>, const char *>) {
    return std::string(value);
  } else {
    std::ostringstream oss;
    oss << value;
    return oss.str();
  }
}

template <typename... Args>
std::string format_string(const std::string &format, Args &&...args) {
  if constexpr (sizeof...(args) == 0) {
    return format;
  } else {
    std::vector<std::string> arg_lists = {
        to_string_helper(std::forward<Args>(args))...};
    std::string result = format;
    size_t arg_index = 0;
    size_t pos = 0;
    while ((pos = result.find("{}", pos)) != std::string::npos &&
           arg_index < arg_lists.size()) {
      result.replace(pos, 2, arg_lists[arg_index++]);
      pos += arg_lists[arg_index - 1].length();
    }
    return result;
  }
}

enum class LogLevel { Debug, Info, Warning, Error, Fatal };

inline std::string log_level_to_string(LogLevel level) {
  switch (level) {
  case LogLevel::Debug:
    return "\033[1;34mDEBUG\033[0m";
  case LogLevel::Info:
    return "\033[1;32mINFO\033[0m";
  case LogLevel::Warning:
    return "\033[1;33mWARNING\033[0m";
  case LogLevel::Error:
    return "\033[1;31mERROR\033[0m";
  case LogLevel::Fatal:
    return "\033[1;31mFATAL\033[0m";
  default:
    return "UNKNOWN";
  }
}

template <typename... Args>
void print(LogLevel level, const std::string &format, Args &&...args) {
  std::string message = format_string(format, std::forward<Args>(args)...);
  std::string level_str = log_level_to_string(level);
  std::cout << "[" << level_str << "] " << message << "\n";
}

// ======================
// Assertion implementation
// ======================

#if STEST_ENABLE_ASSERT
inline void assert_impl(bool condition, const char *expr, const char *file,
                        int line) {
  if (!condition) {
    print(LogLevel::Error, "Assertion failed: {} at {}:{}", expr, file, line);
    throw std::runtime_error(
        format_string("Assertion failed: {} at {}:{}", expr, file, line));
  }
}

template <typename A, typename B>
inline void assert_eq_impl(const A &a, const B &b, const char *expr_a,
                           const char *expr_b, const char *file, int line) {
  if (!(a == b)) {
    std::string msg =
        format_string("Assertion failed: {} == {}, actual: {} vs expected: {}",
                      expr_a, expr_b, a, b);
    print(LogLevel::Error, "{} at {}:{}", msg, file, line);
    throw std::runtime_error(format_string("{} at {}:{}", msg, file, line));
  }
}

template <typename A, typename B, typename E>
inline void assert_near_impl(const A &a, const B &b, const E &epsilon,
                             const char *expr_a, const char *expr_b,
                             const char *file, int line) {
  auto diff = std::abs(a - b);
  if (diff > epsilon) {
    std::string msg = format_string("Assertion failed: {} ≈ {}, actual: {} vs "
                                    "expected: {}, difference: {} > {}",
                                    expr_a, expr_b, a, b, diff, epsilon);
    print(LogLevel::Error, "{} at {}:{}", msg, file, line);
    throw std::runtime_error(format_string("{} at {}:{}", msg, file, line));
  }
}
#endif

// ======================
// User-friendly assertion macros
// ======================

#if STEST_ENABLE_ASSERT
#define ASSERT(cond) stest::assert_impl((cond), #cond, __FILE__, __LINE__)
#define ASSERT_EQ(a, b)                                                        \
  stest::assert_eq_impl((a), (b), #a, #b, __FILE__, __LINE__)
#define ASSERT_NEAR(a, b, epsilon)                                             \
  stest::assert_near_impl((a), (b), (epsilon), #a, #b, __FILE__, __LINE__)
#define ASSERT_MSG(cond, msg)                                                  \
  do {                                                                         \
    if (!(cond)) {                                                             \
      stest::print(stest::LogLevel::Error,                                     \
                   "Assertion failed: {} - {} at {}:{}", #cond, msg, __FILE__, \
                   __LINE__);                                                  \
      throw std::runtime_error(                                                \
          stest::format_string("Assertion failed: {} - {}", #cond, msg));      \
    }                                                                          \
  } while (0)
#else
#define ASSERT(cond) ((void)0)
#define ASSERT_EQ(a, b) ((void)0)
#define ASSERT_NEAR(a, b, epsilon) ((void)0)
#define ASSERT_MSG(cond, msg) ((void)0)
#endif

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

// Logging macros
#define PRINT_DEBUG(...) stest::print(stest::LogLevel::Debug, __VA_ARGS__)
#define PRINT_INFO(...) stest::print(stest::LogLevel::Info, __VA_ARGS__)
#define PRINT_WARNING(...) stest::print(stest::LogLevel::Warning, __VA_ARGS__)
#define PRINT_ERROR(...) stest::print(stest::LogLevel::Error, __VA_ARGS__)
#define PRINT_FATAL(...) stest::print(stest::LogLevel::Fatal, __VA_ARGS__)

} // namespace stest
