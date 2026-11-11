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

// bench_hash.cpp
//
// Compares stdex::hash against std::hash for the same scalar/string key sets.
// The benchmark measures only the hash call; key construction and storage are
// outside the timed loop. Run (Release):
//   bench_hash.exe [--benchmark_filter='BM_Int.*']
#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "hash.hpp"

namespace
{
constexpr std::size_t kSamples = 1U << 16U;

std::vector<int> make_int_keys(bool random)
{
  std::vector<int> keys(kSamples);
  if (random) {
    std::mt19937 rng(42);
    for (int &key : keys) key = static_cast<int>(rng());
  } else {
    for (std::size_t i = 0; i < keys.size(); ++i) keys[i] = static_cast<int>(i);
  }
  return keys;
}

std::vector<std::uint64_t> make_u64_keys(bool random)
{
  std::vector<std::uint64_t> keys(kSamples);
  if (random) {
    std::mt19937_64 rng(42);
    for (std::uint64_t &key : keys) key = rng();
  } else {
    for (std::size_t i = 0; i < keys.size(); ++i) keys[i] = static_cast<std::uint64_t>(i) * 0x9E'37'79'B9'7F'4A'7C'15ULL;
  }
  return keys;
}

std::vector<std::string> make_string_keys()
{
  std::vector<std::string> keys;
  keys.reserve(kSamples);
  for (std::size_t i = 0; i < kSamples; ++i) keys.emplace_back("key-" + std::to_string(i) + "-payload");
  return keys;
}

template <class Hasher, class Key>
void hash_loop(benchmark::State &state, std::vector<Key> const &keys)
{
  std::size_t const mask = keys.size() - 1U;
  Hasher hasher;
  std::size_t index = 0;
  std::uint64_t sink = 0;
  for (auto _ : state) {
    sink ^= static_cast<std::uint64_t>(hasher(keys[index & mask]));
    ++index;
  }
  benchmark::DoNotOptimize(sink);
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()));
}

void BM_IntStdex(benchmark::State &state, bool random)
{
  static auto const sequential = make_int_keys(false);
  static auto const shuffled = make_int_keys(true);
  hash_loop<stdex::hash<int>>(state, random ? shuffled : sequential);
}

void BM_IntStd(benchmark::State &state, bool random)
{
  static auto const sequential = make_int_keys(false);
  static auto const shuffled = make_int_keys(true);
  hash_loop<std::hash<int>>(state, random ? shuffled : sequential);
}

void BM_U64Stdex(benchmark::State &state, bool random)
{
  static auto const sequential = make_u64_keys(false);
  static auto const shuffled = make_u64_keys(true);
  hash_loop<stdex::hash<std::uint64_t>>(state, random ? shuffled : sequential);
}

void BM_U64Std(benchmark::State &state, bool random)
{
  static auto const sequential = make_u64_keys(false);
  static auto const shuffled = make_u64_keys(true);
  hash_loop<std::hash<std::uint64_t>>(state, random ? shuffled : sequential);
}

void BM_StringStdex(benchmark::State &state)
{
  static auto const keys = make_string_keys();
  hash_loop<stdex::hash<std::string>>(state, keys);
}

void BM_StringStd(benchmark::State &state)
{
  static auto const keys = make_string_keys();
  hash_loop<std::hash<std::string>>(state, keys);
}

void BM_StringViewStdex(benchmark::State &state)
{
  static auto const strings = make_string_keys();
  static auto const keys = [] {
    std::vector<std::string_view> views;
    views.reserve(strings.size());
    for (auto const &string : strings) views.push_back(string);
    return views;
  }();
  hash_loop<stdex::hash<std::string_view>>(state, keys);
}

void BM_StringViewStd(benchmark::State &state)
{
  static auto const strings = make_string_keys();
  static auto const keys = [] {
    std::vector<std::string_view> views;
    views.reserve(strings.size());
    for (auto const &string : strings) views.push_back(string);
    return views;
  }();
  hash_loop<std::hash<std::string_view>>(state, keys);
}
} // namespace

BENCHMARK_CAPTURE(BM_IntStdex, sequential, false)->Unit(benchmark::kNanosecond);
BENCHMARK_CAPTURE(BM_IntStdex, random, true)->Unit(benchmark::kNanosecond);
BENCHMARK_CAPTURE(BM_IntStd, sequential, false)->Unit(benchmark::kNanosecond);
BENCHMARK_CAPTURE(BM_IntStd, random, true)->Unit(benchmark::kNanosecond);

BENCHMARK_CAPTURE(BM_U64Stdex, sequential, false)->Unit(benchmark::kNanosecond);
BENCHMARK_CAPTURE(BM_U64Stdex, random, true)->Unit(benchmark::kNanosecond);
BENCHMARK_CAPTURE(BM_U64Std, sequential, false)->Unit(benchmark::kNanosecond);
BENCHMARK_CAPTURE(BM_U64Std, random, true)->Unit(benchmark::kNanosecond);

BENCHMARK(BM_StringStdex)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_StringStd)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_StringViewStdex)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_StringViewStd)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
