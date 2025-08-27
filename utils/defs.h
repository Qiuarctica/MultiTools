
#pragma once
#include <concepts>
#include <cstddef>

template <typename W, typename T>
concept Writer =
    std::invocable<W, T *> && std::is_void_v<std::invoke_result_t<W, T *>>;

template <typename R, typename T>
concept Reader = std::invocable<R, const T *> &&
                 std::is_void_v<std::invoke_result_t<R, const T *>>;

template <typename W, typename T>
concept BulkWriter =
    std::invocable<W, T *, size_t, size_t> &&
    std::convertible_to<std::invoke_result_t<W, T *, size_t, size_t>, size_t>;

template <typename R, typename T>
concept BulkReader =
    std::invocable<R, const T *, size_t, size_t> &&
    std::convertible_to<std::invoke_result_t<R, const T *, size_t, size_t>,
                        size_t>;

constexpr size_t CacheLineSize = 64;

#define IS_POWDER_OF_TWO(x) (((x) & ((x) - 1)) == 0)
#define IS_POWDER_OF_TWO_AND_GREATER_THAN_ZERO(x)                              \
  (IS_POWDER_OF_TWO(x) && (x) > 0)
