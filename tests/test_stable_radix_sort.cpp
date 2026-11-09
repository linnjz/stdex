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

#include <algorithm>
#include <bit>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "int128.hpp"
#include "stable_radix_sort.hpp"

namespace
{

thread_local std::mt19937_64 rng { 42 };

// Augmented comparator that matches radix sort's IEEE 754 total ordering
template <typename T>
bool augmentedLess(T const &a, T const &b)
{
  if constexpr (std::is_floating_point_v<T>) {
    if (a < b) return true;
    if (b < a) return false;
    return std::signbit(a) > std::signbit(b);
  } else {
    return a < b;
  }
}

template <typename T>
bool augmentedGreater(T const &a, T const &b)
{
  return augmentedLess(b, a);
}

// Validation helpers
template <typename T>
bool validate(std::vector<T> v)
{
  auto expected = v;
  std::stable_sort(expected.begin(), expected.end(), augmentedLess<T>);
  stdex::stable_radix_sort(v.begin(), v.end());
  if (v.size() != expected.size()) {
    std::fprintf(stderr, "FAIL: size mismatch\n");
    return false;
  }
  for (size_t i = 0; i < v.size(); ++i) {
    if (v[i] != expected[i]) {
      std::fprintf(stderr, "FAIL: mismatch at index %zu: got %s, expected %s\n", i, std::to_string(v[i]).c_str(), std::to_string(expected[i]).c_str());
      return false;
    }
  }
  return true;
}

template <typename T, typename Proj>
bool validate(std::vector<T> v, Proj proj)
{
  auto expected = v;
  std::stable_sort(expected.begin(), expected.end(), [&](T const &a, T const &b) {
    return augmentedLess(proj(a), proj(b));
  });
  stdex::stable_radix_sort(v.begin(), v.end(), proj);
  if (v.size() != expected.size()) {
    std::fprintf(stderr, "FAIL: size mismatch\n");
    return false;
  }
  for (size_t i = 0; i < v.size(); ++i) {
    if (v[i] != expected[i]) {
      std::fprintf(stderr, "FAIL: mismatch at index %zu\n", i);
      return false;
    }
  }
  return true;
}

constexpr stdex::radix_sort_options kDescOpts = [] {
  auto opts       = stdex::radix_sort_options {};
  opts.sort_order = stdex::radix_sort_order::descending;
  return opts;
}();

constexpr stdex::radix_sort_options kLsdSeqAsc = stdex::radix_sort_options {};

constexpr stdex::radix_sort_options kLsdSeqDesc = [] {
  auto opts       = stdex::radix_sort_options {};
  opts.sort_order = stdex::radix_sort_order::descending;
  return opts;
}();

constexpr stdex::radix_sort_options kMsdSeqAsc = [] {
  auto opts            = stdex::radix_sort_options {};
  opts.sort_strategy   = stdex::radix_sort_strategy::msd;
  return opts;
}();

constexpr stdex::radix_sort_options kMsdSeqDesc = [] {
  auto opts            = stdex::radix_sort_options {};
  opts.sort_strategy   = stdex::radix_sort_strategy::msd;
  opts.sort_order      = stdex::radix_sort_order::descending;
  return opts;
}();

#ifdef _OPENMP
constexpr stdex::radix_sort_options kLsdParAsc = [] {
  auto opts             = stdex::radix_sort_options {};
  opts.execution_policy = stdex::radix_execution_policy::par;
  return opts;
}();

constexpr stdex::radix_sort_options kLsdParDesc = [] {
  auto opts             = stdex::radix_sort_options {};
  opts.execution_policy = stdex::radix_execution_policy::par;
  opts.sort_order       = stdex::radix_sort_order::descending;
  return opts;
}();

constexpr stdex::radix_sort_options kMsdParAsc = [] {
  auto opts             = stdex::radix_sort_options {};
  opts.sort_strategy    = stdex::radix_sort_strategy::msd;
  opts.execution_policy = stdex::radix_execution_policy::par;
  return opts;
}();

constexpr stdex::radix_sort_options kMsdParDesc = [] {
  auto opts             = stdex::radix_sort_options {};
  opts.sort_strategy    = stdex::radix_sort_strategy::msd;
  opts.execution_policy = stdex::radix_execution_policy::par;
  opts.sort_order       = stdex::radix_sort_order::descending;
  return opts;
}();
#endif

template <typename T>
bool validateDescending(std::vector<T> v)
{
  auto expected = v;
  std::stable_sort(expected.begin(), expected.end(), augmentedGreater<T>);
  stdex::stable_radix_sort<kDescOpts>(v.begin(), v.end());
  if (v.size() != expected.size()) {
    std::fprintf(stderr, "FAIL: descending size mismatch\n");
    return false;
  }
  for (size_t i = 0; i < v.size(); ++i) {
    if (v[i] != expected[i]) {
      std::fprintf(stderr, "FAIL: descending mismatch at index %zu\n", i);
      return false;
    }
  }
  return true;
}

// Random data generators
template <typename T>
std::vector<T> generateRandom(size_t n)
{
  std::vector<T> v(n);
  if constexpr (std::is_floating_point_v<T>) {
    std::uniform_real_distribution<T> dist(-1000, 1000);
    for (auto &x : v) x = dist(rng);
  } else if constexpr (sizeof(T) == 1) {
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto &x : v) x = static_cast<T>(dist(rng));
  } else if constexpr (std::is_signed_v<T>) {
    std::uniform_int_distribution<int64_t> dist(
        std::numeric_limits<T>::min() / 2, std::numeric_limits<T>::max() / 2);
    for (auto &x : v) x = static_cast<T>(dist(rng));
  } else {
    std::uniform_int_distribution<uint64_t> dist;
    for (auto &x : v) x = static_cast<T>(dist(rng));
  }
  return v;
}

enum class test_nan_position
{
  none,
  first,
  last
};

// Matches the documented ordering by radix key: -0.0 precedes +0.0, and NaN
// can be placed at either end without relying on float comparisons.
template <typename T>
bool test_total_less(T a, T b, test_nan_position pos)
{
  if constexpr (std::is_floating_point_v<T>) {
    bool const a_nan = std::isnan(a);
    bool const b_nan = std::isnan(b);
    if (pos == test_nan_position::first && a_nan != b_nan) return a_nan;
    if (pos == test_nan_position::last && a_nan != b_nan) return b_nan;
    if (a_nan || b_nan) return false;
    if (a < b) return true;
    if (b < a) return false;
    return std::signbit(a) > std::signbit(b);
  } else {
    return a < b;
  }
}

template <typename T>
bool test_same_value(T a, T b)
{
  if constexpr (std::is_floating_point_v<T>) {
    using uint_type = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
    return std::bit_cast<uint_type>(a) == std::bit_cast<uint_type>(b);
  } else {
    return a == b;
  }
}

template <stdex::radix_sort_options Opts, typename T>
bool test_matches_expected(std::vector<T> const &input)
{
  constexpr test_nan_position nan_pos = [] {
    if constexpr (Opts.nans_pos_handling == stdex::radix_nans_pos_handling::at_first) {
      return test_nan_position::first;
    } else if constexpr (Opts.nans_pos_handling == stdex::radix_nans_pos_handling::at_last) {
      return test_nan_position::last;
    } else {
      return test_nan_position::none;
    }
  }();
  constexpr bool descending = Opts.sort_order == stdex::radix_sort_order::descending;

  auto actual   = input;
  auto expected = input;
  std::stable_sort(expected.begin(), expected.end(), [](T const &a, T const &b) {
    return descending ? test_total_less(b, a, nan_pos) : test_total_less(a, b, nan_pos);
  });
  stdex::stable_radix_sort<Opts>(actual.begin(), actual.end());
  if (actual.size() != expected.size()) {
    std::fprintf(stderr, "FAIL: size mismatch\n");
    return false;
  }
  for (std::size_t i = 0; i < actual.size(); ++i) {
    if (!test_same_value(actual[i], expected[i])) {
      std::fprintf(stderr, "FAIL: mismatch at index %zu\n", i);
      return false;
    }
  }
  return true;
}

struct Item
{
  double score;
  int originalIndex;
  bool operator== (Item const &) const = default;
};

template <class T>
class test_fancy_ptr
{
public:
  using element_type    = T;
  using difference_type = std::ptrdiff_t;
  template <class U>
  using rebind = test_fancy_ptr<U>;

  constexpr test_fancy_ptr() noexcept = default;
  constexpr explicit test_fancy_ptr(T *p) noexcept
      : p_(p)
  {
  }

  constexpr T *operator->() const noexcept { return p_; }
  constexpr T &operator*() const noexcept { return *p_; }
  constexpr T *get() const noexcept { return p_; }

  friend constexpr bool operator== (test_fancy_ptr a, test_fancy_ptr b) noexcept { return a.p_ == b.p_; }

private:
  T *p_ {};
};

struct test_allocation_stats
{
  std::size_t allocations   = 0;
  std::size_t deallocations = 0;
};

template <class T>
struct test_fancy_alloc
{
  using value_type = T;
  using pointer    = test_fancy_ptr<T>;
  using size_type  = std::size_t;

  test_allocation_stats *stats = nullptr;

  constexpr test_fancy_alloc() noexcept = default;
  constexpr explicit test_fancy_alloc(test_allocation_stats &s) noexcept
      : stats(&s)
  {
  }

  pointer allocate(size_type n)
  {
    if (stats != nullptr) ++stats->allocations;
    return pointer { static_cast<T *>(::operator new(n * sizeof(T))) };
  }

  void deallocate(pointer p, size_type) noexcept
  {
    if (stats != nullptr) ++stats->deallocations;
    ::operator delete(p.get());
  }
};

struct test_counting_state
{
  std::size_t allocations   = 0;
  std::size_t deallocations = 0;
};

template <class T>
struct test_counting_alloc
{
  using value_type = T;
  using size_type  = std::size_t;

  test_counting_state *state = nullptr;

  constexpr test_counting_alloc() noexcept = default;
  constexpr explicit test_counting_alloc(test_counting_state &s) noexcept
      : state(&s)
  {
  }

  T *allocate(size_type n)
  {
    ++state->allocations;
    return std::allocator<T> {}.allocate(n);
  }

  void deallocate(T *p, size_type n) noexcept
  {
    ++state->deallocations;
    std::allocator<T> {}.deallocate(p, n);
  }
};

template <stdex::radix_sort_options Opts, typename T>
bool test_sorted_skips_element_scratch(std::vector<T> const &input)
{
  test_counting_state state;
  auto v = input;
  stdex::stable_radix_sort<Opts>(v.begin(), v.end(), test_counting_alloc<T> { state });
  return state.allocations == 0 && state.deallocations == 0 && v == input;
}

template <stdex::radix_sort_options Opts, typename T>
bool test_unsorted_allocates_element_scratch(std::vector<T> const &input)
{
  test_counting_state state;
  auto v = input;
  stdex::stable_radix_sort<Opts>(v.begin(), v.end(), test_counting_alloc<T> { state });
  return state.allocations == 1 && state.deallocations == 1;
}

#ifdef _OPENMP
template <stdex::radix_sort_options Opts, typename T>
std::size_t test_parallel_first_pass_msb(std::vector<T> const &v)
{
  using traits = stdex::stable_radix_sort_detail::radix_sort_traits<
      Opts, T, stdex::stable_radix_sort_detail::radix_key_map_fn>;
  auto projection = stdex::stable_radix_sort_detail::radix_uint_projection
                        .template operator()<Opts.nans_pos_handling>(
                            std::in_place_type<T>, stdex::stable_radix_sort_detail::radix_key_map);
  using projection_t = decltype(projection);
  using helper       = stdex::stable_radix_sort_detail::radix_sort_par_helpers<traits, std::allocator<T>, projection_t>;

  std::size_t const threads   = static_cast<std::size_t>(::omp_get_max_threads());
  std::size_t const hist_size = threads * traits::num_buckets;
  std::vector<typename traits::hist_t> hist(hist_size);
  using hist2d_t = typename traits::hist_t (*)[traits::num_buckets];
  auto *hist2d   = reinterpret_cast<hist2d_t>(hist.data());

  stdex::stable_radix_sort_detail::thread_chunk_info const info(threads, v.size() % threads, v.size() / threads);
  auto const [msb, sorted] = helper::first_pass_scan(v.data(), info, hist2d, projection);
  (void) sorted;
  return msb;
}

template <stdex::radix_sort_options Opts, typename T>
std::size_t test_raw_first_pass_msb(std::vector<T> const &v)
{
  using traits = stdex::stable_radix_sort_detail::radix_sort_traits<
      Opts, T, stdex::stable_radix_sort_detail::radix_key_map_fn>;
  typename traits::uint_t combined {};
  for (auto const &x : v) combined |= std::bit_cast<typename traits::uint_t>(x);
  return stdex::highest_one_bit_index(combined);
}
#endif

struct test_tracked_value
{
  std::uint32_t key {};

  static inline std::size_t default_constructions = 0;
  static inline std::size_t destructions          = 0;

  test_tracked_value() { ++default_constructions; }
  explicit test_tracked_value(std::uint32_t k)
      : key(k)
  {
  }

  test_tracked_value(test_tracked_value const &)                = default;
  test_tracked_value(test_tracked_value &&) noexcept            = default;
  test_tracked_value &operator= (test_tracked_value const &)    = default;
  test_tracked_value &operator= (test_tracked_value &&) noexcept = default;
  ~test_tracked_value() { ++destructions; }

  bool operator== (test_tracked_value const &) const = default;
};

struct test_throwing_value
{
  std::uint32_t key {};

  static inline int live                 = 0;
  static inline int default_calls        = 0;
  static inline int throw_at_default_call = -1;

  test_throwing_value()
  {
    int const call = default_calls++;
    if (call == throw_at_default_call) throw std::runtime_error("test_throwing_value");
    ++live;
  }

  explicit test_throwing_value(std::uint32_t k)
      : key(k)
  {
    ++live;
  }

  test_throwing_value(test_throwing_value const &other)
      : key(other.key)
  {
    ++live;
  }

  test_throwing_value(test_throwing_value &&other) noexcept
      : key(other.key)
  {
    ++live;
  }

  test_throwing_value &operator= (test_throwing_value const &) = default;
  test_throwing_value &operator= (test_throwing_value &&) noexcept = default;
  ~test_throwing_value() { --live; }
};

static_assert(std::same_as<test_fancy_alloc<uint32_t>::pointer, test_fancy_ptr<uint32_t>>);
static_assert(stdex::stable_radix_sort_detail::standard_allocator<test_fancy_alloc<uint32_t>>);
static_assert(stdex::stable_radix_sort_detail::standard_allocator<test_counting_alloc<uint32_t>>);

template <class It>
concept accepts_default_radix_sort = requires (It first, It last) {
  stdex::stable_radix_sort(first, last);
};

static_assert(accepts_default_radix_sort<std::vector<uint32_t>::iterator>);
static_assert(!accepts_default_radix_sort<std::deque<uint32_t>::iterator>);
static_assert(accepts_default_radix_sort<std::vector<bool>::iterator>);
static_assert(accepts_default_radix_sort<std::deque<bool>::iterator>);
static_assert(accepts_default_radix_sort<std::reverse_iterator<std::vector<bool>::iterator>>);
static_assert(!accepts_default_radix_sort<std::vector<bool>::const_iterator>);
static_assert(!accepts_default_radix_sort<const uint32_t *>);
static_assert(!accepts_default_radix_sort<std::reverse_iterator<const uint32_t *>>);

static_assert(stdex::stable_radix_sort_detail::radix_sort_iterator<std::vector<bool>::iterator>);
static_assert(stdex::stable_radix_sort_detail::radix_sort_iterator<std::deque<bool>::iterator>);
static_assert(!stdex::stable_radix_sort_detail::radix_sort_iterator<std::vector<bool>::const_iterator>);
static_assert(!stdex::stable_radix_sort_detail::radix_sort_iterator<const uint32_t *>);

int g_tests_run    = 0;
int g_tests_passed = 0;

#define TEST_RADIX(name)           \
  do {                             \
    ++g_tests_run;                 \
    std::printf("  %-50s", #name); \
  } while (0)

#define PASS()             \
  do {                     \
    ++g_tests_passed;      \
    std::printf("PASS\n"); \
  } while (0)

#define FAIL_MSG(msg)               \
  do {                              \
    std::printf("FAIL: %s\n", msg); \
  } while (0)

} // namespace

// ============================================================================
// Basic tests
// ============================================================================
static void test_empty()
{
  TEST_RADIX(Empty);
  validate(std::vector<uint32_t> {});
  PASS();
}

static void test_single_element()
{
  TEST_RADIX(SingleElement);
  validate(std::vector<uint32_t> { 42 });
  PASS();
}

static void test_already_sorted()
{
  TEST_RADIX(AlreadySorted);
  validate(std::vector<uint32_t> { 1, 2, 3, 4, 5 });
  PASS();
}

static void test_reverse_sorted()
{
  TEST_RADIX(ReverseSorted);
  validate(std::vector<uint32_t> { 5, 4, 3, 2, 1 });
  PASS();
}

static void test_all_equal()
{
  TEST_RADIX(AllEqual);
  validate(std::vector<uint32_t>(100, 42));
  PASS();
}

static void test_small_array_below_threshold()
{
  TEST_RADIX(SmallArrayBelowThreshold);
  std::vector<uint32_t> v(50);
  std::iota(v.begin(), v.end(), uint32_t(0));
  std::reverse(v.begin(), v.end());
  validate(v);
  PASS();
}

// ============================================================================
// Unsigned integer types
// ============================================================================
static void test_unsigned_integers()
{
  TEST_RADIX(UnsignedIntegers);
  validate(generateRandom<uint16_t>(1000));
  validate(generateRandom<uint32_t>(1000));
  validate(generateRandom<uint64_t>(1000));
  PASS();
}

// ============================================================================
// Signed integer types
// ============================================================================
static void test_signed_integers()
{
  TEST_RADIX(SignedIntegers);
  validate(generateRandom<int16_t>(1000));
  validate(generateRandom<int32_t>(1000));
  validate(generateRandom<int64_t>(1000));
  PASS();
}

static void test_signed_extremes()
{
  TEST_RADIX(SignedExtremes);
  validate(std::vector<int32_t> {
      std::numeric_limits<int32_t>::min(),
      std::numeric_limits<int32_t>::max(),
      0,
      -1,
      1,
      std::numeric_limits<int32_t>::min() + 1,
      std::numeric_limits<int32_t>::max() - 1,
  });
  validate(std::vector<int64_t> {
      std::numeric_limits<int64_t>::min(),
      std::numeric_limits<int64_t>::max(),
      0,
      -1,
      1,
      std::numeric_limits<int64_t>::min() + 1,
      std::numeric_limits<int64_t>::max() - 1,
  });
  validate(std::vector<int32_t> { -5, 3, -1, 0, -10, 7, 2, -3 });
  PASS();
}

// ============================================================================
// Floating point types
// ============================================================================
static void test_float_random()
{
  TEST_RADIX(FloatRandom);
  validate(generateRandom<float>(1000));
  validate(generateRandom<double>(1000));
  PASS();
}

static void test_float_special_values()
{
  TEST_RADIX(FloatSpecialValues);
  validate(std::vector<double> {
      std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
      0.0,
      -0.0,
      1.0,
      -1.0,
      std::numeric_limits<double>::max(),
      std::numeric_limits<double>::lowest(),
      std::numeric_limits<double>::denorm_min(),
  });
  validate(std::vector<double> { -0.5, -0.1, -0.9, -0.3, -0.7 });
  PASS();
}

static void test_float_nan_positions()
{
  TEST_RADIX(FloatNanPositions);

  // Mix real NaNs with the subnormal patterns whose at_first +1 transform
  // carries across a byte boundary (0x0000FFFF -> 0x00010000). A pass count
  // derived from raw bit_cast() would skip the byte containing that carry.
  constexpr std::uint32_t patterns[] = {
    0x00000000U, // +0.0
    0x0000FFFFU, // subnormal
    0x00000001U, // denorm_min
    0x7F7FFFFFU, // +FLT_MAX
    0xFF800000U, // -inf
    0x7FC00001U, // quiet NaN
    0xFFC00000U, // negative quiet NaN
    0x7F800001U, // signalling NaN pattern
  };
  std::vector<float> v;
  for (int i = 0; i < 1000; ++i) v.push_back(std::bit_cast<float>(patterns[i % std::size(patterns)]));

  constexpr auto with_nans = [](stdex::radix_sort_options base, stdex::radix_nans_pos_handling pos) {
    base.nans_assumption   = stdex::radix_nans_assumption::existence;
    base.nans_pos_handling = pos;
    return base;
  };

  constexpr auto kLsdSeqAtFirst = with_nans(kLsdSeqAsc, stdex::radix_nans_pos_handling::at_first);
  constexpr auto kLsdSeqAtLast  = with_nans(kLsdSeqAsc, stdex::radix_nans_pos_handling::at_last);
  constexpr auto kMsdSeqAtFirst = with_nans(kMsdSeqAsc, stdex::radix_nans_pos_handling::at_first);
  constexpr auto kMsdSeqAtLast  = with_nans(kMsdSeqAsc, stdex::radix_nans_pos_handling::at_last);
  constexpr auto kMsdSeqDescAtFirst =
      with_nans(kMsdSeqDesc, stdex::radix_nans_pos_handling::at_first);
  constexpr auto kMsdBits16AtFirst = with_nans(
      [] {
        auto opts         = stdex::radix_sort_options {};
        opts.sort_strategy = stdex::radix_sort_strategy::msd;
        opts.bits_per_pass = stdex::radix_bits_per_pass::bits16;
        return opts;
      }(),
      stdex::radix_nans_pos_handling::at_first);

  bool ok = true;
  ok = test_matches_expected<kLsdSeqAtFirst>(v) && ok;
  ok = test_matches_expected<kLsdSeqAtLast>(v) && ok;
  ok = test_matches_expected<kMsdSeqAtFirst>(v) && ok;
  ok = test_matches_expected<kMsdSeqAtLast>(v) && ok;
  ok = test_matches_expected<kMsdSeqDescAtFirst>(v) && ok;
  ok = test_matches_expected<kMsdBits16AtFirst>(v) && ok;
#ifdef _OPENMP
  constexpr auto kLsdParAtFirst = with_nans(kLsdParAsc, stdex::radix_nans_pos_handling::at_first);
  constexpr auto kLsdParAtLast  = with_nans(kLsdParAsc, stdex::radix_nans_pos_handling::at_last);
  constexpr auto kMsdParAtFirst = with_nans(kMsdParAsc, stdex::radix_nans_pos_handling::at_first);
  constexpr auto kMsdParAtLast  = with_nans(kMsdParAsc, stdex::radix_nans_pos_handling::at_last);
  ok = test_matches_expected<kLsdParAtFirst>(v) && ok;
  ok = test_matches_expected<kLsdParAtLast>(v) && ok;
  ok = test_matches_expected<kMsdParAtFirst>(v) && ok;
  ok = test_matches_expected<kMsdParAtLast>(v) && ok;
#endif
  if (!ok) {
    FAIL_MSG("float NaN position mismatch");
    return;
  }
  PASS();
}

static void test_negative_values_all_paths()
{
  TEST_RADIX(NegativeValuesAllPaths);
  bool ok = true;

  // The {0, -1} split is the smallest shape that exposes a first-pass scan
  // using a digit-only transform for LSD: raw signed/float keys put -1 after 0.
  std::vector<std::int32_t> ints;
  for (int i = 0; i < 100; ++i) ints.push_back(0);
  for (int i = 0; i < 100; ++i) ints.push_back(-1);
  ints.push_back(std::numeric_limits<std::int32_t>::min());
  ints.push_back(std::numeric_limits<std::int32_t>::max());
  ints.push_back(1);
  ints.push_back(-2);

  ok = test_matches_expected<kLsdSeqAsc>(ints) && ok;
  ok = test_matches_expected<kLsdSeqDesc>(ints) && ok;
  ok = test_matches_expected<kMsdSeqAsc>(ints) && ok;
  ok = test_matches_expected<kMsdSeqDesc>(ints) && ok;
#ifdef _OPENMP
  ok = test_matches_expected<kLsdParAsc>(ints) && ok;
  ok = test_matches_expected<kLsdParDesc>(ints) && ok;
  ok = test_matches_expected<kMsdParAsc>(ints) && ok;
  ok = test_matches_expected<kMsdParDesc>(ints) && ok;
#endif
  if (!ok) {
    FAIL_MSG("int32 negative ordering mismatch");
    return;
  }

  std::vector<float> floats;
  for (int i = 0; i < 100; ++i) floats.push_back(0.0f);
  for (int i = 0; i < 100; ++i) floats.push_back(-1.0f);
  floats.push_back(-std::numeric_limits<float>::max());
  floats.push_back(std::numeric_limits<float>::denorm_min());
  floats.push_back(-std::numeric_limits<float>::denorm_min());
  floats.push_back(std::numeric_limits<float>::infinity());
  floats.push_back(-std::numeric_limits<float>::infinity());

  ok = test_matches_expected<kLsdSeqAsc>(floats) && ok;
  ok = test_matches_expected<kLsdSeqDesc>(floats) && ok;
  ok = test_matches_expected<kMsdSeqAsc>(floats) && ok;
  ok = test_matches_expected<kMsdSeqDesc>(floats) && ok;
#ifdef _OPENMP
  ok = test_matches_expected<kLsdParAsc>(floats) && ok;
  ok = test_matches_expected<kLsdParDesc>(floats) && ok;
  ok = test_matches_expected<kMsdParAsc>(floats) && ok;
  ok = test_matches_expected<kMsdParDesc>(floats) && ok;
#endif
  if (!ok) {
    FAIL_MSG("float negative ordering mismatch");
    return;
  }

  std::vector<double> doubles;
  for (int i = 0; i < 100; ++i) doubles.push_back(0.0);
  for (int i = 0; i < 100; ++i) doubles.push_back(-1.0);
  doubles.push_back(-std::numeric_limits<double>::max());
  doubles.push_back(std::numeric_limits<double>::denorm_min());
  ok = test_matches_expected<kLsdSeqAsc>(doubles) && ok;
  ok = test_matches_expected<kMsdSeqAsc>(doubles) && ok;
#ifdef _OPENMP
  ok = test_matches_expected<kLsdParAsc>(doubles) && ok;
  ok = test_matches_expected<kMsdParAsc>(doubles) && ok;
#endif
  if (!ok) {
    FAIL_MSG("double negative ordering mismatch");
    return;
  }
  PASS();
}

#ifdef _OPENMP
static void test_parallel_pass_count()
{
  TEST_RADIX(ParallelPassCount);
  std::vector<std::int64_t> i64;
  for (int i = 0; i < 10000; ++i) i64.push_back(static_cast<std::int64_t>(i % 256));

  std::vector<std::int32_t> i32;
  for (int i = 0; i < 10000; ++i) i32.push_back(static_cast<std::int32_t>(i % 256));

  std::vector<float> tiny(10000, 1e-38f);
  std::vector<float> one(10000, 1.0f);
  std::vector<float> zeros;
  for (int i = 0; i < 10000; ++i) zeros.push_back((i % 2 == 0) ? 0.0f : -0.0f);

  bool ok = true;
  ok = (test_parallel_first_pass_msb<kLsdParAsc>(i64) == test_raw_first_pass_msb<kLsdParAsc>(i64)) && ok;
  ok = (test_parallel_first_pass_msb<kLsdParAsc>(i32) == test_raw_first_pass_msb<kLsdParAsc>(i32)) && ok;
  ok = (test_parallel_first_pass_msb<kMsdParAsc>(i64) == test_raw_first_pass_msb<kMsdParAsc>(i64)) && ok;
  ok = (test_parallel_first_pass_msb<kLsdParAsc>(tiny) == test_raw_first_pass_msb<kLsdParAsc>(tiny)) && ok;
  ok = (test_parallel_first_pass_msb<kLsdParAsc>(one) == test_raw_first_pass_msb<kLsdParAsc>(one)) && ok;
  ok = (test_parallel_first_pass_msb<kLsdParAsc>(zeros) == test_raw_first_pass_msb<kLsdParAsc>(zeros)) && ok;
  if (!ok) {
    FAIL_MSG("parallel first-pass pass-count estimate regressed");
    return;
  }
  PASS();
}
#endif

// ============================================================================
// Descending order
// ============================================================================
static void test_descending()
{
  TEST_RADIX(Descending);
  validateDescending(generateRandom<uint64_t>(1000));
  validateDescending(generateRandom<int64_t>(1000));
  validateDescending(generateRandom<double>(1000));
  PASS();
}

// ============================================================================
// Key extractor
// ============================================================================
static void test_key_extractor()
{
  TEST_RADIX(KeyExtractor);

  struct Record
  {
    uint64_t id {};
    std::string name;
    bool operator== (Record const &) const = default;
  };

  {
    std::vector<Record> records = {
      { 5, "five"  },
      { 2, "two"   },
      { 8, "eight" },
      { 1, "one"   },
      { 3, "three" },
    };
    auto expected = records;
    std::stable_sort(expected.begin(), expected.end(), [](Record const &a, Record const &b) { return a.id < b.id; });
    stdex::stable_radix_sort(records.begin(), records.end(), [](Record const &r) { return r.id; });
    if (records != expected) {
      FAIL_MSG("key extractor records mismatch");
      return;
    }
  }
  PASS();
}

// ============================================================================
// Stability
// ============================================================================
static void test_stability()
{
  TEST_RADIX(Stability);
  std::vector<Item> items;
  for (int i = 0; i < 100; ++i) {
    items.push_back({ static_cast<double>(i / 10), i });
  }
  std::shuffle(items.begin(), items.end(), rng);
  auto expected = items;
  std::stable_sort(expected.begin(), expected.end(), [](Item const &a, Item const &b) {
    return augmentedLess(a.score, b.score);
  });
  stdex::stable_radix_sort(items.begin(), items.end(), [](Item const &item) { return item.score; });
  if (items != expected) {
    FAIL_MSG("stability mismatch");
    return;
  }
  PASS();
}

// ============================================================================
// Large input
// ============================================================================
static void test_large_input()
{
  TEST_RADIX(LargeInput);
  validate(generateRandom<uint64_t>(100000));
  validate(generateRandom<double>(100000));
  PASS();
}

// ============================================================================
// Small input (insertion sort fallback)
// ============================================================================
static void test_small_input()
{
  TEST_RADIX(SmallInput);
  validate(std::vector<uint64_t> { 9, 7, 5, 3, 1, 8, 6, 4, 2, 0 });
  validate(std::vector<int64_t> { -5, -3, -1, 0, 2, 4, -4, -2, 1, 3 });
  validate(std::vector<double> { -0.5, 0.3, -0.1, 0.9, -0.7 });
  PASS();
}

// ============================================================================
// 16-bit chunks
// ============================================================================
static void test_chunk_bits16()
{
  TEST_RADIX(ChunkBits16);
  constexpr auto kChunk16Opts = [] {
    return stdex::radix_sort_options {
      .sort_strategy    = stdex::radix_sort_strategy::lsd,
      .execution_policy = stdex::radix_execution_policy::seq,
      .sort_order       = stdex::radix_sort_order::ascending,
      .bits_per_pass    = stdex::radix_bits_per_pass::bits16
    };
  }();
  {
    auto v        = generateRandom<uint32_t>(1000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    stdex::stable_radix_sort<kChunk16Opts>(v.begin(), v.end());
    if (v != expected) {
      FAIL_MSG("uint32 chunk16 mismatch");
      return;
    }
  }
  {
    auto v        = generateRandom<int32_t>(1000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    stdex::stable_radix_sort<kChunk16Opts>(v.begin(), v.end());
    if (v != expected) {
      FAIL_MSG("int32 chunk16 mismatch");
      return;
    }
  }
  PASS();
}

// ============================================================================
// 64-bit bucket counters
// ============================================================================
static void test_counter_type64()
{
  TEST_RADIX(CounterType64);
  constexpr auto kCounter64Opts = [] {
    return stdex::radix_sort_options {
      .sort_strategy          = stdex::radix_sort_strategy::lsd,
      .execution_policy       = stdex::radix_execution_policy::seq,
      .sort_order             = stdex::radix_sort_order::ascending,
      .bits_per_pass          = stdex::radix_bits_per_pass::bits8,
      .histogram_storage_type = stdex::radix_histogram_storage_type::uint64
    };
  }();
  auto v        = generateRandom<uint64_t>(10000);
  auto expected = v;
  std::stable_sort(expected.begin(), expected.end());
  stdex::stable_radix_sort<kCounter64Opts>(v.begin(), v.end());
  if (v != expected) {
    FAIL_MSG("counter64 mismatch");
    return;
  }
  PASS();
}

// ============================================================================
// Insertion threshold boundary (63, 64, 65)
// ============================================================================
static void test_insertion_threshold()
{
  TEST_RADIX(InsertionThreshold);
  for (std::size_t n : { std::size_t { 63 }, std::size_t { 64 }, std::size_t { 65 } }) {
    std::vector<uint32_t> v(n);
    std::iota(v.begin(), v.end(), uint32_t(0));
    std::shuffle(v.begin(), v.end(), rng);
    if (!validate(v)) {
      FAIL_MSG("uint32 threshold boundary");
      return;
    }
  }
  for (std::size_t n : { std::size_t { 63 }, std::size_t { 64 }, std::size_t { 65 } }) {
    std::vector<int64_t> v(n);
    std::iota(v.begin(), v.end(), int64_t(-32));
    std::shuffle(v.begin(), v.end(), rng);
    if (!validate(v)) {
      FAIL_MSG("int64 threshold boundary");
      return;
    }
  }
  PASS();
}

// ============================================================================
// All elements map to the same bucket
// ============================================================================
static void test_all_same_bucket()
{
  TEST_RADIX(AllSameBucket);
  {
    std::vector<uint64_t> v(1000, 0);
    validate(v);
  }
  {
    std::vector<uint32_t> v(500, 42);
    validate(v);
  }
  {
    std::vector<int64_t> v(300, -1);
    validate(v);
  }
  PASS();
}

// ============================================================================
// Early exit when input is already sorted
// ============================================================================
static void test_early_exit_sorted()
{
  TEST_RADIX(EarlyExitSorted);
  std::vector<std::int64_t> sorted(10000);
  std::iota(sorted.begin(), sorted.end(), int64_t(-5000));
  std::vector<std::int64_t> reverse_sorted(sorted.rbegin(), sorted.rend());

  bool ok = true;
  ok = test_sorted_skips_element_scratch<kLsdSeqAsc>(sorted) && ok;
  ok = test_sorted_skips_element_scratch<kMsdSeqAsc>(sorted) && ok;
  ok = test_sorted_skips_element_scratch<kLsdSeqDesc>(reverse_sorted) && ok;
  ok = test_sorted_skips_element_scratch<kMsdSeqDesc>(reverse_sorted) && ok;
#ifdef _OPENMP
  ok = test_sorted_skips_element_scratch<kLsdParAsc>(sorted) && ok;
  ok = test_sorted_skips_element_scratch<kMsdParAsc>(sorted) && ok;
  ok = test_sorted_skips_element_scratch<kLsdParDesc>(reverse_sorted) && ok;
  ok = test_sorted_skips_element_scratch<kMsdParDesc>(reverse_sorted) && ok;
#endif
  if (!ok) {
    FAIL_MSG("sorted input allocated element scratch");
    return;
  }

  auto shuffled = sorted;
  std::shuffle(shuffled.begin(), shuffled.end(), rng);
  ok = test_unsorted_allocates_element_scratch<kLsdSeqAsc>(shuffled) && ok;
  ok = test_unsorted_allocates_element_scratch<kMsdSeqAsc>(shuffled) && ok;
#ifdef _OPENMP
  ok = test_unsorted_allocates_element_scratch<kLsdParAsc>(shuffled) && ok;
  ok = test_unsorted_allocates_element_scratch<kMsdParAsc>(shuffled) && ok;
#endif
  if (!ok) {
    FAIL_MSG("unsorted input did not use element scratch exactly once");
    return;
  }
  PASS();
}

// ============================================================================
// Custom projection
// ============================================================================
static void test_custom_projection()
{
  TEST_RADIX(CustomProjection);
  {
    std::vector<int32_t> v(200);
    for (auto &x : v) x = static_cast<int32_t>(rng() % 1000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end(), [](int32_t a, int32_t b) { return (a % 10) < (b % 10); });
    stdex::stable_radix_sort(v.begin(), v.end(), [](int32_t x) { return x % 10; });
    if (v != expected) {
      FAIL_MSG("modulo projection mismatch");
      return;
    }
  }
  {
    // Regression: the 3-argument options overload must not lose overload
    // resolution to the allocator overload when KeyMap is not an allocator.
    std::vector<int32_t> v(200);
    for (auto &x : v) x = static_cast<int32_t>(rng() % 1000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end(), [](int32_t a, int32_t b) { return (a % 10) < (b % 10); });
    auto key_map = [](int32_t x) { return x % 10; };
    stdex::stable_radix_sort<kMsdSeqAsc>(v.begin(), v.end(), key_map);
    if (v != expected) {
      FAIL_MSG("options + key map overload mismatch");
      return;
    }
  }
  PASS();
}

// ============================================================================
// Reverse iterator
// ============================================================================
static void test_reverse_iterator()
{
  TEST_RADIX(ReverseIterator);
  {
    auto v = generateRandom<uint64_t>(200);
    stdex::stable_radix_sort(v.rbegin(), v.rend());
    for (size_t i = 1; i < v.size(); ++i) {
      if (v[i - 1] < v[i]) {
        FAIL_MSG("reverse iterator ascending failed");
        return;
      }
    }
  }
  PASS();
}

// ============================================================================
// Single-byte types
// ============================================================================
static void test_single_byte()
{
  TEST_RADIX(SingleByte);
  {
    std::vector<uint8_t> v(256);
    std::iota(v.begin(), v.end(), uint8_t(0));
    std::shuffle(v.begin(), v.end(), rng);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    stdex::stable_radix_sort(v.begin(), v.end());
    if (v != expected) {
      FAIL_MSG("uint8 mismatch");
      return;
    }
  }
  {
    std::vector<int8_t> v(256);
    std::iota(v.begin(), v.end(), int8_t(-128));
    std::shuffle(v.begin(), v.end(), rng);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end(), augmentedLess<int8_t>);
    stdex::stable_radix_sort(v.begin(), v.end());
    if (v != expected) {
      FAIL_MSG("int8 mismatch");
      return;
    }
  }
  PASS();
}

// ============================================================================
// char types
// ============================================================================
static void test_char_types()
{
  TEST_RADIX(CharTypes);
  {
    std::vector<unsigned char> v(256);
    std::iota(v.begin(), v.end(), static_cast<unsigned char>(0));
    std::shuffle(v.begin(), v.end(), rng);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    stdex::stable_radix_sort(v.begin(), v.end());
    if (v != expected) {
      FAIL_MSG("unsigned char mismatch");
      return;
    }
  }
  PASS();
}

// ============================================================================
// wchar_t / char16_t / char32_t
// ============================================================================
static void test_wide_char_types()
{
  TEST_RADIX(WideCharTypes);
  {
    std::vector<char16_t> v(1000);
    std::uniform_int_distribution<int> dist(0, 65535);
    for (auto &x : v) x = static_cast<char16_t>(dist(rng));
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    stdex::stable_radix_sort(v.begin(), v.end());
    if (v != expected) {
      FAIL_MSG("char16_t mismatch");
      return;
    }
  }
  {
    std::vector<char32_t> v(1000);
    std::uniform_int_distribution<uint32_t> dist;
    for (auto &x : v) x = static_cast<char32_t>(dist(rng));
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    stdex::stable_radix_sort(v.begin(), v.end());
    if (v != expected) {
      FAIL_MSG("char32_t mismatch");
      return;
    }
  }
  PASS();
}

// ============================================================================
// MSD sort
// ============================================================================
static void test_msd_seq()
{
  TEST_RADIX(MsdSeq);
  constexpr auto kMsdOpts = [] {
    return stdex::radix_sort_options {
      .sort_strategy    = stdex::radix_sort_strategy::msd,
      .execution_policy = stdex::radix_execution_policy::seq
    };
  }();
  {
    auto v        = generateRandom<uint64_t>(1000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    stdex::stable_radix_sort<kMsdOpts>(v.begin(), v.end());
    if (v != expected) {
      FAIL_MSG("msd uint64 mismatch");
      return;
    }
  }
  {
    auto v        = generateRandom<int64_t>(1000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    stdex::stable_radix_sort<kMsdOpts>(v.begin(), v.end());
    if (v != expected) {
      FAIL_MSG("msd int64 mismatch");
      return;
    }
  }
  {
    auto v        = generateRandom<double>(1000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    stdex::stable_radix_sort<kMsdOpts>(v.begin(), v.end());
    if (v != expected) {
      FAIL_MSG("msd double mismatch");
      return;
    }
  }
  PASS();
}

// ============================================================================
// Descending + UInt64 counter
// ============================================================================
static void test_descending_counter64()
{
  TEST_RADIX(DescendingCounter64);
  constexpr auto kDescC64 = [] {
    return stdex::radix_sort_options {
      .sort_order             = stdex::radix_sort_order::descending,
      .histogram_storage_type = stdex::radix_histogram_storage_type::uint64
    };
  }();
  auto v        = generateRandom<uint64_t>(10000);
  auto expected = v;
  std::stable_sort(expected.begin(), expected.end(), std::greater<uint64_t> {});
  stdex::stable_radix_sort<kDescC64>(v.begin(), v.end());
  if (v != expected) {
    FAIL_MSG("descending counter64 mismatch");
    return;
  }
  PASS();
}

// ============================================================================
// Descending + 16-bit chunks
// ============================================================================
static void test_descending_chunk16()
{
  TEST_RADIX(DescendingChunk16);
  constexpr auto kDescC16 = [] {
    return stdex::radix_sort_options {
      .sort_order    = stdex::radix_sort_order::descending,
      .bits_per_pass = stdex::radix_bits_per_pass::bits16
    };
  }();
  {
    auto v        = generateRandom<uint32_t>(1000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end(), std::greater<uint32_t> {});
    stdex::stable_radix_sort<kDescC16>(v.begin(), v.end());
    if (v != expected) {
      FAIL_MSG("descending chunk16 uint32 mismatch");
      return;
    }
  }
  PASS();
}

// ============================================================================
// MSD + Bits16
// ============================================================================
static void test_msd_bits16()
{
  TEST_RADIX(MsdBits16);
  constexpr auto kMsdBits16 = [] {
    return stdex::radix_sort_options {
      .sort_strategy    = stdex::radix_sort_strategy::msd,
      .execution_policy = stdex::radix_execution_policy::seq,
      .bits_per_pass    = stdex::radix_bits_per_pass::bits16
    };
  }();
  auto v        = generateRandom<uint64_t>(500);
  auto expected = v;
  std::stable_sort(expected.begin(), expected.end());
  stdex::stable_radix_sort<kMsdBits16>(v.begin(), v.end());
  if (v != expected) {
    FAIL_MSG("msd bits16 mismatch");
    return;
  }
  PASS();
}

// ============================================================================
// Bool type
// ============================================================================
static void test_bool_type()
{
  TEST_RADIX(BoolType);
  {
    std::vector<uint8_t> v = { 1, 0, 1, 0, 1, 0, 0, 1 };
    auto expected          = v;
    std::stable_sort(expected.begin(), expected.end());
    stdex::stable_radix_sort(v.begin(), v.end());
    if (v != expected) {
      FAIL_MSG("uint8 bool mismatch");
      return;
    }
  }
  PASS();
}

static void test_vector_bool()
{
  TEST_RADIX(VectorBool);
  {
    std::vector<bool> v;
    for (int i = 0; i < 1000; ++i) v.push_back((i * 37) % 5 < 2);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    stdex::stable_radix_sort(v.begin(), v.end());
    if (v != expected) {
      FAIL_MSG("vector<bool> large mismatch");
      return;
    }
  }
  {
    std::vector<bool> v { true, false, true, false, false, true, false, true };
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    stdex::stable_radix_sort(v.begin(), v.end());
    if (v != expected) {
      FAIL_MSG("vector<bool> small mismatch");
      return;
    }
  }
  {
    std::deque<bool> v;
    for (int i = 0; i < 1000; ++i) v.push_back((i * 37) % 5 < 2);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    stdex::stable_radix_sort(v.begin(), v.end());
    if (v != expected) {
      FAIL_MSG("deque<bool> mismatch");
      return;
    }
  }
  {
    std::vector<bool> v;
    for (int i = 0; i < 1000; ++i) v.push_back((i * 37) % 5 < 2);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end(), std::greater<> {});
    stdex::stable_radix_sort(v.rbegin(), v.rend());
    if (v != expected) {
      FAIL_MSG("reverse vector<bool> descending mismatch");
      return;
    }
  }
  PASS();
}

static void test_signed_zero()
{
  TEST_RADIX(SignedZero);
  bool ok = true;

  std::vector<float> alternating;
  for (int i = 0; i < 1000; ++i) alternating.push_back((i % 2 == 0) ? 0.0f : -0.0f);
  ok = test_matches_expected<kLsdSeqAsc>(alternating) && ok;
  ok = test_matches_expected<kLsdSeqDesc>(alternating) && ok;
  ok = test_matches_expected<kMsdSeqAsc>(alternating) && ok;
  ok = test_matches_expected<kMsdSeqDesc>(alternating) && ok;
#ifdef _OPENMP
  ok = test_matches_expected<kLsdParAsc>(alternating) && ok;
  ok = test_matches_expected<kLsdParDesc>(alternating) && ok;
  ok = test_matches_expected<kMsdParAsc>(alternating) && ok;
  ok = test_matches_expected<kMsdParDesc>(alternating) && ok;
#endif

  std::vector<double> mixed;
  for (int i = 0; i < 1000; ++i) {
    switch (i % 5) {
    case 0: mixed.push_back(0.0); break;
    case 1: mixed.push_back(-0.0); break;
    case 2: mixed.push_back(-3.5); break;
    case 3: mixed.push_back(2.25); break;
    default: mixed.push_back(-1.0); break;
    }
  }
  ok = test_matches_expected<kLsdSeqAsc>(mixed) && ok;
  ok = test_matches_expected<kMsdSeqAsc>(mixed) && ok;
  ok = test_matches_expected<kLsdSeqDesc>(mixed) && ok;
#ifdef _OPENMP
  ok = test_matches_expected<kLsdParAsc>(mixed) && ok;
  ok = test_matches_expected<kMsdParAsc>(mixed) && ok;
#endif

  if (!ok) {
    FAIL_MSG("signed zero ordering mismatch");
    return;
  }
  PASS();
}

static void test_fancy_pointer_allocator()
{
  TEST_RADIX(FancyPointerAllocator);
  {
    auto v        = generateRandom<uint32_t>(1000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    test_allocation_stats stats;
    stdex::stable_radix_sort<kLsdSeqAsc>(v.begin(), v.end(), test_fancy_alloc<uint32_t> { stats });
    if (v != expected || stats.allocations != 1 || stats.deallocations != 1) {
      FAIL_MSG("fancy pointer allocator mismatch");
      return;
    }
  }
  {
    // Non-trivial value type exercises allocator_traits::construct/destroy.
    std::vector<test_tracked_value> v;
    std::vector<std::uint32_t> expected;
    v.reserve(1000);
    expected.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
      auto const key = static_cast<std::uint32_t>((i * 37) % 1000);
      v.emplace_back(key);
      expected.push_back(key);
    }
    std::stable_sort(expected.begin(), expected.end());

    auto const dc_before = test_tracked_value::default_constructions;
    auto const d_before  = test_tracked_value::destructions;
    test_allocation_stats stats;
    stdex::stable_radix_sort<kMsdSeqAsc>(
        v.begin(), v.end(), test_fancy_alloc<test_tracked_value> { stats },
        [](test_tracked_value const &x) { return x.key; });

    bool sorted = true;
    for (std::size_t i = 0; i < v.size(); ++i) sorted = sorted && (v[i].key == expected[i]);
    auto const default_constructed = test_tracked_value::default_constructions - dc_before;
    auto const destroyed           = test_tracked_value::destructions - d_before;
    if (!sorted || stats.allocations != 1 || stats.deallocations != 1 || default_constructed == 0 ||
        destroyed != default_constructed) {
      FAIL_MSG("non-trivial fancy pointer allocator mismatch");
      return;
    }
  }
  {
    // A throwing default constructor must destroy constructed elements and
    // deallocate the raw buffer before propagating.
    std::vector<test_throwing_value> v;
    v.reserve(1000);
    for (int i = 0; i < 1000; ++i) v.emplace_back(static_cast<std::uint32_t>((i * 37) % 1000));

    int const live_before  = test_throwing_value::live;
    int const calls_before = test_throwing_value::default_calls;
    test_throwing_value::throw_at_default_call = calls_before + 3;
    test_allocation_stats stats;
    bool threw = false;
    try {
      stdex::stable_radix_sort<kLsdSeqAsc>(
          v.begin(), v.end(), test_fancy_alloc<test_throwing_value> { stats },
          [](test_throwing_value const &x) { return x.key; });
    } catch (std::runtime_error const &) {
      threw = true;
    }
    test_throwing_value::throw_at_default_call = -1;

    if (!threw || test_throwing_value::live != live_before || stats.allocations != 1 ||
        stats.deallocations != 1) {
      FAIL_MSG("fancy pointer exception cleanup mismatch");
      return;
    }
  }
  PASS();
}

// ============================================================================
// 128-bit custom keys (stdex::uint128_t / stdex::int128_t)
// ============================================================================
template <typename T>
std::vector<T> generate128(std::size_t n)
{
  std::vector<T> v(n);
  for (auto &x : v) {
    std::uint64_t const low  = rng();
    std::uint64_t const high = rng();
    x                        = T::from_words(low, high);
  }
  return v;
}

template <typename T>
bool validate128(std::vector<T> v)
{
  auto expected = v;
  std::stable_sort(expected.begin(), expected.end());
  stdex::stable_radix_sort(v.begin(), v.end());
  if (v != expected) {
    std::fprintf(stderr, "FAIL: 128-bit sort mismatch\n");
    return false;
  }
  return true;
}

template <typename T>
bool validate128_descending(std::vector<T> v)
{
  auto expected = v;
  std::stable_sort(expected.begin(), expected.end(), std::greater<T> {});
  stdex::stable_radix_sort<kDescOpts>(v.begin(), v.end());
  if (v != expected) {
    std::fprintf(stderr, "FAIL: 128-bit descending sort mismatch\n");
    return false;
  }
  return true;
}

static void test_uint128_keys()
{
  TEST_RADIX(Uint128Keys);
  auto v = generate128<stdex::uint128_t>(700);
  // Add extremes and duplicates so both the sign/unsigned boundary and stable
  // ordering are exercised.
  v.push_back(stdex::uint128_t {});
  v.push_back(~stdex::uint128_t {});
  v.push_back(stdex::uint128_t::from_u64(1));
  v.push_back(stdex::uint128_t::from_words(0, 0x8000'0000'0000'0000ULL));
  v.push_back(v.front());

  if (!validate128(v)) {
    FAIL_MSG("uint128 ascending");
    return;
  }
  if (!validate128_descending(v)) {
    FAIL_MSG("uint128 descending");
    return;
  }

  constexpr auto kMsdOpts = [] {
    return stdex::radix_sort_options {
      .sort_strategy    = stdex::radix_sort_strategy::msd,
      .execution_policy = stdex::radix_execution_policy::seq
    };
  }();
  auto msd = v;
  auto expected = v;
  std::stable_sort(expected.begin(), expected.end());
  stdex::stable_radix_sort<kMsdOpts>(msd.begin(), msd.end());
  if (msd != expected) {
    FAIL_MSG("uint128 msd");
    return;
  }

  constexpr auto kBits16Opts = [] {
    return stdex::radix_sort_options {
      .sort_strategy    = stdex::radix_sort_strategy::lsd,
      .execution_policy = stdex::radix_execution_policy::seq,
      .bits_per_pass    = stdex::radix_bits_per_pass::bits16
    };
  }();
  auto bits16 = v;
  stdex::stable_radix_sort<kBits16Opts>(bits16.begin(), bits16.end());
  if (bits16 != expected) {
    FAIL_MSG("uint128 bits16");
    return;
  }
  PASS();
}

static void test_int128_keys()
{
  TEST_RADIX(Int128Keys);
  auto v = generate128<stdex::int128_t>(700);
  v.push_back(stdex::int128_t::from_i64(-1));
  v.push_back(stdex::int128_t::from_i64(0));
  v.push_back(stdex::int128_t::from_i64(1));
  v.push_back(std::numeric_limits<stdex::int128_t>::min());
  v.push_back(std::numeric_limits<stdex::int128_t>::max());

  if (!validate128(v)) {
    FAIL_MSG("int128 ascending");
    return;
  }
  if (!validate128_descending(v)) {
    FAIL_MSG("int128 descending");
    return;
  }

  constexpr auto kMsdOpts = [] {
    return stdex::radix_sort_options {
      .sort_strategy    = stdex::radix_sort_strategy::msd,
      .execution_policy = stdex::radix_execution_policy::seq
    };
  }();
  auto msd      = v;
  auto expected = v;
  std::stable_sort(expected.begin(), expected.end());
  stdex::stable_radix_sort<kMsdOpts>(msd.begin(), msd.end());
  if (msd != expected) {
    FAIL_MSG("int128 msd");
    return;
  }
  PASS();
}

static void test_int128_projection()
{
  TEST_RADIX(Int128Projection);
  struct Record
  {
    stdex::uint128_t key;
    int tag;
    bool operator== (Record const &) const = default;
  };

  std::vector<Record> records;
  for (int i = 0; i < 400; ++i) {
    records.push_back({ stdex::uint128_t::from_words(rng(), rng() >> (i % 4 == 0 ? 32U : 0U)), i });
  }
  auto expected = records;
  std::stable_sort(expected.begin(), expected.end(), [](Record const &a, Record const &b) {
    return a.key < b.key;
  });
  stdex::stable_radix_sort(records.begin(), records.end(), [](Record const &r) { return r.key; });
  if (records != expected) {
    FAIL_MSG("uint128 projection");
    return;
  }
  PASS();
}

// ============================================================================
// main
// ============================================================================
int main()
{
  std::printf("=== stable_radix_sort tests ===\n\n");

  test_empty();
  test_single_element();
  test_already_sorted();
  test_reverse_sorted();
  test_all_equal();
  test_small_array_below_threshold();
  test_unsigned_integers();
  test_signed_integers();
  test_signed_extremes();
  test_float_random();
  test_float_special_values();
  test_float_nan_positions();
  test_negative_values_all_paths();
#ifdef _OPENMP
  test_parallel_pass_count();
#endif
  test_descending();
  test_key_extractor();
  test_stability();
  test_large_input();
  test_small_input();
  test_chunk_bits16();
  test_counter_type64();
  test_insertion_threshold();
  test_all_same_bucket();
  test_early_exit_sorted();
  test_custom_projection();
  test_reverse_iterator();
  test_single_byte();
  test_char_types();
  test_wide_char_types();
  test_msd_seq();
  test_descending_counter64();
  test_descending_chunk16();
  test_msd_bits16();
  test_bool_type();
  test_vector_bool();
  test_signed_zero();
  test_fancy_pointer_allocator();

  test_uint128_keys();
  test_int128_keys();
  test_int128_projection();

  std::printf("\n=== Results: %d/%d passed ===\n", g_tests_passed, g_tests_run);
  return g_tests_passed == g_tests_run ? 0 : 1;
}
