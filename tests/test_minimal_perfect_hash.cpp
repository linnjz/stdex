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
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include <version>

#if defined(__has_include)
#  if __has_include(<meta>)
#    include <meta>
#  endif
#endif

#if defined(__cpp_lib_optional) && __cpp_lib_optional >= 202506L
#  include <optional>
#endif

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

// A genuinely stateful allocator: the `tag` makes two copies distinct, which is
// what exposes duplicated per-buffer allocator copies in the container layout.
template <class T>
struct tagged_allocator
{
  using value_type = T;
  using pointer    = T *;
  using size_type  = std::size_t;

  int tag = 0;

  constexpr tagged_allocator() = default;
  constexpr explicit tagged_allocator(int t) : tag(t) {}
  template <class U>
  constexpr tagged_allocator(tagged_allocator<U> const &other) : tag(other.tag)
  {
  }

  constexpr T *allocate(std::size_t n) { return std::allocator<T>{}.allocate(n); }
  constexpr void deallocate(T *p, std::size_t n) { std::allocator<T>{}.deallocate(p, n); }

  template <class U>
  friend constexpr bool operator==(tagged_allocator const &a, tagged_allocator<U> const &b) noexcept { return a.tag == b.tag; }
  template <class U>
  friend constexpr bool operator!=(tagged_allocator const &a, tagged_allocator<U> const &b) noexcept { return !(a == b); }
};

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
  stdex::minimal_perfect_hash_set<std::uint64_t> set(keys.begin(), keys.end());

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
  stdex::minimal_perfect_hash_map<std::uint64_t, std::string> map(values.begin(), values.end());

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
    stdex::minimal_perfect_hash_set<std::uint64_t, stdex::hash<std::uint64_t>, std::equal_to<std::uint64_t>, stdex::ptr_hash_heap_options<std::allocator<std::uint64_t>>::profile_balanced()>
        set(keys.begin(), keys.end());
    for (std::uint64_t key : keys) CHECK(set.contains(key));
  }
  {
    stdex::minimal_perfect_hash_set<std::uint64_t, stdex::hash<std::uint64_t>, std::equal_to<std::uint64_t>, stdex::ptr_hash_heap_options<std::allocator<std::uint64_t>>::profile_compact()>
        set(keys.begin(), keys.end());
    for (std::uint64_t key : keys) CHECK(set.contains(key));
  }
}

void test_stack_mode()
{
  std::printf("\n=== minimal_perfect_hash: stack mode ===\n");

  using set_type = stdex::minimal_perfect_hash_set<
      std::uint64_t, stdex::hash<std::uint64_t>, std::equal_to<std::uint64_t>,
      stdex::ptr_hash_stack_options<2048>::profile_default()>;
  static_assert(set_type::storage_policy == stdex::ptr_hash_storage_policy::stack);

  std::vector<std::uint64_t> const keys = make_random_keys(1500, 424242);
  set_type set(keys.begin(), keys.end());
  CHECK(set.size() == keys.size());
  CHECK(set.max_size() == 2048);
  for (std::uint64_t key : keys) {
    CHECK(set.contains(key));
    CHECK(set.find(key) != set.end());
    CHECK(set.at_unchecked(key) == key);
  }
  CHECK(!set.contains(0xDEAD'BEEF'CAFE'BABEULL));

  using map_type = stdex::minimal_perfect_hash_map<
      std::uint64_t, std::uint64_t, stdex::hash<std::uint64_t>, std::equal_to<std::uint64_t>,
      stdex::ptr_hash_stack_options<2048>::profile_default()>;
  static_assert(map_type::storage_policy == stdex::ptr_hash_storage_policy::stack);

  std::vector<std::pair<std::uint64_t, std::uint64_t>> values;
  for (std::uint64_t i = 0; i < 1500; ++i) values.emplace_back(i * 13 + 1, i);
  map_type map(values.begin(), values.end());
  CHECK(map.size() == values.size());
  for (auto const &[key, mapped] : values) {
    CHECK(map.contains(key));
    CHECK(map.at(key) == mapped);
  }

  // Non-trivial mapped values exercise the cycle permutation path with inline storage.
  using string_map_type = stdex::minimal_perfect_hash_map<
      std::uint64_t, std::string, stdex::hash<std::uint64_t>, std::equal_to<std::uint64_t>,
      stdex::ptr_hash_stack_options<512>::profile_default()>;
  std::vector<std::pair<std::uint64_t, std::string>> string_values;
  for (std::uint64_t i = 0; i < 256; ++i) string_values.emplace_back(i * 7 + 3, "v" + std::to_string(i));
  string_map_type string_map(string_values.begin(), string_values.end());
  CHECK(string_map.size() == string_values.size());
  for (auto const &[key, mapped] : string_values) {
    CHECK(string_map.contains(key));
    CHECK(string_map.at(key) == mapped);
  }
}

void test_string_keys()
{
  std::printf("\n=== minimal_perfect_hash: string keys ===\n");
  std::vector<std::string> keys;
  for (int i = 0; i < 1000; ++i) keys.push_back("key-" + std::to_string(i));
  stdex::minimal_perfect_hash_set<std::string> set(keys.begin(), keys.end());
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
  stdex::minimal_perfect_hash_set<std::uint64_t> original(keys.begin(), keys.end());
  auto copy  = original;
  auto moved = std::move(original);
  for (std::uint64_t key : keys) {
    CHECK(copy.contains(key));
    CHECK(moved.contains(key));
  }
}

void test_interface()
{
  std::printf("\n=== minimal_perfect_hash: interface ===\n");
  std::vector<std::uint64_t> const keys = make_random_keys(128, 2026);
  stdex::minimal_perfect_hash_set<std::uint64_t> set(keys.begin(), keys.end());

  using set_type = decltype(set);
  static_assert(std::is_same_v<set_type::reference, set_type::const_reference>);
  static_assert(std::is_same_v<set_type::pointer, set_type::const_pointer>);
  static_assert(std::is_same_v<set_type::iterator, set_type::const_iterator>);

  CHECK(set.max_size() >= set.size());
  CHECK(set.bucket_count() > 0);
  CHECK(set.max_bucket_count() == set.bucket_count());

  auto const found = set.equal_range(keys.front());
  CHECK(found.first != set.end());
  CHECK(found.second == found.first + 1);
  CHECK(*found.first == keys.front());
  auto const missing = set.equal_range(999999999);
  CHECK(missing.first == set.end());
  CHECK(missing.second == set.end());

  stdex::minimal_perfect_hash_map<std::uint64_t, std::string> map { { 1, "one" }, { 2, "two" } };
  auto const [first, last] = map.equal_range(2);
  CHECK(first != map.end());
  CHECK(first->second == "two");
  CHECK(last == first + 1);
  CHECK(map.equal_range(3).first == map.end());
  CHECK(map.max_size() >= map.size());
  CHECK(map.bucket_count() > 0);
  CHECK(map.max_bucket_count() == map.bucket_count());
}

void test_ctad()
{
  std::printf("\n=== minimal_perfect_hash: CTAD ===\n");
  using MapAlloc = std::allocator<std::pair<int, int>>;
  using SetAlloc = std::allocator<int>;
  MapAlloc map_alloc;
  SetAlloc set_alloc;
  std::hash<int> hasher;
  std::vector<std::pair<int, int>> values { { 1, 10 }, { 2, 20 } };
  std::vector<int> keys { 1, 2, 3 };
  std::initializer_list<std::pair<int, int>> ilist = { { 1, 10 }, { 2, 20 } };
  std::initializer_list<int> key_ilist            = { 1, 2, 3 };

  stdex::minimal_perfect_hash_map m1(values.begin(), values.end());
  static_assert(std::is_same_v<typename decltype(m1)::key_type, int>);
  static_assert(std::is_same_v<typename decltype(m1)::mapped_type, int>);
  CHECK(m1.size() == values.size());

  stdex::minimal_perfect_hash_map m2(values.begin(), values.end(), map_alloc);
  static_assert(std::is_same_v<typename decltype(m2)::allocator_type, MapAlloc>);
  CHECK(m2.size() == values.size());

  stdex::minimal_perfect_hash_map m3(values.begin(), values.end(), hasher, map_alloc);
  static_assert(std::is_same_v<typename decltype(m3)::hasher, std::hash<int>>);
  CHECK(m3.size() == values.size());

  stdex::minimal_perfect_hash_map m4 { std::pair { 1, 1 }, std::pair { 2, 2 } };
  static_assert(std::is_same_v<typename decltype(m4)::key_type, int>);
  static_assert(std::is_same_v<typename decltype(m4)::mapped_type, int>);
  CHECK(m4.size() == 2);

  stdex::minimal_perfect_hash_map m5(ilist, map_alloc);
  static_assert(std::is_same_v<typename decltype(m5)::allocator_type, MapAlloc>);
  CHECK(m5.size() == ilist.size());

  stdex::minimal_perfect_hash_map m6(ilist, hasher, map_alloc);
  static_assert(std::is_same_v<typename decltype(m6)::hasher, std::hash<int>>);
  CHECK(m6.size() == ilist.size());

#ifdef __cpp_lib_containers_ranges
  stdex::minimal_perfect_hash_map m7(std::from_range, values);
  static_assert(std::is_same_v<typename decltype(m7)::key_type, int>);
  CHECK(m7.size() == values.size());

  stdex::minimal_perfect_hash_map m8(std::from_range, values, map_alloc);
  static_assert(std::is_same_v<typename decltype(m8)::allocator_type, MapAlloc>);
  CHECK(m8.size() == values.size());

  stdex::minimal_perfect_hash_map m9(std::from_range, values, hasher, map_alloc);
  static_assert(std::is_same_v<typename decltype(m9)::hasher, std::hash<int>>);
  CHECK(m9.size() == values.size());
#endif

  stdex::minimal_perfect_hash_set s1(keys.begin(), keys.end());
  static_assert(std::is_same_v<typename decltype(s1)::key_type, int>);
  CHECK(s1.size() == keys.size());

  stdex::minimal_perfect_hash_set s2(keys.begin(), keys.end(), set_alloc);
  static_assert(std::is_same_v<typename decltype(s2)::allocator_type, SetAlloc>);
  CHECK(s2.size() == keys.size());

  stdex::minimal_perfect_hash_set s3(keys.begin(), keys.end(), hasher, set_alloc);
  static_assert(std::is_same_v<typename decltype(s3)::hasher, std::hash<int>>);
  CHECK(s3.size() == keys.size());

  stdex::minimal_perfect_hash_set s4 { 1, 2, 3 };
  static_assert(std::is_same_v<typename decltype(s4)::key_type, int>);
  CHECK(s4.size() == 3);

  stdex::minimal_perfect_hash_set s5(key_ilist, set_alloc);
  static_assert(std::is_same_v<typename decltype(s5)::allocator_type, SetAlloc>);
  CHECK(s5.size() == key_ilist.size());

  stdex::minimal_perfect_hash_set s6(key_ilist, hasher, set_alloc);
  static_assert(std::is_same_v<typename decltype(s6)::hasher, std::hash<int>>);
  CHECK(s6.size() == key_ilist.size());

#ifdef __cpp_lib_containers_ranges
  stdex::minimal_perfect_hash_set s7(std::from_range, keys);
  static_assert(std::is_same_v<typename decltype(s7)::key_type, int>);
  CHECK(s7.size() == keys.size());

  stdex::minimal_perfect_hash_set s8(std::from_range, keys, set_alloc);
  static_assert(std::is_same_v<typename decltype(s8)::allocator_type, SetAlloc>);
  CHECK(s8.size() == keys.size());

  stdex::minimal_perfect_hash_set s9(std::from_range, keys, hasher, set_alloc);
  static_assert(std::is_same_v<typename decltype(s9)::hasher, std::hash<int>>);
  CHECK(s9.size() == keys.size());
#endif
}

void test_heterogeneous_lookup()
{
  std::printf("\n=== minimal_perfect_hash: heterogeneous lookup ===\n");

  std::vector<std::string> keys;
  std::vector<std::pair<std::string, int>> values;
  for (int i = 0; i < 1000; ++i) {
    keys.push_back("key-" + std::to_string(i));
    values.emplace_back(keys.back(), i);
  }

  using map_type = stdex::minimal_perfect_hash_map<std::string, int, stdex::hash<std::string>, std::equal_to<>>;
  map_type map(values.begin(), values.end());

  std::string_view const hit = values[42].first;
  auto const found           = map.find(hit);
  CHECK(found != map.end());
  CHECK(found->second == 42);
  CHECK(map.contains(std::string_view { "key-77" }));
  CHECK(map.count(std::string_view { "key-77" }) == 1);
  CHECK(!map.contains(std::string_view { "missing" }));
  CHECK(map.at(std::string_view { "key-9" }) == 9);
  CHECK(map.at_unchecked(std::string_view { "key-9" }) == 9);

  auto const [first, last] = map.equal_range(std::string_view { "key-123" });
  CHECK(first != map.end());
  CHECK(first->second == 123);
  CHECK(last == first + 1);

  CHECK(map.contains("key-5"));
  CHECK(map.at("key-5") == 5);
  CHECK(map.find("key-5") != map.end());

  using set_type = stdex::minimal_perfect_hash_set<std::string, stdex::hash<std::string>, std::equal_to<>>;
  set_type set(keys.begin(), keys.end());
  CHECK(set.contains(std::string_view { "key-7" }));
  CHECK(set.count("key-7") == 1);
  CHECK(set.find(std::string_view { "key-7" }) != set.end());
  CHECK(set.at(std::string_view { "key-7" }) == "key-7");
  CHECK(set.equal_range(std::string_view { "key-7" }).first != set.end());

  using view_set_type = stdex::minimal_perfect_hash_set<std::string_view, stdex::hash<std::string_view>, std::equal_to<>>;
  std::vector<std::string_view> views;
  for (std::string const &key : keys) views.emplace_back(key);
  view_set_type view_set(views.begin(), views.end());
  CHECK(view_set.contains(keys.front()));
  CHECK(view_set.find(keys.back()) != view_set.end());

  struct transparent_string_hash
  {
    using is_transparent = void;

    std::size_t operator() (std::string const &value) const noexcept { return stdex::hash<std::string> {}(value); }
    std::size_t operator() (std::string_view const value) const noexcept { return stdex::hash<std::string_view> {}(value); }
    std::size_t operator() (char const *const value) const noexcept { return stdex::hash<std::string_view> {}(value); }
  };

  using custom_map_type = stdex::minimal_perfect_hash_map<std::string, int, transparent_string_hash, std::equal_to<>>;
  custom_map_type custom_map(values.begin(), values.end());
  CHECK(custom_map.find(std::string_view { "key-321" }) != custom_map.end());
  CHECK(custom_map.contains("key-321"));
  CHECK(custom_map.at(std::string_view { "key-321" }) == 321);
}

#if defined(__cpp_lib_optional) && __cpp_lib_optional >= 202506L
void test_lookup()
{
  std::printf("\n=== minimal_perfect_hash: lookup ===\n");

  std::vector<std::pair<std::string, int>> values;
  for (int i = 0; i < 256; ++i) values.emplace_back("key-" + std::to_string(i), i);

  using map_type = stdex::minimal_perfect_hash_map<std::string, int, stdex::hash<std::string>, std::equal_to<>>;
  map_type map(values.begin(), values.end());

  auto hit = map.lookup(std::string_view { "key-42" });
  static_assert(std::is_same_v<decltype(hit), std::optional<const int &>>);
  CHECK(hit.has_value());
  CHECK(hit.value() == 42);
  CHECK(*hit == 42);
  CHECK(&*hit == &map.at(std::string_view { "key-42" }));

  auto miss = map.lookup("missing");
  CHECK(!miss);
  CHECK(miss.value_or(-1) == -1);

  auto const &const_map = map;
  auto const_hit        = const_map.lookup("key-7");
  CHECK(const_hit.has_value());
  CHECK(*const_hit == 7);
}
#endif

void test_stateful_allocator()
{
  std::printf("\n=== minimal_perfect_hash: stateful allocator ===\n");

  using map_alloc = tagged_allocator<std::pair<std::uint64_t, std::uint64_t>>;
  using set_alloc = tagged_allocator<std::uint64_t>;
  using map_type  = stdex::minimal_perfect_hash_map<
      std::uint64_t, std::uint64_t, stdex::hash<std::uint64_t>, std::equal_to<std::uint64_t>,
      stdex::ptr_hash_heap_options<map_alloc>::profile_default()>;
  using set_type = stdex::minimal_perfect_hash_set<
      std::uint64_t, stdex::hash<std::uint64_t>, std::equal_to<std::uint64_t>,
      stdex::ptr_hash_heap_options<set_alloc>::profile_default()>;

  // The whole point of the layout: a stateful allocator adds exactly one copy
  // (inside the ptr_hash index), not one per value/fingerprint buffer.
  using default_map_type = stdex::minimal_perfect_hash_map<std::uint64_t, std::uint64_t>;
  // A single stored copy: the stateful container must not be larger than the
  // stateless one plus a couple of alignment-rounded allocator copies.
  CHECK(sizeof(map_type) <= sizeof(default_map_type) + 2 * sizeof(map_alloc));

  std::vector<std::pair<std::uint64_t, std::uint64_t>> entries;
  for (std::uint64_t i = 0; i < 4000; ++i) entries.emplace_back(i * 2654435761u + 17u, i);
  std::vector<std::uint64_t> keys;
  keys.reserve(entries.size());
  for (auto const &e : entries) keys.push_back(e.first);

  map_type map(entries.begin(), entries.end(), stdex::hash<std::uint64_t>{}, std::equal_to<std::uint64_t>{}, map_alloc{ 7 });
  CHECK(map.get_allocator().tag == 7);
  CHECK(map.size() == entries.size());
  for (auto const &[key, mapped] : entries) {
    CHECK(map.contains(key));
    CHECK(map.at(key) == mapped);
  }

  map_type copy = map;
  CHECK(copy.get_allocator().tag == 7);
  CHECK(copy.size() == map.size());
  for (auto const &[key, mapped] : entries) CHECK(copy.at(key) == mapped);

  map_type moved = std::move(copy);
  CHECK(moved.get_allocator().tag == 7);
  for (auto const &[key, mapped] : entries) CHECK(moved.at(key) == mapped);

  map_type assigned;
  assigned = map;
  CHECK(assigned.size() == map.size());
  for (auto const &[key, mapped] : entries) CHECK(assigned.at(key) == mapped);

  auto assigned_move = map_type{};
  assigned_move       = std::move(assigned);
  for (auto const &[key, mapped] : entries) CHECK(assigned_move.at(key) == mapped);

  set_type set(keys.begin(), keys.end(),
               stdex::hash<std::uint64_t>{}, std::equal_to<std::uint64_t>{}, set_alloc{ 9 });
  CHECK(set.get_allocator().tag == 9);
  CHECK(set.size() == entries.size());
}

void test_stress_sizes()
{
  std::printf("\n=== minimal_perfect_hash: stress sizes ===\n");
  for (std::size_t count = 0; count <= 500; ++count) {
    std::vector<std::uint64_t> const keys = make_random_keys(count, count * 131u + 7u);
    stdex::minimal_perfect_hash_set<std::uint64_t> set(keys.begin(), keys.end());
    CHECK(set.size() == count);
    for (std::uint64_t key : keys) CHECK(set.contains(key));
  }
}
// ---------------------------------------------------------------
// Reflection freeze() (C++26 P3491 std::define_static_array).
//
// Compiled only when the standard library actually exposes
// __cpp_lib_define_static, so it stays dormant on pre-C++26 toolchains and
// becomes active automatically once a local compiler supports C++26 reflection.
// The heap-backed default options are used because they can be built during
// constant evaluation (stack storage of non-trivial value_type is not yet
// constexpr-constructible).
// ---------------------------------------------------------------
#if defined(__cpp_lib_define_static)
consteval bool minimal_freeze_reproduces_owner()
{
  constexpr int n = 40;

  std::array<std::pair<int, int>, n> entries {};
  for (int i = 0; i < n; ++i) entries[static_cast<std::size_t>(i)] = { i, i * 3 + 1 };

  stdex::minimal_perfect_hash_map<int, int> map(entries.begin(), entries.end());
  auto const mv = stdex::minimal_perfect_hash_map<int, int>::freeze(map);

  bool ok = mv.size() == map.size() && mv.bucket_count() == map.bucket_count();
  for (int i = 0; i < n; ++i) {
    if (!mv.contains(i) || mv.at(i) != i * 3 + 1) ok = false;
    auto const it = mv.find(i);
    if (it == mv.end() || it->second != i * 3 + 1) ok = false;
  }
  if (mv.contains(n + 12345)) ok = false;
  std::size_t visited = 0;
  for (auto it = mv.begin(); it != mv.end(); ++it) ++visited;
  if (visited != static_cast<std::size_t>(n)) ok = false;

  std::array<int, n> keys {};
  for (int i = 0; i < n; ++i) keys[static_cast<std::size_t>(i)] = i;
  stdex::minimal_perfect_hash_set<int> set(keys.begin(), keys.end());
  auto const sv = stdex::minimal_perfect_hash_set<int>::freeze(set);
  for (int i = 0; i < n; ++i) {
    if (!sv.contains(i) || sv.at(i) != i) ok = false;
  }
  if (sv.contains(n + 12345)) ok = false;
  return ok;
}
#endif

void test_reflection_freeze()
{
  std::printf("\n=== minimal_perfect_hash: reflection freeze ===\n");
#if defined(__cpp_lib_define_static)
  constexpr bool ok = minimal_freeze_reproduces_owner();
  static_assert(ok);
  CHECK(ok);
#else
  std::printf("  (skipped: __cpp_lib_define_static / <meta> reflection not available)\n");
  CHECK(true);
#endif
}
} // namespace

int main()
{
  test_empty();
  test_set_basic();
  test_map_basic();
  test_profiles();
  test_stack_mode();
  test_string_keys();
  test_initializer_list();
  test_copy_move();
  test_stateful_allocator();
  test_interface();
  test_ctad();
  test_heterogeneous_lookup();
#if defined(__cpp_lib_optional) && __cpp_lib_optional >= 202506L
  test_lookup();
#endif
  test_stress_sizes();
  test_reflection_freeze();

  std::printf("\n=== minimal_perfect_hash summary: %d passed, %d failed ===\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
