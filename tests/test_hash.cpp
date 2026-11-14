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

// test_hash.cpp
//
// Functional and statistical checks for stdex::hash / hash_range.
// All statistical inputs are generated from fixed seeds so failures are
// reproducible; quality thresholds are deliberately below the ideal values.
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "hash.hpp"

namespace
{
int g_pass = 0;
int g_fail = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (cond) {                                                              \
      ++g_pass;                                                              \
    } else {                                                                 \
      ++g_fail;                                                              \
      if (g_fail < 20) {                                                     \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
      }                                                                      \
    }                                                                        \
  } while (0)

using u64 = std::uint64_t;
using u32 = std::uint32_t;

constexpr std::size_t kSampleKeys = 100'000;
constexpr std::size_t kDistributionBuckets = 4096;

std::uint64_t next_rand(std::uint64_t &state)
{
  std::uint64_t z = (state += 0x9E'37'79'B9'7F'4A'7C'15ULL);
  z               = (z ^ (z >> 30U)) * 0xBF'58'47'6D'1C'E4'E5'B9ULL;
  z               = (z ^ (z >> 27U)) * 0x94'D0'49'BB'13'31'11'EBULL;
  return z ^ (z >> 31U);
}

std::vector<u64> make_keys(std::size_t count, std::uint64_t seed)
{
  std::vector<u64> keys(count);
  std::uint64_t state = seed;
  for (u64 &key : keys) key = next_rand(state);
  return keys;
}

template <class T, class Fn>
double avalanche_score(Fn fn, int samples)
{
  std::mt19937_64 rng(0x1234'5678'9ABC'DEF0ULL);
  double total = 0.0;
  constexpr unsigned width = sizeof(T) * 8U;
  for (int sample = 0; sample < samples; ++sample) {
    T const base = static_cast<T>(rng());
    auto const base_hash = fn(base);
    for (unsigned bit = 0; bit < width; ++bit) {
      T const changed = static_cast<T>(base ^ (T { 1 } << bit));
      total += static_cast<double>(std::popcount(base_hash ^ fn(changed)));
    }
  }
  return total / (samples * static_cast<double>(width));
}

template <class Fn>
double avalanche_score_128(Fn fn, int samples)
{
  std::mt19937_64 rng(0x0FED'CBA9'8765'4321ULL);
  double total = 0.0;
  for (int sample = 0; sample < samples; ++sample) {
    stdex::uint128_t const base { rng(), rng() };
    auto const base_hash = fn(base);
    for (unsigned bit = 0; bit < 64U; ++bit) {
      stdex::uint128_t const low_changed { base.lo() ^ (u64 { 1 } << bit), base.hi() };
      stdex::uint128_t const high_changed { base.lo(), base.hi() ^ (u64 { 1 } << bit) };
      total += static_cast<double>(std::popcount(base_hash ^ fn(low_changed)));
      total += static_cast<double>(std::popcount(base_hash ^ fn(high_changed)));
    }
  }
  return total / (samples * 128.0);
}

struct distribution_result
{
  double chi_square {};
  std::size_t max_bucket {};
  std::size_t duplicate_count {};
};

template <class Fn>
distribution_result measure_distribution(std::vector<u64> const &keys, Fn fn)
{
  constexpr std::size_t bucket_count = kDistributionBuckets;
  std::vector<std::size_t> buckets(bucket_count, 0);
  std::vector<u64> hashes;
  hashes.reserve(keys.size());
  for (u64 const key : keys) {
    u64 const hash = fn(key);
    hashes.push_back(hash);
    constexpr unsigned width = sizeof(std::size_t) * 8U;
    constexpr unsigned bucket_bits = 12U;
    static_assert((kDistributionBuckets & (kDistributionBuckets - 1U)) == 0, "bucket count must be a power of two");
    static_assert(kDistributionBuckets == (std::size_t { 1 } << bucket_bits));
    auto const bucket = static_cast<std::size_t>((hash >> (width - bucket_bits)) & (bucket_count - 1U));
    buckets[bucket]++;
  }

  double const mean = static_cast<double>(keys.size()) / bucket_count;
  double chi_square = 0.0;
  for (std::size_t const count : buckets) {
    double const delta = count - mean;
    chi_square += delta * delta / mean;
  }

  std::sort(hashes.begin(), hashes.end());
  std::size_t duplicates = 0;
  for (std::size_t i = 1; i < hashes.size(); ++i) duplicates += hashes[i] == hashes[i - 1];
  return { chi_square, *std::max_element(buckets.begin(), buckets.end()), duplicates };
}

void check_distribution(char const *name, std::vector<u64> const &keys, u64 (*fn)(u64))
{
  distribution_result const result = measure_distribution(keys, fn);
  double const df = static_cast<double>(kDistributionBuckets - 1);
  std::printf("  %-24s chi2=%8.1f (df=%.0f) max=%zu dup=%zu\n", name, result.chi_square, df, result.max_bucket, result.duplicate_count);
  CHECK(result.chi_square < df * 1.6);
  CHECK(result.max_bucket < (keys.size() / kDistributionBuckets) * 4 + 8);
  if constexpr (sizeof(std::size_t) >= sizeof(u64)) {
    CHECK(result.duplicate_count == 0);
  }
}

void test_scalar_contract()
{
  std::printf("\n=== hash: scalar contract ===\n");

  CHECK(stdex::hash<int> {}(42) == stdex::hash<int> {}(42));
  CHECK(stdex::hash<int> {}(42) != stdex::hash<int> {}(43));
  CHECK(stdex::hash<int> {}(42) != stdex::hash<int> {}(42, 1));
  CHECK(stdex::hash<int> {}(42, 1) != stdex::hash<int> {}(42, 2));
  CHECK(stdex::hash<std::int64_t> {}(-1) == stdex::hash<std::uint64_t> {}(0xFFFF'FFFF'FFFF'FFFFULL));
  CHECK(stdex::hash<std::int32_t> {}(-1) == stdex::hash<std::uint32_t> {}(0xFFFF'FFFFU));
  CHECK(stdex::hash<bool> {}(true) != stdex::hash<bool> {}(false));
  CHECK(stdex::hash<double> {}(0.0) == stdex::hash<double> {}(-0.0));
  CHECK(stdex::hash<double> {}(1.5) == stdex::hash<double> {}(1.5));

  CHECK(stdex::seeded_hash<int> {42}(42) == stdex::hash<int> {}(42, 42));
  CHECK(stdex::seeded_hash<int> {42} == stdex::seeded_hash<int> {42});
  CHECK(stdex::seeded_hash<int> {42} != stdex::seeded_hash<int> {43});
  CHECK(stdex::seeded_hash<std::nullptr_t> {7}(nullptr) == stdex::hash<std::nullptr_t> {}(nullptr, 7));
}

void test_strings_and_ranges()
{
  std::printf("\n=== hash: strings and ranges ===\n");

  std::string const text = "the quick brown fox jumps over the lazy dog";
  std::string_view const view = text;
  CHECK(stdex::hash<std::string> {}(text) == stdex::hash_detail::to_size_t(stdex::hash_range<std::string>(text)));
  CHECK(stdex::hash<std::string_view> {}(view) == stdex::hash_detail::to_size_t(stdex::hash_range<std::string_view>(view)));
  CHECK(stdex::hash<std::string> {}(text, 17) == stdex::hash_detail::to_size_t(stdex::hash_range<std::string>(text, 17)));

  std::array<std::byte, 200> bytes {};
  for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::byte>(i * 37U + 11U);
  CHECK(stdex::hash_range(bytes) == stdex::hash_bytes(std::span<std::byte const> { bytes }));
  CHECK((stdex::hash_range<decltype(bytes), stdex::hash_micro_t>(bytes) != stdex::hash_range(bytes)));
  CHECK((stdex::hash_range<decltype(bytes), stdex::hash_nano_t>(bytes) != stdex::hash_range(bytes)));
}

void test_128()
{
  std::printf("\n=== hash: 128-bit values ===\n");

  CHECK((stdex::hash<stdex::uint128_t> {}(stdex::uint128_t { 1, 2 }) !=
         stdex::hash<stdex::uint128_t> {}(stdex::uint128_t { 2, 1 })));
  CHECK(stdex::hash<stdex::int128_t> {}(stdex::int128_t::from_i64(-1)) ==
        stdex::hash<stdex::uint128_t> {}(~stdex::uint128_t {}));
  CHECK((stdex::hash<stdex::uint128_t> {}(stdex::uint128_t { 1, 2 }, 1) !=
         stdex::hash<stdex::uint128_t> {}(stdex::uint128_t { 1, 2 })));

  constexpr std::size_t count = sizeof(std::size_t) >= sizeof(u64) ? 4096 : 1024;
  std::vector<u64> hashes;
  hashes.reserve(count * 2);
  for (std::uint64_t i = 1; i < count; ++i) {
    hashes.push_back(stdex::hash<stdex::uint128_t> {}(stdex::uint128_t { i, 0 }));
    hashes.push_back(stdex::hash<stdex::uint128_t> {}(stdex::uint128_t { 0, i }));
  }
  std::sort(hashes.begin(), hashes.end());
  CHECK(std::adjacent_find(hashes.begin(), hashes.end()) == hashes.end());
}

void test_avalanche()
{
  std::printf("\n=== hash: avalanche ===\n");

  double const expected = static_cast<double>(sizeof(std::size_t) * 8U) / 2.0;
  auto const scalar = [](u64 value) { return static_cast<u64>(stdex::hash<u64> {}(value)); };
  auto const scalar32 = [](u32 value) { return static_cast<std::size_t>(stdex::hash<u32> {}(value)); };
  auto const wide = [](stdex::uint128_t value) { return static_cast<std::size_t>(stdex::hash<stdex::uint128_t> {}(value)); };

  double const score64 = avalanche_score<u64>(scalar, 128);
  double const score32 = avalanche_score<u32>(scalar32, 128);
  double const score128 = avalanche_score_128(wide, 128);
  std::printf("  hash<uint64_t>=%.2f  hash<uint32_t>=%.2f  hash<uint128_t>=%.2f  ideal=%.2f\n",
              score64, score32, score128, expected);
  CHECK(score64 > expected * 0.85);
  CHECK(score32 > expected * 0.85);
  CHECK(score128 > expected * 0.85);
}

void test_optional_finalizer()
{
  std::printf("\n=== hash: optional finalizer ===\n");

  auto const finalized = [](u64 value) {
    std::size_t hash = stdex::hash<u64> {}(value);
    if constexpr (sizeof(std::size_t) == 8) {
      return static_cast<std::size_t>(hash * stdex::kPiMixMultConstant);
    } else {
      return static_cast<std::size_t>(hash * stdex::kPiMixMultConstant32);
    }
  };
  double const score = avalanche_score<u64>(finalized, 128);
  double const expected = static_cast<double>(sizeof(std::size_t) * 8U) / 2.0;
  std::printf("  hash<uint64_t> * kPiMixMultConstant=%.2f  ideal=%.2f\n", score, expected);
  CHECK(score > expected * 0.90);
}

void test_bit_balance()
{
  std::printf("\n=== hash: bit balance ===\n");

  std::vector<u64> const keys = make_keys(kSampleKeys, 0x5EED'1234'ABCD'9876ULL);
  constexpr unsigned width = sizeof(std::size_t) * 8U;
  std::array<std::size_t, width> ones {};
  for (u64 const key : keys) {
    std::size_t const hash = stdex::hash<u64> {}(key);
    for (unsigned bit = 0; bit < width; ++bit) ones[bit] += ((hash >> bit) & 1U) != 0;
  }

  double const expected = static_cast<double>(keys.size()) / 2.0;
  double min_ratio = 1.0;
  double max_ratio = 0.0;
  for (unsigned bit = 0; bit < width; ++bit) {
    double const ratio = ones[bit] / expected;
    min_ratio = std::min(min_ratio, ratio);
    max_ratio = std::max(max_ratio, ratio);
  }
  std::printf("  bit-one ratio range=%.3f..%.3f\n", min_ratio, max_ratio);
  CHECK(min_ratio > 0.93);
  CHECK(max_ratio < 1.07);
}

void test_distribution()
{
  std::printf("\n=== hash: distribution ===\n");

  std::vector<u64> sequential(kSampleKeys);
  for (std::size_t i = 0; i < sequential.size(); ++i) sequential[i] = i;
  std::vector<u64> arithmetic(kSampleKeys);
  for (std::size_t i = 0; i < arithmetic.size(); ++i) arithmetic[i] = static_cast<u64>(i) * 0x9E'37'79'B9'7F'4A'7C'15ULL;
  std::vector<u64> random = make_keys(kSampleKeys, 0xC0FF'EE12'3456'789AULL);

  auto const hash64 = [](u64 value) { return static_cast<u64>(stdex::hash<u64> {}(value)); };
  check_distribution("hash<uint64_t>/sequential", sequential, hash64);
  check_distribution("hash<uint64_t>/arithmetic", arithmetic, hash64);
  check_distribution("hash<uint64_t>/random", random, hash64);

  auto const hash32 = [](u64 value) { return static_cast<u64>(stdex::hash<u32> {}(static_cast<u32>(value))); };
  check_distribution("hash<uint32_t>/sequential", sequential, hash32);
}

} // namespace

int main()
{
  test_scalar_contract();
  test_strings_and_ranges();
  test_128();
  test_avalanche();
  test_optional_finalizer();
  test_bit_balance();
  test_distribution();

  std::printf("\n===============================\n");
  std::printf("PASS: %d   FAIL: %d\n", g_pass, g_fail);
  std::printf("===============================\n");
  return g_fail == 0 ? 0 : 1;
}
