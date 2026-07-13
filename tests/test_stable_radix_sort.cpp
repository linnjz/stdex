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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <numeric>
#include <random>
#include <string>
#include <vector>

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

struct Item
{
  double score;
  int originalIndex;
  bool operator== (Item const &) const = default;
};

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
  for (auto n : { 63, 64, 65 }) {
    std::vector<uint32_t> v(n);
    std::iota(v.begin(), v.end(), uint32_t(0));
    std::shuffle(v.begin(), v.end(), rng);
    if (!validate(v)) {
      FAIL_MSG("uint32 threshold boundary");
      return;
    }
  }
  for (auto n : { 63, 64, 65 }) {
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
  {
    std::vector<uint64_t> v(10000);
    std::iota(v.begin(), v.end(), uint64_t(0));
    validate(v);
  }
  {
    std::vector<int64_t> v(10000);
    std::iota(v.begin(), v.end(), int64_t(-5000));
    validate(v);
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

  std::printf("\n=== Results: %d/%d passed ===\n", g_tests_passed, g_tests_run);
  return g_tests_passed == g_tests_run ? 0 : 1;
}
