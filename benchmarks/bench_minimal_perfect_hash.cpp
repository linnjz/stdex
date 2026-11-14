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

// bench_minimal_perfect_hash.cpp
/// \file Immutable-map/set lookup comparison: minimal_perfect vs unordered_flat vs frozen.
///
/// All containers use the same fixed key sets and values. Construction happens
/// once, outside the timed regions; only hit/miss lookups and at() are timed.
/// Hash implementations differ by library design: frozen receives a seeded
/// stdex hash, while the stdex containers use their internal seeded hash path;
/// compare relative lookup cost rather than treating the hash work as identical.
///
/// Run (Release): bench_minimal_perfect_hash.exe [--benchmark_filter='Int/.*']
#include <algorithm>
#include <array>
#include <cstdint>
#include <numeric>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include <frozen/unordered_map.h>
#include <frozen/unordered_set.h>

#include "hash.hpp"
#include "minimal_perfect_hash_map.hpp"
#include "minimal_perfect_hash_set.hpp"
#include "unordered_flat_map.hpp"
#include "unordered_flat_set.hpp"

namespace
{
// frozen's PMH builder keeps its bucket table on the stack, so keep the
// comparison size modest; lookup cost is still dominated by the same probe
// and key-equality work that matters for larger tables.
constexpr std::size_t kIntCount    = 1024;
constexpr std::size_t kStringCount = 1024;

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

template <class Key>
struct frozen_seeded_hash
{
  std::size_t operator() (Key const &key, std::size_t seed) const noexcept
  {
    return static_cast<std::size_t>(stdex::hash<Key> {}(key, static_cast<std::uint64_t>(seed)));
  }
};

template <std::size_t N>
std::array<std::pair<int, int>, N> make_int_entries(std::vector<int> const &keys)
{
  std::array<std::pair<int, int>, N> entries {};
  for (std::size_t i = 0; i < N; ++i) entries[i] = { keys[i], static_cast<int>(i) };
  return entries;
}

template <std::size_t N>
std::array<int, N> make_int_set_entries(std::vector<int> const &keys)
{
  std::array<int, N> entries {};
  for (std::size_t i = 0; i < N; ++i) entries[i] = keys[i];
  return entries;
}

template <std::size_t N>
std::array<std::pair<std::string, int>, N> make_string_entries(std::vector<std::string> const &keys)
{
  std::array<std::pair<std::string, int>, N> entries {};
  for (std::size_t i = 0; i < N; ++i) entries[i] = { keys[i], static_cast<int>(i) };
  return entries;
}

template <std::size_t N>
std::array<std::string, N> make_string_set_entries(std::vector<std::string> const &keys)
{
  std::array<std::string, N> entries {};
  for (std::size_t i = 0; i < N; ++i) entries[i] = keys[i];
  return entries;
}

using FrozenIntMap    = frozen::unordered_map<int, int, kIntCount, frozen_seeded_hash<int>>;
using FrozenIntSet    = frozen::unordered_set<int, kIntCount, frozen_seeded_hash<int>>;
using FrozenStringMap = frozen::unordered_map<std::string, int, kStringCount, frozen_seeded_hash<std::string>>;
using FrozenStringSet = frozen::unordered_set<std::string, kStringCount, frozen_seeded_hash<std::string>>;

struct IntData
{
  std::vector<int> keys                                  = shuffled_ints(0, kIntCount, 42);
  std::vector<int> miss                                  = shuffled_ints(kIntCount, kIntCount, 43);
  std::array<std::pair<int, int>, kIntCount> map_entries = make_int_entries<kIntCount>(keys);
  std::array<int, kIntCount> set_entries                 = make_int_set_entries<kIntCount>(keys);

  stdex::minimal_perfect_hash_map<int, int> minimal_map { map_entries.begin(), map_entries.end() };
  stdex::unordered_flat_map<int, int> flat_map { map_entries.begin(), map_entries.end() };
  FrozenIntMap frozen_map { frozen::make_unordered_map(map_entries, frozen_seeded_hash<int> {}, std::equal_to<int> {}) };

  stdex::minimal_perfect_hash_set<int> minimal_set { set_entries.begin(), set_entries.end() };
  stdex::unordered_flat_set<int> flat_set { set_entries.begin(), set_entries.end() };
  FrozenIntSet frozen_set { frozen::make_unordered_set(set_entries, frozen_seeded_hash<int> {}, std::equal_to<int> {}) };
};

struct StringData
{
  std::vector<std::string> keys                                     = numbered_strings("key_", kStringCount);
  std::vector<std::string> miss                                     = numbered_strings("miss_", kStringCount);
  std::array<std::pair<std::string, int>, kStringCount> map_entries = make_string_entries<kStringCount>(keys);
  std::array<std::string, kStringCount> set_entries                 = make_string_set_entries<kStringCount>(keys);

  stdex::minimal_perfect_hash_map<std::string, int> minimal_map { map_entries.begin(), map_entries.end() };
  stdex::unordered_flat_map<std::string, int> flat_map { map_entries.begin(), map_entries.end() };
  FrozenStringMap frozen_map {
    frozen::make_unordered_map(map_entries, frozen_seeded_hash<std::string> {}, std::equal_to<std::string> {})
  };

  stdex::minimal_perfect_hash_set<std::string> minimal_set { set_entries.begin(), set_entries.end() };
  stdex::unordered_flat_set<std::string> flat_set { set_entries.begin(), set_entries.end() };
  FrozenStringSet frozen_set {
    frozen::make_unordered_set(set_entries, frozen_seeded_hash<std::string> {}, std::equal_to<std::string> {})
  };
};

IntData const &int_data()
{
  static IntData const data;
  return data;
}

StringData const &string_data()
{
  static StringData const data;
  return data;
}

template <class Map, class Key>
void BM_FindHit(benchmark::State &state, Map const &map, std::vector<Key> const &keys)
{
  for (auto _ : state) {
    std::int64_t sum = 0;
    for (auto const &key : keys) {
      auto const it = map.find(key);
      if (it != map.end()) sum += it->second;
    }
    benchmark::DoNotOptimize(sum);
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * static_cast<std::int64_t>(keys.size())));
}

template <class Map, class Key>
void BM_FindMiss(benchmark::State &state, Map const &map, std::vector<Key> const &miss)
{
  for (auto _ : state) {
    std::int64_t found = 0;
    for (auto const &key : miss) found += (map.find(key) != map.end());
    benchmark::DoNotOptimize(found);
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * static_cast<std::int64_t>(miss.size())));
}

template <class Map, class Key>
void BM_AtHit(benchmark::State &state, Map const &map, std::vector<Key> const &keys)
{
  for (auto _ : state) {
    std::int64_t sum = 0;
    for (auto const &key : keys) sum += map.at(key);
    benchmark::DoNotOptimize(sum);
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * static_cast<std::int64_t>(keys.size())));
}

template <class Set, class Key>
void BM_ContainsHit(benchmark::State &state, Set const &set, std::vector<Key> const &keys)
{
  for (auto _ : state) {
    std::int64_t found = 0;
    for (auto const &key : keys) found += set.contains(key);
    benchmark::DoNotOptimize(found);
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * static_cast<std::int64_t>(keys.size())));
}

template <class Set, class Key>
void BM_ContainsMiss(benchmark::State &state, Set const &set, std::vector<Key> const &miss)
{
  for (auto _ : state) {
    std::int64_t found = 0;
    for (auto const &key : miss) found += set.contains(key);
    benchmark::DoNotOptimize(found);
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * static_cast<std::int64_t>(miss.size())));
}

template <class Map, class Key>
void register_map_benchmarks(char const *group, Map const &map, std::vector<Key> const &keys, std::vector<Key> const &miss)
{
  using State = benchmark::State;
  benchmark::RegisterBenchmark(std::string(group) + "/FindHit", [&](State &state) { BM_FindHit(state, map, keys); })
      ->Unit(benchmark::kMicrosecond);
  benchmark::RegisterBenchmark(std::string(group) + "/FindMiss", [&](State &state) { BM_FindMiss(state, map, miss); })
      ->Unit(benchmark::kMicrosecond);
  benchmark::RegisterBenchmark(std::string(group) + "/AtHit", [&](State &state) { BM_AtHit(state, map, keys); })
      ->Unit(benchmark::kMicrosecond);
}

template <class Set, class Key>
void register_set_benchmarks(char const *group, Set const &set, std::vector<Key> const &keys, std::vector<Key> const &miss)
{
  using State = benchmark::State;
  benchmark::RegisterBenchmark(std::string(group) + "/ContainsHit", [&](State &state) { BM_ContainsHit(state, set, keys); })
      ->Unit(benchmark::kMicrosecond);
  benchmark::RegisterBenchmark(std::string(group) + "/ContainsMiss", [&](State &state) { BM_ContainsMiss(state, set, miss); })
      ->Unit(benchmark::kMicrosecond);
}

struct Registrar
{
  Registrar()
  {
    auto const &ints = int_data();
    register_map_benchmarks("Int/Mph", ints.minimal_map, ints.keys, ints.miss);
    register_map_benchmarks("Int/Flat", ints.flat_map, ints.keys, ints.miss);
    register_map_benchmarks("Int/Frozen", ints.frozen_map, ints.keys, ints.miss);
    register_set_benchmarks("IntSet/Mph", ints.minimal_set, ints.keys, ints.miss);
    register_set_benchmarks("IntSet/Flat", ints.flat_set, ints.keys, ints.miss);
    register_set_benchmarks("IntSet/Frozen", ints.frozen_set, ints.keys, ints.miss);

    auto const &strings = string_data();
    register_map_benchmarks("Str/Mph", strings.minimal_map, strings.keys, strings.miss);
    register_map_benchmarks("Str/Flat", strings.flat_map, strings.keys, strings.miss);
    register_map_benchmarks("Str/Frozen", strings.frozen_map, strings.keys, strings.miss);
    register_set_benchmarks("StrSet/Mph", strings.minimal_set, strings.keys, strings.miss);
    register_set_benchmarks("StrSet/Flat", strings.flat_set, strings.keys, strings.miss);
    register_set_benchmarks("StrSet/Frozen", strings.frozen_set, strings.keys, strings.miss);
  }
} g_registrar;
} // namespace
