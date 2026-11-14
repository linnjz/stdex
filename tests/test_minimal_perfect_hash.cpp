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

// test_minimal_perfect_hash.cpp
//
// Functional checks for stdex::minimal_perfect_hash_map / hash_set.
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "minimal_perfect_hash_map.hpp"
#include "minimal_perfect_hash_set.hpp"

namespace
{
int g_pass = 0;
int g_fail = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    if (cond) {                                                     \
      ++g_pass;                                                     \
    } else {                                                        \
      ++g_fail;                                                     \
      if (g_fail < 20) {                                            \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      }                                                             \
    }                                                               \
  } while (0)

std::uint64_t next_rand(std::uint64_t &state)
{
  std::uint64_t z = (state += 0x9E'37'79'B9'7F'4A'7C'15ULL);
  z               = (z ^ (z >> 30U)) * 0xBF'58'47'6D'1C'E4'E5'B9ULL;
  z               = (z ^ (z >> 27U)) * 0x94'D0'49'BB'13'31'11'EBULL;
  return z ^ (z >> 31U);
}

std::vector<std::uint64_t> make_random_keys(std::size_t count, std::uint64_t seed)
{
  std::vector<std::uint64_t> keys(count);
  std::uint64_t state = seed;
  for (auto &key : keys) key = next_rand(state);
  return keys;
}

void test_empty()
{
  std::printf("\n=== minimal_perfect_hash: empty ===\n");
  stdex::minimal_perfect_hash_set<std::uint64_t> set;
  CHECK(set.empty());
  CHECK(set.size() == 0);
  CHECK(set.begin() == set.end());
  CHECK(set.find(42) == set.end());
  CHECK(!set.contains(42));
  CHECK(set.count(42) == 0);

  stdex::minimal_perfect_hash_map<std::uint64_t, std::string> map;
  CHECK(map.empty());
  CHECK(map.find(42) == map.end());
  bool threw = false;
  try {
    (void) map.at(42);
  } catch (std::out_of_range const &) {
    threw = true;
  }
  CHECK(threw);
}

void test_set_basic()
{
  std::printf("\n=== minimal_perfect_hash: set basic ===\n");
  std::vector<std::uint64_t> const keys = make_random_keys(2000, 12345);
  stdex::minimal_perfect_hash_set<std::uint64_t> set(std::from_range_t{}, keys);

  CHECK(set.size() == keys.size());
  for (std::uint64_t key : keys) {
    CHECK(set.contains(key));
    CHECK(set.count(key) == 1);
    auto const it = set.find(key);
    CHECK(it != set.end());
    CHECK(*it == key);
    CHECK(set.at_unchecked(key) == key);
  }

  std::size_t iterated = 0;
  for (std::uint64_t key : set) {
    CHECK(set.contains(key));
    ++iterated;
  }
  CHECK(iterated == set.size());

  std::uint64_t state         = 999;
  std::size_t false_positives = 0;
  for (std::size_t i = 0; i < 5000; ++i) {
    std::uint64_t const key = next_rand(state);
    if (set.contains(key)) ++false_positives;
  }
  CHECK(false_positives == 0);
}

void test_map_basic()
{
  std::printf("\n=== minimal_perfect_hash: map basic ===\n");
  std::vector<std::pair<std::uint64_t, std::string>> values;
  for (std::uint64_t key = 0; key < 1500; ++key) {
    values.emplace_back(key * 37 + 11, "value-" + std::to_string(key));
  }
  stdex::minimal_perfect_hash_map<std::uint64_t, std::string> map(std::from_range_t {}, values);

  CHECK(map.size() == values.size());
  for (auto const &[key, value] : values) {
    auto const it = map.find(key);
    CHECK(it != map.end());
    CHECK(it->first == key);
    CHECK(it->second == value);
    CHECK(map.at(key) == value);
    CHECK(map.at_unchecked(key) == value);
  }

  std::uint64_t state = 4321;
  for (std::size_t i = 0; i < 5000; ++i) {
    std::uint64_t const key = next_rand(state);
    if (map.contains(key)) {
      // A hit is only valid if it really is one of the constructed keys.
      CHECK((key - 11) % 37 == 0 && (key - 11) / 37 < 1500);
      CHECK(map.at(key) == "value-" + std::to_string((key - 11) / 37));
    }
  }

  CHECK(map.find(12) == map.end());
  bool threw = false;
  try {
    (void) map.at(12);
  } catch (std::out_of_range const &) {
    threw = true;
  }
  CHECK(threw);

  std::size_t iterated = 0;
  for (auto const &entry : map) {
    CHECK(map.contains(entry.first));
    CHECK(map.at(entry.first) == entry.second);
    ++iterated;
  }
  CHECK(iterated == map.size());
}

void test_profiles()
{
  std::printf("\n=== minimal_perfect_hash: profiles ===\n");
  std::vector<std::uint64_t> const keys = make_random_keys(3000, 777);
  {
    stdex::minimal_perfect_hash_set<std::uint64_t, stdex::hash<std::uint64_t>, std::equal_to<std::uint64_t>, std::allocator<std::uint64_t>, stdex::ptr_hash_option_balanced>
        set(std::from_range_t {}, keys);
    for (std::uint64_t key : keys) CHECK(set.contains(key));
  }
  {
    stdex::minimal_perfect_hash_set<std::uint64_t, stdex::hash<std::uint64_t>, std::equal_to<std::uint64_t>, std::allocator<std::uint64_t>, stdex::ptr_hash_option_compact>
        set(std::from_range_t {}, keys);
    for (std::uint64_t key : keys) CHECK(set.contains(key));
  }
}

void test_string_keys()
{
  std::printf("\n=== minimal_perfect_hash: string keys ===\n");
  std::vector<std::string> keys;
  for (int i = 0; i < 1000; ++i) keys.push_back("key-" + std::to_string(i));
  stdex::minimal_perfect_hash_set<std::string> set(std::from_range_t {}, keys);
  for (auto const &key : keys) CHECK(set.contains(key));
  CHECK(!set.contains("missing"));
}

void test_initializer_list()
{
  std::printf("\n=== minimal_perfect_hash: initializer_list ===\n");
  stdex::minimal_perfect_hash_set<int> set { 1, 2, 3, 4, 5, 6, 7, 8 };
  for (int i = 1; i <= 8; ++i) CHECK(set.contains(i));
  CHECK(!set.contains(9));

  stdex::minimal_perfect_hash_map<int, std::string> map {
    { 1, "one"   },
    { 2, "two"   },
    { 3, "three" }
  };
  CHECK(map.at(1) == "one");
  CHECK(map.at(2) == "two");
  CHECK(map.at(3) == "three");
}

void test_copy_move()
{
  std::printf("\n=== minimal_perfect_hash: copy and move ===\n");
  std::vector<std::uint64_t> const keys = make_random_keys(300, 31415);
  stdex::minimal_perfect_hash_set<std::uint64_t> original(std::from_range_t {}, keys);
  auto copy  = original;
  auto moved = std::move(original);
  for (std::uint64_t key : keys) {
    CHECK(copy.contains(key));
    CHECK(moved.contains(key));
  }
}

void test_stress_sizes()
{
  std::printf("\n=== minimal_perfect_hash: stress sizes ===\n");
  for (std::size_t count = 0; count <= 500; ++count) {
    std::vector<std::uint64_t> const keys = make_random_keys(count, count * 131u + 7u);
    stdex::minimal_perfect_hash_set<std::uint64_t> set(std::from_range_t {}, keys);
    CHECK(set.size() == count);
    for (std::uint64_t key : keys) CHECK(set.contains(key));
  }
}
} // namespace

int main()
{
  test_empty();
  test_set_basic();
  test_map_basic();
  test_profiles();
  test_string_keys();
  test_initializer_list();
  test_copy_move();
  test_stress_sizes();

  std::printf("\n=== minimal_perfect_hash summary: %d passed, %d failed ===\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
