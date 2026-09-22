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

// bench_hash_table.cpp
/// \file Flat-map comparison: std (baseline) vs boost vs absl vs stdex.
///
/// Baseline is std::unordered_map, registered first in every group.
/// Fairness: all four maps run the identical key sets (fixed seeds), the same
/// element counts, std::allocator throughout, reserve(N) for every map in
/// steady-state cases and no reserve for any map in growth cases. Only the
/// timed operation runs inside the benchmark loop; container fill/teardown
/// stays in PauseTiming. Hash functions necessarily differ by library design
/// (only stdex shares std::hash with the baseline); key order and workload
/// are what is held equal. Set is out of scope.
///
/// Groups Int/Nano, Int/Micro, Str/Nano and Str/Micro pair stdex with the
/// nano/micro hash modes; Boost keeps its own default hash in both groups.
/// Run (Release): bench_hash_table.exe [--benchmark_filter='Int/.*']
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include <benchmark/benchmark.h>

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/functional/hash.hpp>

#include <absl/container/flat_hash_map.h>

#include "hash.hpp"
#include "unordered_flat_map.hpp"

namespace
{
constexpr std::size_t kIntN = 100000;
constexpr std::size_t kStrN = 20000;

std::vector<int> shuffled_ints(std::size_t begin, std::size_t count, unsigned seed)
{
  std::vector<int> keys(count);
  std::iota(keys.begin(), keys.end(), static_cast<int>(begin));
  std::shuffle(keys.begin(), keys.end(), std::mt19937 { seed });
  return keys;
}

std::vector<std::string> numbered_strings(char const *prefix, std::size_t count)
{
  std::vector<std::string> keys;
  keys.reserve(count);
  for (std::size_t i = 0; i < count; ++i) keys.emplace_back(std::string(prefix) + std::to_string(i));
  return keys;
}

struct IntData
{
  std::vector<int> keys  = shuffled_ints(0, kIntN, 42);
  std::vector<int> miss  = shuffled_ints(kIntN, kIntN, 43);
  std::vector<int> extra = shuffled_ints(2 * kIntN, kIntN / 2, 44);
};

struct StrData
{
  std::vector<std::string> keys  = numbered_strings("key_", kStrN);
  std::vector<std::string> miss  = numbered_strings("miss_", kStrN);
  std::vector<std::string> extra = numbered_strings("extra_", kStrN / 2);
};

IntData const &int_data()
{
  static IntData const data = [] {
    IntData d;
    std::shuffle(d.keys.begin(), d.keys.end(), std::mt19937 { 42 });
    return d;
  }();
  return data;
}

StrData const &str_data()
{
  static StrData const data = [] {
    StrData d;
    std::shuffle(d.keys.begin(), d.keys.end(), std::mt19937 { 42 });
    return d;
  }();
  return data;
}

template<class T>
struct hahser
{
  using is_avalanching = void;

  size_t operator()(T const& value) const noexcept
  {
    return std::hash<T>{}(value);
  }
};

struct stdex_nano_int
{
  size_t operator() (int const value) const noexcept
  {
    return static_cast<size_t>(stdex::hash_value<int, stdex::nano_t>(value, 0u));
  }
};

struct stdex_micro_int
{
  size_t operator() (int const value) const noexcept
  {
    return static_cast<size_t>(stdex::hash_value<int, stdex::micro_t>(value, 0u));
  }
};

struct stdex_nano_str
{
  size_t operator() (std::string const &value) const noexcept
  {
    return static_cast<size_t>(stdex::hash_range<std::string, stdex::nano_t>(value, 0u));
  }
};

struct stdex_micro_str
{
  size_t operator() (std::string const &value) const noexcept
  {
    return static_cast<size_t>(stdex::hash_range<std::string, stdex::micro_t>(value, 0u));
  }
};

using StdIntMap   = std::unordered_map<int, int>;
using BoostIntMap = boost::unordered::unordered_flat_map<int, int>;
using AbslIntMap  = absl::flat_hash_map<int, int>;
using StdexIntMap = stdex::unordered_flat_map<int, int>;

using StdStrMap   = std::unordered_map<std::string, int>;
using BoostStrMap = boost::unordered::unordered_flat_map<std::string, int>;
using AbslStrMap  = absl::flat_hash_map<std::string, int>;
using StdexStrMap = stdex::unordered_flat_map<std::string, int>;

template <class Map, class Key>
void BM_Insert(benchmark::State &state, std::vector<Key> const &keys)
{
  for (auto _ : state) {
    Map m;
    for (std::size_t i = 0; i < keys.size(); ++i) m.emplace(keys[i], static_cast<int>(i));
    benchmark::DoNotOptimize(m.size());
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * keys.size()));
}

template <class Map, class Key>
void fill_reserved(Map &m, std::vector<Key> const &keys)
{
  m.reserve(keys.size());
  for (std::size_t i = 0; i < keys.size(); ++i) m.emplace(keys[i], static_cast<int>(i));
}

template <class Map, class Key>
void BM_FindHit(benchmark::State &state, std::vector<Key> const &keys)
{
  for (auto _ : state) {
    state.PauseTiming();
    Map m;
    fill_reserved(m, keys);
    state.ResumeTiming();
    std::int64_t sum = 0;
    for (auto const &k : keys) {
      auto it = m.find(k);
      if (it != m.end()) sum += it->second;
    }
    benchmark::DoNotOptimize(sum);
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * keys.size()));
}

template <class Map, class Key>
void BM_FindMiss(benchmark::State &state, std::vector<Key> const &keys, std::vector<Key> const &miss)
{
  for (auto _ : state) {
    state.PauseTiming();
    Map m;
    fill_reserved(m, keys);
    state.ResumeTiming();
    std::int64_t found = 0;
    for (auto const &k : miss) found += (m.find(k) != m.end());
    benchmark::DoNotOptimize(found);
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * miss.size()));
}

template <class Map, class Key>
void BM_Update(benchmark::State &state, std::vector<Key> const &keys)
{
  for (auto _ : state) {
    state.PauseTiming();
    Map m;
    fill_reserved(m, keys);
    state.ResumeTiming();
    for (std::size_t i = 0; i < keys.size(); ++i) m[keys[i]] = static_cast<int>(i) + 1;
    benchmark::DoNotOptimize(m.size());
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * keys.size()));
}

template <class Map, class Key>
void BM_Erase(benchmark::State &state, std::vector<Key> const &keys)
{
  for (auto _ : state) {
    state.PauseTiming();
    Map m;
    fill_reserved(m, keys);
    state.ResumeTiming();
    for (auto const &k : keys) m.erase(k);
    benchmark::DoNotOptimize(m.size());
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * keys.size()));
}

template <class Map, class Key>
void BM_Rehash(benchmark::State &state, std::vector<Key> const &keys)
{
  for (auto _ : state) {
    state.PauseTiming();
    Map m;
    fill_reserved(m, keys);
    state.ResumeTiming();
    m.rehash(m.size() * 4);
    benchmark::DoNotOptimize(m.bucket_count());
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * keys.size()));
}

template <class Map, class Key>
void BM_Churn(benchmark::State &state, std::vector<Key> const &keys, std::vector<Key> const &extra)
{
  std::size_t const half = keys.size() / 2;
  for (auto _ : state) {
    state.PauseTiming();
    Map m;
    m.reserve(keys.size() + extra.size());
    fill_reserved(m, keys);
    state.ResumeTiming();
    for (std::size_t i = 0; i < half; ++i) m.erase(keys[i]);
    for (std::size_t i = 0; i < extra.size(); ++i) m.emplace(extra[i], static_cast<int>(i));
    benchmark::DoNotOptimize(m.size());
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * (half + extra.size())));
}

template <class Map, class Data>
void register_group(char const *group, Data const &data)
{
  using State = benchmark::State;
  benchmark::RegisterBenchmark(std::string(group) + "/Insert", [&](State &s) { BM_Insert<Map>(s, data.keys); })
      ->Unit(benchmark::kMillisecond);
  benchmark::RegisterBenchmark(std::string(group) + "/FindHit", [&](State &s) { BM_FindHit<Map>(s, data.keys); })
      ->Unit(benchmark::kMillisecond);
  benchmark::RegisterBenchmark(std::string(group) + "/FindMiss",
                               [&](State &s) { BM_FindMiss<Map>(s, data.keys, data.miss); })
      ->Unit(benchmark::kMillisecond);
  benchmark::RegisterBenchmark(std::string(group) + "/Update", [&](State &s) { BM_Update<Map>(s, data.keys); })
      ->Unit(benchmark::kMillisecond);
  benchmark::RegisterBenchmark(std::string(group) + "/Erase", [&](State &s) { BM_Erase<Map>(s, data.keys); })
      ->Unit(benchmark::kMillisecond);
  benchmark::RegisterBenchmark(std::string(group) + "/Rehash", [&](State &s) { BM_Rehash<Map>(s, data.keys); })
      ->Unit(benchmark::kMillisecond);
  benchmark::RegisterBenchmark(std::string(group) + "/Churn",
                               [&](State &s) { BM_Churn<Map>(s, data.keys, data.extra); })
      ->Unit(benchmark::kMillisecond);
}

struct Registrar
{
  Registrar()
  {
    //register_group<StdIntMap>("Int/Std", int_data());
    register_group<BoostIntMap>("Int/Boost", int_data());
    //register_group<AbslIntMap>("Int/Absl", int_data());
    register_group<StdexIntMap>("Int/Stdex", int_data());
    //register_group<StdStrMap>("Str/Std", str_data());
    register_group<BoostStrMap>("Str/Boost", str_data());
    //register_group<AbslStrMap>("Str/Absl", str_data());
    register_group<StdexStrMap>("Str/Stdex", str_data());
  }
} g_registrar;
} // namespace
