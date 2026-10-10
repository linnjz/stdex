/*
 * Copyright (c) 2026 LinnJz
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <algorithm>   // std::fill_n
#include <bit>         // std::bit_cast、std::bit_width
#include <cassert>     // assert
#include <cmath>       // std::isnan
#include <concepts>    // std::same_as、std::copy_constructible、std::regular_invocable、
                       // std::unsigned_integral、std::signed_integral、std::floating_point
#include <iterator>    // std::reverse_iterator、std::random_access_iterator、
                       // std::bidirectional_iterator、std::indirect_strict_weak_order、
                       // std::iter_value_t、std::next、std::prev
#include <limits>      // std::numeric_limits
#include <memory>      // std::allocator、std::allocator_traits、std::destroy_at
#include <type_traits> // std::conditional_t、std::false_type、std::true_type、
                       // std::is_trivially_default_constructible_v、
                       // std::is_trivially_destructible_v、std::is_object_v、
                       // std::is_array_v、std::is_arithmetic_v、std::is_integral_v、
                       // std::is_floating_point_v、std::is_signed_v、
                       // std::underlying_type_t、std::decay_t、
                       // std::invoke_result_t、std::is_constant_evaluated
#include <utility>     // std::move、std::swap、std::pair、std::forward、std::declval、
                       // std::in_place_type_t、std::index_sequence、
                       // std::make_index_sequence
#ifdef _OPENMP
#  include <omp.h> // omp_get_max_threads、omp_get_thread_num、omp_in_parallel
#endif

#include "bit_traits.hpp" // unsigned_of_bits_t, highest_one_bit_index
#include "hybrid_alloc.hpp" // HYBRID_ALIGNED_ALLOC_THRESHOLD、hybrid_free

#pragma push_macro("RADIX_SORT_RESTRICT")
#pragma push_macro("RADIX_SORT_ALWAYS_INLINE")
#undef RADIX_SORT_RESTRICT
#undef RADIX_SORT_ALWAYS_INLINE

#ifdef _MSC_VER
#  define RADIX_SORT_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#  define RADIX_SORT_ALWAYS_INLINE inline __attribute__((__always_inline__))
#else
#  define RADIX_SORT_ALWAYS_INLINE inline
#endif

#if defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER) || \
    defined(__INTEL_COMPILER)
#  define RADIX_SORT_RESTRICT __restrict
#else
#  define RADIX_SORT_RESTRICT
#endif

namespace stdex
{
/**
 * @brief Radix sort traversal strategy.
 *
 * @details
 * - lsd: Least Significant Digit first. Processes digits from least to most
 *   significant byte. Stable, iterative, and uses a single flat array.
 *
 * - msd: Most Significant Digit first. Recursively partitions by the most
 *   significant digit. Forms a tree of sub-problems.
 *
 * # Memory layout & algorithm flow
 *
 * ## LSD (iterative, multi-pass)
 *
 *   Pass 1 (LSB)       Pass 2             Pass 3             Pass 4 (MSB)
 *   +----------------+ +----------------+ +----------------+ +----------------+
 *   | array -> buffer| | buffer -> array| | array -> buffer| | buffer -> array|
 *   +----------------+ +----------------+ +----------------+ +----------------+
 *   -> sorted
 *
 *   - Works on the whole array every pass.
 *   - Each pass scatters elements into 256 (or 65536) buckets.
 *   - No recursion, very good cache locality.
 *
 * ## MSD (recursive, depth-first)
 *
 *   Level 0 (byte 7)      Level 1 (byte 6)          Level 2 (byte 5) ...
 *   +-------------------------------------+
 *   |          whole array                |
 *   +-----------------+-------------------+
 *                     |
 *                     | split by most significant byte
 *              +------+------+
 *              |      |      |
 *        +-----+  +-----+  +-----+
 *        |bucket0|  |bucket1|  ... (up to 256 buckets)
 *        +--+---+  +--+---+
 *           |         |
 *           | recursive sort on next byte
 *        +--+---+  +--+---+
 *        |      |  |      |
 *       ...    ...  ...   ...
 *
 *   - High-order bits decide global order early.
 *   - Once a bucket contains <= threshold, fallback to LSD or insertion sort.
 *
 * # Performance considerations
 *
 * ## 32-bit keys (e.g., `std::uint32_t`)
 *
 *   - LSD with 8-bit chunks: only 4 passes, each pass scans the whole array.
 *   - Very predictable memory access, minimal recursion overhead.
 *   - Usually faster than MSD on modern CPUs (better cache and SIMD friendly).
 *
 * ## 64-bit keys (e.g., `std::uint64_t`)
 *
 *   - LSD with 8-bit chunks needs 8 passes -> more memory writes.
 *   - LSD with 16-bit chunks reduces passes to 4 but uses 65536 buckets,
 *     increasing histogram memory traffic.
 *   - MSD can be faster because the highest bytes often distribute data widely.
 *     After the first pass, sub-problems become much smaller, reducing total
 *     work. Recursion overhead is amortized over large inputs.
 *
 * ## Rule of thumb
 *
 *   - For 32-bit integers or small element types, prefer `lsd`.
 *   - For 64-bit integers, especially with skewed distributions or when
 *     the most significant byte has high entropy, `msd` may outperform `lsd`.
 *   - Always benchmark with your actual data.
 */
enum class radix_sort_strategy
{
  lsd,
  msd
};

/**
 * @brief Execution policy for radix sort.
 *
 * @details
 * Controls whether the sort runs sequentially or uses parallel execution
 *
 * @note parallel execution is only available when _OPENMP is defined.
 */
enum class radix_execution_policy
{
  seq, // sequential execution
  par, // parallel execution (requires OpenMP)
  /* unseq,    */
  /* par_unseq, */
};

/**
 * @brief Sort order for radix sort.
 */
enum class radix_sort_order
{
  ascending,
  descending
};

/**
 * @brief Number of bits processed per radix pass.
 *
 * @details
 * Determines the bucket count as 2^radix_bits_per_pass.
 * - `bits8`  : 8 bits per pass → 256 buckets, 4 passes for 32‑bit keys.
 * - `bits16` : 16 bits per pass → 65536 buckets, 2 passes for 32‑bit keys.
 *
 * `bits16` can be faster for large arrays because it halves the number of
 * passes, but the larger histogram incurs more memory traffic.
 */
enum class radix_bits_per_pass
{
  bits8  = 8,
  bits16 = 16
};

/**
 * @brief Integer type used for bucket counters in the histogram.
 *
 * @details
 * - `uint32` (32‑bit): Sufficient for arrays with fewer than 2³² elements.
 *   Uses less memory for the counter array.
 * - `uint64` (64‑bit): Prevents overflow for arrays with 2³² or more elements,
 *   but doubles the memory footprint of the counter arrays compared to
 * `uint32`.
 *
 * The choice affects both memory usage and the maximum array size that can
 * be sorted without counter overflow.
 */
enum class radix_histogram_storage_type
{
  uint32 = 32,
  uint64 = 64
};

/**
 * @brief Controls where NaN values are placed in the sorted order
 *        when sorting floating-point types.
 *
 * @details
 * This enum is used together with `radix_nans_assumption`. When the assumption
 * is `existence`, the sort will apply the selected positioning strategy.
 * When the assumption is `non_existence`, this setting is ignored (no NaN
 * handling overhead).
 *
 * @warning Requires IEEE 754 compliance (std::numeric_limits<T>::is_iec559)
 *          to be guaranteed at compile time for predictable behavior.
 */
enum class radix_nans_pos_handling
{
  /// No special treatment. NaN values may appear in arbitrary positions
  /// because IEEE 754 does not define a total order for NaN.
  unhandled,

  /// All NaN values are placed at the very beginning of the sorted output,
  /// before -inf (the smallest finite value).
  at_first,

  /// All NaN values are placed at the very end, after +inf.
  at_last
};

/**
 * @brief Informs the sort whether the input floating‑point array may contain
 *        NaN values.
 *
 * @details
 * The default is `non_existence`, which provides the best performance by
 * skipping all NaN‑related logic. If `existence` is chosen, the sort will
 * use the strategy specified by `radix_nans_pos_handling` to position NaN
 * values.
 *
 * @note This enum applies only to floating‑point types (float, double, etc.).
 *       For integer types, it has no effect.
 */
enum class radix_nans_assumption
{
  /// NaN values may be present; the sort will use `radix_nans_pos_handling`
  /// to determine their final position.
  existence,

  /// Assume that no NaN values occur in the input. The sort will skip
  /// all NaN‑handling code, and `radix_nans_pos_handling` is ignored.
  non_existence
};

/**
 * @brief Complete set of compile-time options for radix sort.
 */
struct radix_sort_options
{
  radix_sort_strategy sort_strategy                   = radix_sort_strategy::lsd;
  radix_execution_policy execution_policy             = radix_execution_policy::seq;
  radix_sort_order sort_order                         = radix_sort_order::ascending;
  radix_bits_per_pass bits_per_pass                   = radix_bits_per_pass::bits8;
  radix_histogram_storage_type histogram_storage_type = radix_histogram_storage_type::uint32;
  radix_nans_pos_handling nans_pos_handling           = radix_nans_pos_handling::unhandled;
  radix_nans_assumption nans_assumption               = radix_nans_assumption::non_existence;

  /// MSD sub-problem size below which LSD is used
  std::size_t msd_fallback_to_lsd_threshold   = 256U * 256U;
  /// Stack-allocate histogram if under this
  std::size_t histogram_stack_threshold_bytes = 16U * 1024U;
};

namespace stable_radix_sort_detail
{
template <typename T>
struct is_reverse_iterator : std::false_type
{
};

template <typename Iter>
struct is_reverse_iterator<std::reverse_iterator<Iter>> : std::true_type
{
};

template <typename T>
inline constexpr bool is_reverse_iterator_v = is_reverse_iterator<T>::value;

// Allocator concepts and RAII
template <typename Alloc>
concept standard_allocator = requires (Alloc a, typename std::allocator_traits<Alloc>::size_type n) {
  typename std::allocator_traits<Alloc>::value_type;
  requires std::copy_constructible<Alloc>;
  requires std::same_as<typename std::allocator_traits<Alloc>::pointer, typename std::allocator_traits<Alloc>::value_type *>;
  {
    a.allocate(n)
  } -> std::same_as<typename std::allocator_traits<Alloc>::pointer>;
  {
    a.deallocate(std::declval<typename std::allocator_traits<Alloc>::pointer>(), n)
  } -> std::same_as<void>;
};

template <standard_allocator Alloc>
class allocator_holder
{
  using allocator_traits = std::allocator_traits<Alloc>;

public:
  using value_type = typename allocator_traits::value_type;
  using pointer    = typename allocator_traits::pointer;
  using size_type  = typename allocator_traits::size_type;

  constexpr explicit allocator_holder(size_type size, Alloc const &alloc)
      : alloc_(alloc)
      , size_(size)
      , buffer_(size == 0 ? nullptr : allocator_traits::allocate(alloc_, size))
  {
    construct();
  }

  allocator_holder(allocator_holder const &)             = delete;
  allocator_holder &operator= (allocator_holder const &) = delete;

  constexpr ~allocator_holder() { destroy(); }

  constexpr pointer data() noexcept { return buffer_; }

private:
  constexpr void construct()
  {
    if constexpr (!std::is_trivially_default_constructible_v<value_type> || !std::is_trivially_destructible_v<value_type>) {
      size_type i = 0;
      try {
        for (; i < size_; ++i) {
          allocator_traits::construct(alloc_, buffer_ + i);
        }
      } catch (...) {
        for (size_type j = 0; j < i; ++j) {
          allocator_traits::destroy(alloc_, buffer_ + j);
        }
        allocator_traits::deallocate(alloc_, buffer_, size_);
        throw;
      }
    }
  }

  constexpr void destroy() noexcept
  {
    if (buffer_ == nullptr) return;

    if constexpr (!std::is_trivially_destructible_v<value_type>) {
      for (size_type i = 0; i < size_; ++i) {
        allocator_traits::destroy(alloc_, buffer_ + i);
      }
    }
    allocator_traits::deallocate(alloc_, buffer_, size_);
  }

  [[no_unique_address]] Alloc alloc_;
  size_type size_ { 0 };
  pointer buffer_ { nullptr };
};

struct hybrid_alloc_tag
{
};

/// RAII wrapper around HYBRID_ALIGNED_ALLOC_THRESHOLD / hybrid_free.
template <class T>
class histogram_heap_holder
{
  static_assert(std::is_object_v<T>, "histogram_heap_holder requires an object type");
  static_assert(!std::is_array_v<T>, "histogram_heap_holder does not support array value_type");

public:
  using value_type = T;
  using pointer    = T *;
  using size_type  = std::size_t;

  constexpr histogram_heap_holder(hybrid_alloc_tag, void *ptr, size_type size)
      : data_(static_cast<pointer>(ptr))
      , size_(size)
      , is_hybrid_alloc_ { true }
  {
  }

  constexpr histogram_heap_holder(pointer ptr, size_type size)
      : data_(ptr)
      , size_(size)
  {
  }

  constexpr histogram_heap_holder(std::nullptr_t, size_type) = delete;

  histogram_heap_holder(histogram_heap_holder const &)             = delete;
  histogram_heap_holder &operator= (histogram_heap_holder const &) = delete;
  histogram_heap_holder(histogram_heap_holder &&)                  = delete;
  histogram_heap_holder &operator= (histogram_heap_holder &&)      = delete;

  constexpr ~histogram_heap_holder()
  {
    if (data_ == nullptr) {
      return;
    }

    reset();
  }

  constexpr void reset() noexcept
  {
    if (std::is_constant_evaluated()) {
      delete data_;
    } else {
      if (!is_hybrid_alloc_) {
        delete data_;
      } else {
        if constexpr (!std::is_trivially_destructible_v<value_type>) {
          for (size_type i = 0; i < size_; ++i) {
            std::destroy_at(data_ + i);
          }
        }
        hybrid_free(data_); // HYBRID_ALIGNED_ALLOC_THRESHOLD
      }
    }
    data_ = nullptr;
  }

  constexpr pointer release() noexcept
  {
    pointer p = data_;
    data_     = nullptr;
    return p;
  }

  constexpr pointer data() noexcept { return data_; }

  constexpr pointer const data() const noexcept { return data_; }

  constexpr size_type size() const noexcept { return size_; }

  constexpr T &operator[] (size_type i) noexcept { return data_[i]; }

  constexpr T const &operator[] (size_type i) const noexcept { return data_[i]; }

private:
  pointer data_ { nullptr };
  size_type size_ { 0 };
  bool is_hybrid_alloc_ { false };
};

// User key maps must return arithmetic keys.
template <typename KeyMap, typename T>
concept arithmetic_key_map = std::regular_invocable<KeyMap, T> &&
                             std::is_arithmetic_v<std::invoke_result_t<KeyMap, T>>;

// Internal projections must return unsigned radix keys.
template <typename Projection, typename T>
concept unsigned_integral_projection = requires (Projection p, T t) {
  requires std::regular_invocable<Projection, T>;
  { p(t) } -> std::unsigned_integral;
};

// Adjusts sort order for reverse iterators at compile time.
template <typename Iter, radix_sort_order Order>
struct radix_real_sort_order
{
  static constexpr radix_sort_order value = Order;
};

// Reverse iterators invert the requested sort order.
template <typename WrappedIter, radix_sort_order Order>
struct radix_real_sort_order<std::reverse_iterator<WrappedIter>, Order>
{
  static constexpr radix_sort_order value = (Order == radix_sort_order::ascending)
                                              ? radix_sort_order::descending
                                              : radix_sort_order::ascending;
};

// Default key map for arithmetic value types.
struct radix_key_map_fn
{
  template <typename T>
  constexpr T operator() (T val) const noexcept
  {
    return val;
  }
};

inline constexpr radix_key_map_fn radix_key_map {};

///< Sign-bit traits

// Provides bit masks and indices for the sign bit of an arithmetic type.
template <typename T>
struct sign_bit_traits
{
  static_assert(std::is_arithmetic_v<T>, "arithmetic type required");
  using type                             = T;
  using uint_t                           = unsigned_of_bits_t<sizeof(T) * CHAR_BIT>;
  static constexpr std::size_t sign_bit_index = (sizeof(T) * CHAR_BIT) - 1;
  static constexpr uint_t all_bit_mask   = ~uint_t(0);
  static constexpr uint_t sign_bit_mask  = all_bit_mask << sign_bit_index;
};

///< Radix sort traits (compile-time parameter bundle)

// Compile-time parameter pack combining user options, element type, and key.
template <
    radix_sort_options RadixOptions,
    typename T,
    stable_radix_sort_detail::arithmetic_key_map<T> KeyMap>
struct radix_sort_traits : sign_bit_traits<std::invoke_result_t<KeyMap, T>>
{
  // Re-export user options
  static constexpr auto sort_strategy                   = RadixOptions.sort_strategy;
  static constexpr auto execution_policy                = RadixOptions.execution_policy;
  static constexpr auto sort_order                      = RadixOptions.sort_order;
  static constexpr auto nans_pos_handling               = RadixOptions.nans_pos_handling;
  static constexpr auto nans_assumption                 = RadixOptions.nans_assumption;
  static constexpr auto histogram_stack_threshold_bytes = RadixOptions.histogram_stack_threshold_bytes;
  static constexpr auto msd_fallback_to_lsd_threshold   = RadixOptions.msd_fallback_to_lsd_threshold;

  // Core type aliases
  using value_t = T;
  using key_t   = std::invoke_result_t<KeyMap, T>;
  using uint_t  = typename sign_bit_traits<key_t>::uint_t;
  using hist_t  = std::conditional_t<
      RadixOptions.histogram_storage_type == radix_histogram_storage_type::uint32,
      std::uint32_t,
      std::uint64_t>;

  // Radix parameters
  static constexpr std::size_t bits_per_pass = static_cast<std::underlying_type_t<radix_bits_per_pass>>(RadixOptions.bits_per_pass);
  static constexpr std::size_t num_passes    = sizeof(uint_t) * CHAR_BIT / bits_per_pass;
  static constexpr std::size_t num_buckets   = std::size_t { 1 } << bits_per_pass;
  static constexpr std::size_t radix_mask    = num_buckets - 1U;

  // Pass order (for LSD vs MSD)
  static constexpr std::size_t first_pass = sort_strategy == radix_sort_strategy::lsd ? 0 : num_passes - 1;
  static constexpr std::size_t last_pass  = sort_strategy == radix_sort_strategy::lsd ? num_passes - 1 : 0;

  // Whether sign correction is applied on the first pass
  static constexpr bool is_sign_handling_on_first_pass =
      (sort_strategy == radix_sort_strategy::msd || num_passes == 1) &&
      std::is_signed_v<key_t>;

  // Compile-time sequence of all pass indices
  static constexpr auto pass_indices = std::make_index_sequence<num_passes> {};

  struct alignas(std::hardware_constructive_interference_size) seq_radix_histogram2d
  {
    hist_t storage_[num_passes][num_buckets];
  };

  using aligned_hist2d_t = seq_radix_histogram2d;
};

///< Unsigned integer projection

/// Projects elements to unsigned integers for radix sorting.
/// Composes a user KeyMap with bit-level transformations for signed integers
/// and IEEE 754 floats, handling sign bits and NaN positioning.
struct radix_uint_projection_fn
{
  template <radix_nans_pos_handling nans_pos_handling, typename Arithmetic, typename KeyMap>
  struct projector
  {
    KeyMap km_;

    using sign_bit_traits = sign_bit_traits<Arithmetic>;
    using uint_t          = typename sign_bit_traits::uint_t;

    /// Transforms an arithmetic key into an unsigned integer.
    /// Unsigned → identity. Signed → flips sign bit (if IsSignBitHandling).
    /// IEEE 754 → maps negative floats to [0, 0x7F..FF], positive to
    /// [0x80..00, 0xFF..FF]; NaN goes to 0 or max if requested.
    template <bool IsSignBitHandling = !std::unsigned_integral<Arithmetic>>
    static constexpr auto transform(Arithmetic key) noexcept -> uint_t
    {
      if constexpr (std::same_as<Arithmetic, bool> || std::unsigned_integral<Arithmetic>) {
        // Unsigned types need no transformation.
        return key;
      } else if constexpr (std::signed_integral<Arithmetic>) {
        // Two's complement signed integers: flip sign bit for correct order.
        return IsSignBitHandling ? static_cast<uint_t>(key) ^ sign_bit_traits::sign_bit_mask
                                 : static_cast<uint_t>(key);
      } else if constexpr (std::floating_point<Arithmetic>) {
        static_assert(
            !(nans_pos_handling != radix_nans_pos_handling::unhandled && !std::numeric_limits<Arithmetic>::is_iec559),
            "NaN pos handling requires IEEE 754 floating-point");

        // Handle NaNs if requested.
        if constexpr (nans_pos_handling != radix_nans_pos_handling::unhandled) {
          if (std::isnan(key)) {
            // Map NaN to either the smallest or largest possible key so
            // that it sorts to the beginning or end of the sequence.
            return nans_pos_handling == radix_nans_pos_handling::at_first
                     ? uint_t(0)
                     : std::numeric_limits<uint_t>::max();
          }
        }
        auto const v = std::bit_cast<uint_t>(key);

        // sign_mask is 0 for positive floats, ~0 for negative.
        // Computed as 0 - (v >> sign_bit_index) with unsigned wrap.
        auto const sign_mask = (uint_t(0) - (v >> sign_bit_traits::sign_bit_index));

        // Apply sign-bit flip and, for negatives, invert all bits except sign.
        uint_t base = IsSignBitHandling
                        ? (v ^ sign_mask) | (sign_bit_traits::sign_bit_mask & ~sign_mask)
                        : (v ^ sign_mask);

        // Shift finite values up by one if NaN occupies slot 0.
        if constexpr (nans_pos_handling == radix_nans_pos_handling::at_first) base += 1U;
        return base;
      } else {
        static_assert(std::is_arithmetic_v<Arithmetic>, "transform() requires an arithmetic type");
      }
    }

    template <bool IsSignBitHandling = !std::unsigned_integral<Arithmetic>>
    constexpr auto operator() (auto const &x) const noexcept -> uint_t
    {
      return transform<IsSignBitHandling>(km_(x));
    }

    constexpr auto get_key_map() const noexcept -> KeyMap const & { return km_; }
  };

  template <radix_nans_pos_handling nans_pos_handling, typename T, typename KeyMap>
  constexpr auto operator() (std::in_place_type_t<T> /*unused*/, KeyMap &&key_map) const noexcept
  {
    using arithmetic_t = std::invoke_result_t<KeyMap, T>;
    return projector<nans_pos_handling, arithmetic_t, std::decay_t<KeyMap>> {
      std::forward<KeyMap>(key_map)
    };
  }
};

inline constexpr radix_uint_projection_fn radix_uint_projection {};

///< Radix digit extraction

/// Returns the bucket index for a key in a given radix pass.
/// Shifts by `pass * bits_per_pass` and masks with `radix_mask`.
/// In descending order, inverts the key first.
template <typename RadixTraits>
#if !defined(DEBUG) && !defined(_DEBUG)
RADIX_SORT_ALWAYS_INLINE
#endif
    constexpr auto nth_radix(typename RadixTraits::uint_t x, std::size_t pass) noexcept -> std::size_t
{
  if constexpr (RadixTraits::sort_order == radix_sort_order::ascending) {
    return (x >> RadixTraits::bits_per_pass * pass) & RadixTraits::radix_mask;
  } else if constexpr (RadixTraits::sort_order == radix_sort_order::descending) {
    return ((~x) >> RadixTraits::bits_per_pass * pass) & RadixTraits::radix_mask;
  } else {
    static_assert(RadixTraits::sort_order == radix_sort_order::ascending || 
                  RadixTraits::sort_order == radix_sort_order::descending, 
                  "unsupported radix_sort_order");
  }
}

///< Radix parallel computing utils

/// Thread-chunk descriptors for distributing work across OpenMP threads.
struct thread_chunk_info
{
  std::uint32_t threads; ///< Total OpenMP threads.
  std::uint32_t remain;  ///< Threads that process one extra element.
  std::size_t step;      ///< floor(n / threads) — base elements per thread.
};

/// Per-thread chunk statistics used for sortedness detection.
template <typename T>
struct thread_chunk_stats
{
  bool is_sorted; ///< Whether the chunk is locally sorted (ascending).
  T min_val;      ///< Minimum value in the chunk.
  T max_val;      ///< Maximum value in the chunk.
};

/// Returns the half-open [beg, end) range assigned to thread `tid`.
#if !defined(DEBUG) && !defined(_DEBUG)
RADIX_SORT_ALWAYS_INLINE
#endif
constexpr auto compute_thread_chunk_range(std::size_t tid, std::size_t step, std::size_t remain) noexcept
    -> std::pair<std::size_t, std::size_t>
{
  std::size_t beg_index, end_index;
  if (tid < remain) {
    beg_index = tid * (step + 1);
    end_index = beg_index + (step + 1);
  } else {
    beg_index = remain * (step + 1) + (tid - remain) * step;
    end_index = beg_index + step;
  }
  return std::pair { beg_index, end_index };
}

///< Radix sort fallback sorting mode

/// Maximum element count for which insertion sort is used instead of radix
/// sort.
inline constexpr std::size_t kRadixSortThreshold = 64U;

/// partitions boolean values into false/true groups in a single pass.
template <std::random_access_iterator RandIter, bool Descending>
constexpr void sort_bool(RandIter first, RandIter last)
{
  std::size_t cnt = 0;
  for (auto curr = first; curr < last; ++curr) {
    auto const v   = *curr;
    *curr          = !Descending;
    *(first + cnt) = v;
    cnt += static_cast<std::size_t>(Descending ? v : !v);
  }
}

/// Insertion sort over a bidirectional iterator range.
template <
    std::bidirectional_iterator BiIter,
    std::indirect_strict_weak_order<BiIter> Compare = std::ranges::less>
constexpr void insertion_sort(BiIter first, BiIter last, Compare comp = {})
{
  if (first == last) [[unlikely]]
    return;

  for (auto current = std::next(first); current != last; ++current) {
    auto value = std::move(*current);
    auto hole  = current;
    auto prev  = hole;
    while (prev != first && comp(value, *std::prev(prev))) {
      --prev;
      *hole = std::move(*prev);
      hole  = prev;
    }
    *hole = std::move(value);
  }
}

/// Fallback LSD radix sort used by MSD for small sub-problems.
template <
    typename RadixTraits,
    typename Projection,
    bool IsSignBitHandling = false>
constexpr void integer_radix_sort(
    typename RadixTraits::value_t *RADIX_SORT_RESTRICT data,
    std::size_t const n,
    typename RadixTraits::value_t *RADIX_SORT_RESTRICT buffer,
    std::size_t const passes,
    typename RadixTraits::hist_t *RADIX_SORT_RESTRICT hist1d,
    Projection proj,
    std::size_t depth = 0)
{
  if (n == 0) return;

  for (std::size_t pass = 0; pass < passes; ++pass) {
    using hist_t = typename RadixTraits::hist_t;
    std::fill_n(hist1d, RadixTraits::num_buckets, hist_t { 0 });

    for (std::size_t i = 0; i < n; i++) {
      auto x = proj.template operator()<IsSignBitHandling>(data[i]);
      ++hist1d[nth_radix<RadixTraits>(x, pass)];
    }

    for (std::size_t i = 1; i < RadixTraits::num_buckets; i++) {
      hist1d[i] += hist1d[i - 1];
    }

    for (std::size_t i = n; i-- > 0;) {
      auto x = proj.template operator()<IsSignBitHandling>(data[i]);

      buffer[--hist1d[nth_radix<RadixTraits>(x, pass)]] = std::move(data[i]);
    }
    std::swap(data, buffer);
  }

  if ((depth + passes) & 1) std::move(data, data + n, buffer);
}

///< Implementations for LSD/MSD and sequential/parallel execution.

// Dispatcher selected by options, iterator, allocator, and projection.
template <
    typename RadixTraits,
    std::random_access_iterator RandIter,
    standard_allocator Allocator,
    unsigned_integral_projection<std::iter_value_t<RandIter>> Projection>
struct radix_sort_impl_dispatcher;

///< LSD + seq — sequential, least-significant-digit-first
template <
    typename RadixTraits,
    std::random_access_iterator RandIter,
    standard_allocator Allocator,
    unsigned_integral_projection<std::iter_value_t<RandIter>> Projection>
requires (
    RadixTraits::sort_strategy == radix_sort_strategy::lsd &&
    RadixTraits::execution_policy == radix_execution_policy::seq)
struct radix_sort_impl_dispatcher<RadixTraits, RandIter, Allocator, Projection>
    : RadixTraits
{
  // Re-export frequently used types and constants from the base trait.
  using base = RadixTraits;
  using base::histogram_stack_threshold_bytes;
  using base::last_pass;
  using base::num_buckets;
  using base::num_passes;
  using base::pass_indices;
  using base::sort_order;
  using typename base::aligned_hist2d_t;
  using typename base::hist_t;
  using typename base::key_t;
  using typename base::value_t;
  using hist2d_t = hist_t (*)[num_buckets];

  /// Single-pass histogram builder and sortedness checker.
  /// Returns true if the input is already in the desired order.
  static constexpr auto build_histogram(value_t const *data, std::size_t const n, hist2d_t hist2d, Projection proj)
      -> bool
  {
    // Choose the comparison that indicates "not out of order":
    // for ascending, we expect prev <= key; for descending, prev >= key.
    auto comp = std::conditional_t<sort_order == radix_sort_order::ascending, std::greater_equal<>, std::less_equal<>> {};
    auto prev = sort_order == radix_sort_order::ascending
                  ? std::numeric_limits<key_t>::lowest()
                  : std::numeric_limits<key_t>::max();

    auto const &key_map = proj.get_key_map();
    bool is_sorted      = true;

    for (std::size_t i = 0; i < n; ++i) {
      auto const key = key_map(data[i]);
      is_sorted      = is_sorted && comp(key, prev);
      prev           = key;

      // Build histograms for all passes in a single scan (fold over
      // pass_indices at compile time). Scanning once instead of num_passes
      // times keeps the loop in L1 cache.
      [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        ((++hist2d[Is][nth_radix<RadixTraits>(Projection::transform(key), Is)]), ...);
      }(pass_indices);
    }
    return is_sorted;
  }

  /// Converts per-pass bucket counts to prefix-sum offsets.
  /// Returns non-empty bucket count per pass (used to skip trivial passes).
  template <std::size_t... Is>
  static constexpr void prefix_sum(hist2d_t hist2d, std::index_sequence<Is...> /*unused*/, std::size_t (&non_empty_counts)[sizeof...(Is)])
  {
    auto process = [&](std::size_t index) -> void {
      non_empty_counts[index] = (hist2d[index][0] != 0);
      for (std::size_t i = 1; i < num_buckets; ++i) {
        non_empty_counts[index] += (hist2d[index][i] != 0);
        hist2d[index][i] += hist2d[index][i - 1];
      }
    };
    (process(Is), ...);
  }

  /// Scatters all passes. Sign-bit correction deferred to the last pass.
  /// Returns pointer to the sorted data (may be data or buffer depending on
  /// pass-count parity).
  static constexpr auto do_scatter(
      value_t *RADIX_SORT_RESTRICT data,
      std::size_t const n,
      value_t *RADIX_SORT_RESTRICT buffer,
      std::size_t const (&non_empty_counts)[num_passes],
      hist2d_t hist2d,
      Projection proj) -> value_t *
  {
    // All passes except the last one: sign-bit correction is not needed.
    for (std::size_t pass = 0; pass < last_pass; ++pass) {
      if (non_empty_counts[pass] <= 1) {
        continue; // All elements have the same digit; no reordering needed.
      }
      for (std::size_t i = n; i-- > 0;) {
        auto const x = proj.template operator()<false>(data[i]);

        buffer[--hist2d[pass][nth_radix<RadixTraits>(x, pass)]] = std::move(data[i]);
      }
      std::swap(data, buffer);
    }

    // Last pass: apply full sign-bit handling if the key type is signed.
    if (non_empty_counts[last_pass] > 1) {
      for (std::size_t i = n; i-- > 0;) {
        auto const x = proj(data[i]); // uses the default `IsSignBitHandling`

        buffer[--hist2d[last_pass][nth_radix<RadixTraits>(x, last_pass)]] = std::move(data[i]);
      }
      std::swap(data, buffer);
    }
    return data;
  }

  /// Entry point: builds histogram, computes prefix sums, scatters all passes.
  /// Always leaves the sorted result in the original array.
  static constexpr void sort(
      value_t *RADIX_SORT_RESTRICT data,
      std::size_t const n,
      Allocator const &allocator,
      Projection proj)
  {
    auto *const original = data;

    // Allocate temporary element buffer.
    allocator_holder<Allocator> holder(n, allocator);
    auto *RADIX_SORT_RESTRICT buffer = holder.data();

    auto run_sort = [&](auto &hist2d) constexpr {
      // Step 1: build histograms and check sortedness.
      if (bool sorted = build_histogram(data, n, hist2d, proj)) {
        return; // Already sorted; nothing to do.
      }

      // Step 2: convert counts to offsets and get non-empty bucket counts.
      std::size_t non_empty_counts[num_passes];
      prefix_sum(hist2d, pass_indices, non_empty_counts);

      // Step 3: scatter all passes.
      // The result may end up in the temporary buffer if the number of passes
      // executed is odd.
      auto *result = do_scatter(data, n, buffer, non_empty_counts, hist2d, proj);

      // Step 4: if the sorted data is not in the original array, move it back.
      if (result != original) std::move(result, result + n, original);
    };

    std::size_t const hist_bytes = num_passes * num_buckets * sizeof(hist_t);
    if constexpr (hist_bytes <= histogram_stack_threshold_bytes) {
      aligned_hist2d_t hist2d {}; // init
      run_sort(hist2d.storage_);
    } else {
      histogram_heap_holder<aligned_hist2d_t> hist2d {
        new aligned_hist2d_t(), num_passes
      };
      run_sort(hist2d.data()->storage_);
    }
  }
};

///< MSD + seq — sequential, most-significant-digit-first
template <
    typename RadixTraits,
    std::random_access_iterator RandIter,
    standard_allocator Allocator,
    unsigned_integral_projection<std::iter_value_t<RandIter>> Projection>
requires (
    RadixTraits::sort_strategy == radix_sort_strategy::msd &&
    RadixTraits::execution_policy == radix_execution_policy::seq &&
    RadixTraits::num_passes > 1)
struct radix_sort_impl_dispatcher<RadixTraits, RandIter, Allocator, Projection>
    : RadixTraits
{
  // Re-export frequently used types and constants from the base trait.
  using base = RadixTraits;
  using base::bits_per_pass;
  using base::first_pass;
  using base::histogram_stack_threshold_bytes;
  using base::is_sign_handling_on_first_pass;
  using base::msd_fallback_to_lsd_threshold;
  using base::num_buckets;
  using base::num_passes;
  using base::sort_order;
  using typename base::aligned_hist2d_t;
  using typename base::hist_t;
  using typename base::key_t;
  using typename base::uint_t;
  using typename base::value_t;
  using hist2d_t = hist_t (*)[num_buckets];

  /// First-pass scan: builds MSB histogram, checks sortedness, computes msb.
  /// Returns {msb index, is sorted}.
  static constexpr auto first_pass_scan(value_t const *data, std::size_t const n, hist2d_t hist2d, Projection proj)
      -> std::pair<std::size_t, bool>
  {
    auto prev = sort_order == radix_sort_order::ascending
                  ? std::numeric_limits<key_t>::lowest()
                  : std::numeric_limits<key_t>::max();
    auto comp = std::conditional_t<sort_order == radix_sort_order::ascending, std::greater_equal<>, std::less_equal<>> {};

    auto const &key_map = proj.get_key_map();
    uint_t combined     = 0;
    bool sorted         = true;
    for (std::size_t i = 0; i < n; ++i) {
      auto const key = key_map(data[i]);
      sorted         = sorted && comp(key, prev);
      prev           = key;
      // OR all keys' bit patterns to find the overall msb.
      // This determines the actual pass count in the sort step,
      // skipping passes for leading-zero bytes at no extra scan cost.
      combined |= std::bit_cast<uint_t>(key);
      auto const x = Projection::transform(key);
      ++hist2d[first_pass][nth_radix<RadixTraits>(x, first_pass)];
    }

    return { highest_one_bit_index(combined), sorted };
  }

  /// Builds the histogram for a given non-first pass.
  static constexpr void build_histogram(
      value_t const *data,
      std::size_t const n,
      hist2d_t hist2d,
      std::size_t const pass,
      Projection proj)
  {
    for (std::size_t i = 0; i < n; ++i) {
      auto x = proj.template operator()<false>(data[i]);
      ++hist2d[pass][nth_radix<RadixTraits>(x, pass)];
    }
  }

  /// Converts bucket counts to prefix-sum offsets for a pass.
  static constexpr auto prefix_sum(hist2d_t hist2d, std::size_t const pass)
      -> std::size_t
  {
    std::size_t non_empty_count = (hist2d[pass][0] != 0);
    for (std::size_t i = 1; i < num_buckets; ++i) {
      non_empty_count += (hist2d[pass][i] != 0);
      hist2d[pass][i] += hist2d[pass][i - 1];
    }
    return non_empty_count;
  }

  /// Scatters elements into the buffer for a given pass.
  template <bool IsSignBitHandling = is_sign_handling_on_first_pass>
  static constexpr void do_scatter(
      value_t *RADIX_SORT_RESTRICT data,
      std::size_t const n,
      value_t *RADIX_SORT_RESTRICT buffer,
      std::size_t const pass,
      hist2d_t hist2d,
      Projection proj)
  {
    for (std::size_t i = n; i-- > 0;) {
      auto x = proj.template operator()<IsSignBitHandling>(data[i]);

      buffer[--hist2d[pass][nth_radix<RadixTraits>(x, pass)]] = std::move(data[i]);
    }
  }

  /// Iterates over buckets after scatter; recurses into multi-element buckets.
  static constexpr void recurse_buckets(
      value_t *RADIX_SORT_RESTRICT data,
      std::size_t const n,
      value_t *RADIX_SORT_RESTRICT buffer,
      std::size_t const pass,
      hist2d_t hist2d,
      Projection proj,
      std::size_t depth = 0)
  {
    for (std::size_t i = 0; i < num_buckets; ++i) {
      std::size_t chunk_beg = hist2d[pass][i];
      std::size_t chunk_end = (i + 1 < num_buckets) ? hist2d[pass][i + 1] : n;

      if (std::size_t chunk_size = chunk_end - chunk_beg; chunk_size > 1) {
        // Recurse to the next lower digit, increasing depth.
        do_msd_recursion(
            buffer + chunk_beg,
            chunk_size,
            data + chunk_beg,
            pass - 1,
            hist2d,
            proj,
            depth + 1);
      } else if (chunk_size == 1 && (depth & 1) == 0) {
        // For singleton buckets at even depth, move the element back to the
        // original array because the buffer roles alternate each recursion.
        data[chunk_beg] = std::move(buffer[chunk_beg]);
      }
    }
  }

  /// Recursively sorts sub-problem for a given pass.
  /// Falls back to LSD if sub-problem is small or no lower digits remain.
  static constexpr void do_msd_recursion(
      value_t *RADIX_SORT_RESTRICT data,
      std::size_t const n,
      value_t *RADIX_SORT_RESTRICT buffer,
      std::size_t const pass,
      hist2d_t hist2d,
      Projection proj,
      std::size_t depth = 0)
  {
    auto *hist1d = hist2d[pass];
    // Fallback to LSD when the sub-problem is small or no lower digits remain.
    if (n <= msd_fallback_to_lsd_threshold || pass < 1) {
      // `pass + 1` because LSD expects the number of passes, not the index.
      integer_radix_sort<RadixTraits>(data, n, buffer, pass + 1, hist1d, proj, depth);
      return;
    }

    // Step 1 : Clear the histogram for the current pass.
    std::fill_n(hist1d, num_buckets, hist_t { 0 });

    // Step 2 : build histogram for the current pass.
    build_histogram(data, n, hist2d, pass, proj);

    // Step 3 :
    if (std::size_t non_empty_count = prefix_sum(hist2d, pass); non_empty_count <= 1) {
      // If all elements share the same digit, skip scatter and go to next pass.
      return do_msd_recursion(data, n, buffer, pass - 1, hist2d, proj, depth);
    }

    // Step 4 : scatter using the current pass (no sign-bit handling for
    // non-first passes).
    do_scatter<false>(data, n, buffer, pass, hist2d, proj);
    // Step 5 : recurse into each bucket.
    recurse_buckets(data, n, buffer, pass, hist2d, proj, depth);
  }

  /// Entry point: first-pass scan, then recursive MSD partitioning.
  /// Always leaves the sorted result in the original array.
  static constexpr void sort(
      value_t *RADIX_SORT_RESTRICT data,
      std::size_t const n,
      Allocator const &allocator,
      Projection proj)
  {
    allocator_holder<Allocator> holder(n, allocator);
    auto *RADIX_SORT_RESTRICT buffer = holder.data();

    auto run_sort = [&](auto &hist2d) constexpr {
      // Only the first pass histogram needs to be cleared; other passes will be
      // cleared individually in `do_msd_recursion`.
      std::fill_n(hist2d[first_pass], num_buckets, hist_t { 0 });

      // First pass scan: builds MSB histogram, checks sortedness, computes msb.
      auto const [msb, sorted] = first_pass_scan(data, n, hist2d, proj);
      if (sorted) return;

      // Compute the actual number of passes needed based on the highest set bit.
      // Example: for 32-bit with 8-bit chunks, msb=24 -> passes=(24+8)/8=4.
      auto const passes = (msb + bits_per_pass) / bits_per_pass;

      // If the full number of passes is needed, the first pass histogram is
      // ready.
      if (passes == num_passes && prefix_sum(hist2d, first_pass) > 1) {
        // Scatter using the first pass (with sign-bit handling if needed).
        do_scatter(data, n, buffer, first_pass, hist2d, proj);
        recurse_buckets(data, n, buffer, first_pass, hist2d, proj);
      } else {
        // Start recursion from the highest pass that actually contains data.
        do_msd_recursion(data, n, buffer, passes - 1, hist2d, proj);
      }
    };

    std::size_t const hist_bytes = num_passes * num_buckets * sizeof(hist_t);
    // Pre-allocate histogram memory for all passes at once,
    // prevent memory allocation overhead in every recursion.
    if constexpr (hist_bytes <= histogram_stack_threshold_bytes) {
      aligned_hist2d_t hist2d; // not init
      run_sort(hist2d.storage_);
    } else {
      histogram_heap_holder<aligned_hist2d_t> hist2d {
        new aligned_hist2d_t, num_passes
      };
      run_sort(hist2d.data()->storage_);
    }
  }
};

#ifdef _OPENMP
/// Each thread builds a private histogram; prefix_sum merges them.
template <typename RadixTraits, typename Allocator, typename Projection>
struct radix_sort_par_helpers : RadixTraits
{
  using base = RadixTraits;
  using base::first_pass;
  using base::histogram_stack_threshold_bytes;
  using base::is_sign_handling_on_first_pass;
  using base::nans_assumption;
  using base::num_buckets;
  using base::sort_order;
  using typename base::hist_t;
  using typename base::key_t;
  using typename base::uint_t;
  using typename base::value_t;
  using hist2d_t = hist_t (*)[num_buckets];

  /// Like MSD+seq::first_pass_scan but with per-thread histograms.
  static auto first_pass_scan(
      value_t const *data,
      thread_chunk_info const info,
      hist2d_t hist2d,
      Projection proj) -> std::pair<std::size_t, bool>
  {
    auto comp = std::conditional_t<
        sort_order == radix_sort_order::ascending,
        std::greater_equal<>,
        std::less_equal<>> {};

    auto const &key_map              = proj.get_key_map();
    std::uint32_t const threads           = info.threads;
    std::uint32_t const remain            = info.remain;
    std::size_t const step                = info.step;
    using thread_chunk_stats_wrapped = thread_chunk_stats<key_t>;
    auto const thread_bytes          = threads * sizeof(thread_chunk_stats_wrapped);
    histogram_heap_holder<thread_chunk_stats_wrapped> stats_array {
      hybrid_alloc_tag {},
      HYBRID_ALIGNED_ALLOC_THRESHOLD(
          thread_bytes,
          std::hardware_constructive_interference_size,
          histogram_stack_threshold_bytes),
      threads
    };
    std::memset(stats_array.data(), 0, thread_bytes);

#  pragma omp parallel num_threads((int) threads)
    {
      int const tid         = omp_get_thread_num();
      auto const [beg, end] = compute_thread_chunk_range(tid, step, remain);

      bool sorted  = true;
      auto min_val = std::numeric_limits<key_t>::max();
      auto max_val = std::numeric_limits<key_t>::lowest();
      auto prev    = sort_order == radix_sort_order::ascending
                       ? std::numeric_limits<key_t>::lowest()
                       : std::numeric_limits<key_t>::max();

      for (std::size_t i = beg; i < end; ++i) {
        auto const key = key_map(data[i]);
        sorted         = sorted && comp(key, prev);
        prev           = key;

        auto x { Projection::template transform<is_sign_handling_on_first_pass>(key) };
        ++hist2d[tid][nth_radix<RadixTraits>(x, first_pass)];

        // Skip NaN when computing min/max if NaNs are allowed (they break
        // ordering).
        if constexpr (std::is_floating_point_v<key_t> && nans_assumption == radix_nans_assumption::existence) {
          if (std::isnan(key)) continue;
        }
        if (key < min_val) min_val = key;
        if (key > max_val) max_val = key;
      }
      stats_array[tid] = thread_chunk_stats { sorted, min_val, max_val };
    }

    // Merge per-thread statistics.
    auto &[sorted_1, min_val_1, max_val_1] = stats_array[0];
    uint_t combined                        = 0;
    combined |= std::bit_cast<uint_t>(min_val_1) | std::bit_cast<uint_t>(max_val_1);
    for (std::size_t i = 1; i < threads; ++i) {
      auto const [sorted, min_val, max_val] = stats_array[i];
      combined |= std::bit_cast<uint_t>(min_val) | std::bit_cast<uint_t>(max_val);
      sorted_1 = sorted_1 && sorted;
      if constexpr (sort_order == radix_sort_order::ascending) {
        sorted_1 = sorted_1 && (min_val >= stats_array[i - 1].max_val);
      } else {
        sorted_1 = sorted_1 && (max_val <= stats_array[i - 1].min_val);
      }
    }
    return { highest_one_bit_index(combined), sorted_1 };
  }

  /// Per-thread histogram builder (like the seq version but parallelized).
  template <bool IsSignBitHandling = false>
  static void build_histogram(
      value_t *data,
      thread_chunk_info const info,
      hist2d_t hist2d,
      std::size_t const pass,
      Projection proj)
  {
    std::uint32_t const threads = info.threads;
    std::uint32_t const remain  = info.remain;
    std::size_t const step      = info.step;
#  pragma omp parallel num_threads((int) threads)
    {
      int const tid         = omp_get_thread_num();
      auto const [beg, end] = compute_thread_chunk_range(tid, step, remain);

      for (std::size_t i = beg; i < end; ++i) {
        auto const x = proj.template operator()<IsSignBitHandling>(data[i]);
        ++hist2d[tid][nth_radix<RadixTraits>(x, pass)];
      }
    }
  }

  /// Merges per-thread histogram into global prefix-sum offsets (N-shaped
  /// scan). After this, hist2d[tid][bucket] is the global start offset for that
  /// thread and bucket. Returns the number of non-empty buckets.
  static auto prefix_sum(hist2d_t hist2d, std::size_t threads) -> std::size_t
  {
    for (std::size_t tid = 1; tid < threads; ++tid) {
      hist2d[tid][0] += hist2d[tid - 1][0];
    }

    std::size_t non_empty = (hist2d[threads - 1][0] != 0);
    for (std::size_t i = 1; i < num_buckets; ++i) {
      auto const last = hist2d[threads - 1][i - 1];
      hist2d[0][i] += last;

      for (std::size_t tid = 1; tid < threads; ++tid) {
        hist2d[tid][i] += hist2d[tid - 1][i];
      }

      non_empty += (hist2d[threads - 1][i] - last != 0);
    }
    return non_empty;
  }

  /// Per-thread scatter (like the seq version but parallelized).
  template <bool IsSignBitHandling = false>
  static void do_scatter(
      value_t *RADIX_SORT_RESTRICT data,
      thread_chunk_info const info,
      value_t *RADIX_SORT_RESTRICT buffer,
      std::size_t const pass,
      hist2d_t hist2d,
      Projection proj)
  {
    std::uint32_t const threads = info.threads;
    std::uint32_t const remain  = info.remain;
    std::size_t const step      = info.step;
#  pragma omp parallel num_threads((int) threads)
    {
      int tid         = omp_get_thread_num();
      auto [beg, end] = compute_thread_chunk_range(tid, step, remain);
      for (std::size_t i = end; i-- > beg;) {
        auto const x = proj.template operator()<IsSignBitHandling>(data[i]);

        buffer[--hist2d[tid][nth_radix<RadixTraits>(x, pass)]] = std::move(data[i]);
      }
    }
  }
};

///< LSD + par — parallel version of LSD+seq (requires _OPENMP)
/// Algorithm is the same; key difference: each thread builds its own histogram
/// and scatters elements independently. See LSD + seq for algorithm details.
template <
    typename RadixTraits,
    std::random_access_iterator RandIter,
    standard_allocator Allocator,
    unsigned_integral_projection<std::iter_value_t<RandIter>> Projection>
requires (
    RadixTraits::sort_strategy == radix_sort_strategy::lsd &&
    RadixTraits::execution_policy == radix_execution_policy::par)
struct radix_sort_impl_dispatcher<RadixTraits, RandIter, Allocator, Projection>
    : public radix_sort_par_helpers<RadixTraits, Allocator, Projection>
{
  using base = radix_sort_par_helpers<RadixTraits, Allocator, Projection>;
  using base::bits_per_pass;
  using base::build_histogram;
  using base::do_scatter;
  using base::first_pass;
  using base::first_pass_scan;
  using base::histogram_stack_threshold_bytes;
  using base::is_sign_handling_on_first_pass;
  using base::num_buckets;
  using base::num_passes;
  using base::prefix_sum;
  using typename base::hist_t;
  using typename base::value_t;
  using hist2d_t = hist_t (*)[num_buckets];

  /// Entry point (parallel LSD). Same algorithm as LSD+seq; uses per-thread
  /// histograms and prefix_sum merging for parallel passes.
  static void sort(
      value_t *RADIX_SORT_RESTRICT data,
      std::size_t const n,
      Allocator const &allocator,
      Projection proj)
  {
    auto *const original = data;
    allocator_holder<Allocator> holder(n, allocator);
    auto *RADIX_SORT_RESTRICT buffer = holder.data();

    std::size_t const threads = ::omp_get_max_threads();
    // Per-thread histogram: hist2d[tid][bucket] avoids atomic contention.
    std::size_t const hist_size  = threads * num_buckets;
    std::size_t const hist_bytes = hist_size * sizeof(hist_t);
    thread_chunk_info const chunk_info(threads, n % threads, n / threads);

    histogram_heap_holder<hist_t> hist_storage {
      hybrid_alloc_tag {},
      HYBRID_ALIGNED_ALLOC_THRESHOLD(
          hist_bytes,
          std::hardware_constructive_interference_size,
          histogram_stack_threshold_bytes),
      hist_size
    };
    std::memset(hist_storage.data(), 0, hist_bytes);
    auto hist2d = reinterpret_cast<hist2d_t>(hist_storage.data());

    // Unlike LSD+seq's single-scan multi-pass histogram, the 2D per-thread
    // layout requires iterative histogram-scatter per pass.
    auto [msb, sorted] = first_pass_scan(data, chunk_info, hist2d, proj);
    if (sorted) return;

    if (std::size_t num_empty_count = prefix_sum(hist2d, threads); num_empty_count > 1) {
      base::template do_scatter<is_sign_handling_on_first_pass>(data, chunk_info, buffer, first_pass, hist2d, proj);
      std::swap(data, buffer);
    }

    auto const passes = (msb + bits_per_pass) / bits_per_pass;

    // Process remaining passes (from 1 to passes-1).
    for (std::size_t pass = 1; pass < passes; ++pass) {
      std::memset(hist_storage.data(), 0, hist_bytes);

      if (pass != passes - 1 || passes != num_passes) {
        build_histogram(data, chunk_info, hist2d, pass, proj);
        if (prefix_sum(hist2d, threads) > 1) {
          do_scatter(data, chunk_info, buffer, pass, hist2d, proj);
          std::swap(data, buffer);
        }
      } else {
        base::template build_histogram<true>(data, chunk_info, hist2d, pass, proj);
        if (prefix_sum(hist2d, threads) > 1) {
          base::template do_scatter<true>(data, chunk_info, buffer, pass, hist2d, proj);
          std::swap(data, buffer);
        }
      }
    }

    // If the sorted data is in the temporary buffer, move it back to the
    // original array (this happens when the number of passes executed is odd).
    if (data != original) std::move(data, data + n, original);
  }
};

///< MSD + par — parallel version of MSD+seq (requires _OPENMP)
/// Algorithm is the same; key difference: each sub-problem at the top level
/// is processed by separate OpenMP threads. See MSD + seq for algorithm
/// details.
template <
    typename RadixTraits,
    std::random_access_iterator RandIter,
    standard_allocator Allocator,
    unsigned_integral_projection<std::iter_value_t<RandIter>> Projection>
requires (
    RadixTraits::sort_strategy == radix_sort_strategy::msd &&
    RadixTraits::execution_policy == radix_execution_policy::par &&
    RadixTraits::num_passes > 1)
struct radix_sort_impl_dispatcher<RadixTraits, RandIter, Allocator, Projection>
    : public radix_sort_par_helpers<RadixTraits, Allocator, Projection>
{
  using base = radix_sort_par_helpers<RadixTraits, Allocator, Projection>;
  using base::bits_per_pass;
  using base::build_histogram;
  using base::do_scatter;
  using base::first_pass;
  using base::first_pass_scan;
  using base::histogram_stack_threshold_bytes;
  using base::msd_fallback_to_lsd_threshold;
  using base::num_buckets;
  using base::num_passes;
  using base::prefix_sum;
  using typename base::hist_t;
  using typename base::value_t;
  using hist2d_t = hist_t (*)[num_buckets];

  /// Iterates buckets after scatter, parallelized at the bucket level.
  static void recurse_buckets(
      value_t *RADIX_SORT_RESTRICT data,
      std::size_t const n,
      value_t *RADIX_SORT_RESTRICT buffer,
      std::size_t const pass,
      hist2d_t hist2d,
      thread_chunk_info const info,
      Projection proj,
      std::size_t depth = 0)
  {
    // static schedule: bucket sizes vary but static avoids steal overhead
#  pragma omp parallel for schedule(static) \
      num_threads((int) info.threads) if (info.threads > 1 && !omp_in_parallel())
    // Compatibility:
    //   MSVC provides '/openmp' '/openmp:experimental' '/openmp:llvm'
    //   '/openmp' needs a signed index variable
    for (int i = 0; i < static_cast<int>(num_buckets); ++i) {
      std::size_t chunk_beg = hist2d[0][i];
      std::size_t chunk_end = (i + 1 < num_buckets) ? hist2d[0][i + 1] : n;

      if (std::size_t chunk_size = chunk_end - chunk_beg; chunk_size > 1) {
        do_msd_recursion(
            buffer + chunk_beg,
            chunk_size,
            data + chunk_beg,
            pass - 1,
            info,
            proj,
            depth + 1);
      } else if (chunk_size == 1 && (depth & 1) == 0) {
        data[chunk_beg] = std::move(buffer[chunk_beg]);
      }
    }
  }

  /// Same logic as MSD+seq::do_msd_recursion, but disables nested OpenMP
  /// parallelism once inside a parallel region.
  static void do_msd_recursion(
      value_t *RADIX_SORT_RESTRICT data,
      std::size_t const n,
      value_t *RADIX_SORT_RESTRICT buffer,
      std::size_t const pass,
      thread_chunk_info const info,
      Projection proj,
      std::size_t depth = 0)
  {
    // Disable nested parallelism once inside a parallel region. Recursing
    // with all threads would oversubscribe and hurt cache locality.
    std::uint32_t const threads  = omp_in_parallel() ? 1U : info.threads;
    std::size_t const hist_size  = std::size_t { threads } * num_buckets;
    std::size_t const hist_bytes = hist_size * sizeof(hist_t);

    histogram_heap_holder<hist_t> hist_storage {
      hybrid_alloc_tag {},
      HYBRID_ALIGNED_ALLOC_THRESHOLD(
          hist_bytes,
          std::hardware_constructive_interference_size,
          histogram_stack_threshold_bytes),
      hist_size
    };
    auto hist2d  = reinterpret_cast<hist2d_t>(hist_storage.data());
    auto *hist1d = hist2d[0];

    if (n <= msd_fallback_to_lsd_threshold || pass < 1) {
      integer_radix_sort<RadixTraits>(data, n, buffer, pass + 1, hist1d, proj, depth);
      return;
    }

    std::memset(hist_storage.data(), 0, hist_bytes);

    auto [step, remain] = ::lldiv(n, threads);
    thread_chunk_info const sub_info(threads, remain, step);
    build_histogram(data, sub_info, hist2d, pass, proj);

    if (prefix_sum(hist2d, threads) <= 1) {
      do_msd_recursion(data, n, buffer, pass - 1, info, proj, depth);
      return;
    }
    do_scatter(data, sub_info, buffer, pass, hist2d, proj);
    recurse_buckets(data, n, buffer, pass, hist2d, sub_info, proj, depth);
  }

  /// Entry point (parallel MSD). Same as MSD+seq with parallel first-pass
  /// scan; recursion falls back to sequential once inside a parallel region.
  static void sort(
      value_t *RADIX_SORT_RESTRICT data,
      std::size_t const n,
      Allocator const &allocator,
      Projection proj)
  {
    allocator_holder<Allocator> holder(n, allocator);
    auto *RADIX_SORT_RESTRICT buffer = holder.data();

    std::size_t const threads    = ::omp_get_max_threads();
    std::size_t const hist_size  = threads * num_buckets;
    std::size_t const hist_bytes = hist_size * sizeof(hist_t);
    thread_chunk_info const chunk_info(threads, n % threads, n / threads);

    histogram_heap_holder<hist_t> hist_storage {
      hybrid_alloc_tag {},
      HYBRID_ALIGNED_ALLOC_THRESHOLD(
          hist_bytes,
          std::hardware_constructive_interference_size,
          histogram_stack_threshold_bytes),
      hist_size
    };
    std::memset(hist_storage.data(), 0, hist_bytes);
    auto hist2d = reinterpret_cast<hist2d_t>(hist_storage.data());

    auto [msb, sorted] = first_pass_scan(data, chunk_info, hist2d, proj);
    if (sorted) return;

    auto const passes = (msb + bits_per_pass) / bits_per_pass;
    if (passes == num_passes && prefix_sum(hist2d, threads) > 1) {
      base::template do_scatter<true>(data, chunk_info, buffer, first_pass, hist2d, proj);
      recurse_buckets(data, n, buffer, first_pass, hist2d, chunk_info, proj);
    } else {
      do_msd_recursion(data, n, buffer, passes - 1, chunk_info, proj);
    }
  }
};
#endif

// Selects and invokes the correct radix_sort_impl_dispatcher.
template <
    typename RadixTraits,
    std::random_access_iterator RandIter,
    stable_radix_sort_detail::standard_allocator Allocator,
    unsigned_integral_projection<std::iter_value_t<RandIter>> Projection>
constexpr void stable_radix_sort_impl(
    RandIter first,
    RandIter last,
    Allocator const &allocator,
    Projection proj)
{
  static_assert(
      !(RadixTraits::nans_assumption == radix_nans_assumption::non_existence &&
        RadixTraits::nans_pos_handling != radix_nans_pos_handling::unhandled),
      "If you assumed there are no NaNs, then you shouldn't change the default settings for NaN positions.");

  static_assert(
      !(RadixTraits::bits_per_pass == 16U &&
        sizeof(typename RadixTraits::uint_t) == 1U),
      "Do not set radix_sort_options.bits_per_pass = radix_bits_per_pass::bits16 (two-byte) for one-byte data!");

  static_assert(
      !(RadixTraits::sort_strategy == radix_sort_strategy::msd &&
        RadixTraits::num_passes <= 1),
      "MSD sort with only one pass is equivalent to counting sort; "
      "use LSD strategy (radix_sort_strategy::lsd) for single-pass sorting, "
      "or ensure at least 2 passes for MSD to avoid unsigned underflow in recurse_buckets (pass(0) - 1).");

#ifndef _OPENMP
  static_assert(
      RadixTraits::execution_policy != radix_execution_policy::par,
      "parallel policy requires _OPENMP defined");
#endif

  assert(first <= last && "Invalid iterator range: first must be <= last");

  // 1. Special case: boolean elements use a dedicated O(n) partition routine.
  if constexpr (std::same_as<std::iter_value_t<RandIter>, bool>) {
    // Invert order if descending is requested.
    sort_bool<RandIter, RadixTraits::sort_order != radix_sort_order::ascending>(first, last);
  } else {
    std::size_t const size = last - first;

    // 2. Small-array fallback: insertion sort avoids radix-sort overhead.
    if (size <= kRadixSortThreshold) [[unlikely]] {
      // Use projection as key extractor; compare according to sort_order.
      insertion_sort(first, last, [proj](auto const &a, auto const &b) {
        if constexpr (RadixTraits::sort_order == radix_sort_order::ascending) {
          return proj(a) < proj(b);
        } else {
          return proj(b) < proj(a);
        }
      });
      return;
    }

    // 3. Regular path: Dispatch to the selected strategy
    //    (LSD/seq or LSD/par or MSD/seq or MSD/par).
    radix_sort_impl_dispatcher<RadixTraits, RandIter, Allocator, Projection>::sort(&*first, size, allocator, proj);
  }
}

} // namespace stable_radix_sort_detail

///< Public API

/**
 * @brief Radix sort a range `[first, last)` with full control over
 *                options, allocator, and key projection.
 *
 * @details
 * This is the most general overload. It accepts a `radix_sort_options` template
 * argument, an allocator for temporary storage, and an arbitrary `KeyMap`
 * that projects each element to an arithmetic key (integral or floating‑point).
 *
 * The function automatically handles reverse iterators by swapping the
 * sort order and passing the underlying base iterators to the internal
 * implementation.
 *
 * @tparam RadixOptions  Compile‑time sorting options (see `radix_sort_options`).
 * @tparam RandIter      Random‑access iterator type.
 * @tparam Allocator     Allocator type (must satisfy `standard_allocator`).
 * @tparam KeyMap        A callable `value_t -> arithmetic` (default: identity).
 * @param first          Inclusive start of the range.
 * @param last           Exclusive end of the range.
 * @param allocator      Allocator instance for temporary buffer.
 * @param key_map         Key extraction functor.
 *
 * @example
 * @code
 *   struct Item { double score; int id; };
 *   std::vector<Item> items = ...;
 *   constexpr radix_sort_options opts{
 *       .sort_strategy = radix_sort_strategy::msd,
 *       .sort_order = radix_sort_order::descending};
 *   std::stable_radix_sort<opts>(items.begin(), items.end(),
 *                           std::allocator<Item>{},
 *                           [](const Item& x) { return x.score; });
 * @endcode
 */
template <
    radix_sort_options RadixOptions,
    std::random_access_iterator RandIter,
    stable_radix_sort_detail::standard_allocator Allocator,
    stable_radix_sort_detail::arithmetic_key_map<std::iter_value_t<RandIter>> KeyMap =
        stable_radix_sort_detail::radix_key_map_fn>
constexpr void stable_radix_sort(
    RandIter first,
    RandIter last,
    Allocator const &allocator,
    KeyMap key_map = {})
{
  using value_t = std::iter_value_t<RandIter>;

  // Construct the projection functor that maps value -> unsigned key.
  auto projection = stable_radix_sort_detail::radix_uint_projection.template
                    operator()<RadixOptions.nans_pos_handling>(std::in_place_type<value_t>, std::move(key_map));

  // Reverse iterators invert semantic order, so adjust the options before
  // passing the underlying base iterators to the implementation.
  constexpr auto adjusted_options = [] {
    radix_sort_options opts = RadixOptions;
    opts.sort_order         = stable_radix_sort_detail::radix_real_sort_order<RandIter, RadixOptions.sort_order>::value;
    return opts;
  }();

  using radix_traits = stable_radix_sort_detail::radix_sort_traits<adjusted_options, value_t, KeyMap>;

  // If the iterator is a reverse_iterator, unwrap to base and swap first/last.
  if constexpr (stable_radix_sort_detail::is_reverse_iterator_v<RandIter>) {
    stable_radix_sort_detail::stable_radix_sort_impl<radix_traits>(last.base(), first.base(), allocator, std::move(projection));
  } else {
    stable_radix_sort_detail::stable_radix_sort_impl<radix_traits>(first, last, allocator, std::move(projection));
  }
}

/**
 * @brief Radix sort with explicit options but defaulted allocator.
 *
 * @details
 * Convenience overload that uses `std::allocator<value_t>` internally.
 * All other parameters are the same as the four‑argument overload.
 *
 * @tparam RadixOptions  Compile‑time sorting options.
 * @tparam RandIter      Random‑access iterator type.
 * @tparam KeyMap        A callable `value_t -> arithmetic` (default: identity).
 * @param first          Inclusive start of the range.
 * @param last           Exclusive end of the range.
 * @param key_map         Key extraction functor.
 */
template <
    radix_sort_options RadixOptions,
    std::random_access_iterator RandIter,
    stable_radix_sort_detail::arithmetic_key_map<std::iter_value_t<RandIter>> KeyMap =
        stable_radix_sort_detail::radix_key_map_fn>
constexpr void stable_radix_sort(RandIter first, RandIter last, KeyMap key_map = {})
{
  using value_t = typename std::iter_value_t<RandIter>;
  std::allocator<value_t> allocator {};
  stable_radix_sort<RadixOptions>(first, last, allocator, std::move(key_map));
}

/**
 * @brief Radix sort with default options and defaulted allocator.
 *
 * @details
 * The simplest entry point: sorts `[first, last)` in ascending order
 * using LSD radix sort with 8‑bit chunks and all other options at their
 * defaults. This overload is ideal for quick, generic use.
 *
 * @tparam RandIter  Random‑access iterator type.
 * @tparam KeyMap    A callable `value_t -> arithmetic` (default: identity).
 * @param first      Inclusive start of the range.
 * @param last       Exclusive end of the range.
 * @param key_map    Key extraction functor.
 */
template <
    std::random_access_iterator RandIter,
    stable_radix_sort_detail::arithmetic_key_map<std::iter_value_t<RandIter>> KeyMap =
        stable_radix_sort_detail::radix_key_map_fn>
constexpr void stable_radix_sort(RandIter first, RandIter last, KeyMap key_map = {})
{
  using value_t = typename std::iter_value_t<RandIter>;
  std::allocator<value_t> allocator {};
  constexpr radix_sort_options options {};
  stable_radix_sort<options>(first, last, allocator, std::move(key_map));
}

} // namespace stdex

#pragma pop_macro("RADIX_SORT_ALWAYS_INLINE")
#pragma pop_macro("RADIX_SORT_RESTRICT")
