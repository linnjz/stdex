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

// test_ptrhash.cpp
//
// Functional checks for stdex::ptr_hash in both heap and stack modes
// (include/ptrhash.hpp). Deterministic SplitMix64 key streams keep the test free
// of flakiness.
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <version>

#include "ptrhash.hpp"

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

template <class Table>
bool check_indices(Table const &table, std::vector<std::uint64_t> const &keys, std::size_t bound)
{
  std::vector<unsigned char> taken(bound, 0);
  for (std::uint64_t const key : keys) {
    std::size_t const index           = table.index(key);
    std::size_t const unchecked_index = table.index_unchecked(key);
    CHECK(index == unchecked_index);
    CHECK(index < bound);
    if (index >= bound) return false;
    CHECK(taken[index] == 0);
    taken[index] = 1;
  }
  return true;
}

template <class Table>
bool check_absent_keys(Table const &table, std::size_t count, std::size_t bound, std::uint64_t seed)
{
  std::uint64_t state = seed;
  for (std::size_t i = 0; i < count; ++i) {
    std::uint64_t const key = next_rand(state);
    CHECK(table.index(key) < bound);
  }
  return true;
}

struct identity_hasher
{
  std::uint64_t operator() (std::uint64_t const key) const noexcept { return key; }
};

struct constant_hasher
{
  std::uint64_t operator() (std::uint64_t) const noexcept { return 0; }
};

template <class Allocator>
struct options_zero_evictions
{
  static constexpr auto value = [] {
    stdex::ptr_hash_options<stdex::ptr_hash_heap_storage_policy<Allocator>> options {};
    options.max_evictions_per_slot = 0;
    return options;
  }();
};

template <class Allocator>
struct options_tiny_load
{
  static constexpr auto value = [] {
    stdex::ptr_hash_options<stdex::ptr_hash_heap_storage_policy<Allocator>> options {};
    options.slot_load_factor = 1e-30;
    return options;
  }();
};

constexpr bool fixed_constexpr_check()
{
  std::array<std::uint64_t, 8> const keys { 1, 2, 3, 4, 5, 6, 7, 8 };
  stdex::ptr_hash<std::uint64_t, stdex::hash<std::uint64_t>, stdex::ptr_hash_stack_option_default<8>> table(keys.begin(), keys.end());
  if (table.size() != keys.size()) return false;
  for (std::uint64_t key : keys) {
    if (table.index(key) >= table.capacity()) return false;
  }
  return true;
}
static_assert(fixed_constexpr_check(), "stack-mode ptr_hash must be usable during constant evaluation");

struct pointer_key
{
  int *value;
};

struct pointer_key_hasher
{
  std::uint64_t operator() (pointer_key const &key) const noexcept
  {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(key.value));
  }
};

struct allocation_stats
{
  inline static std::size_t allocations   = 0;
  inline static std::size_t deallocations = 0;
};

template <class T>
struct counting_allocator
{
  using value_type = T;
  using pointer    = T *;
  using size_type  = std::size_t;

  int tag = 0;

  constexpr counting_allocator() = default;

  constexpr explicit counting_allocator(int value)
      : tag(value)
  {
  }

  template <class U>
  constexpr counting_allocator(counting_allocator<U> const &other)
      : tag(other.tag)
  {
  }

  constexpr T *allocate(std::size_t count)
  {
    ++allocation_stats::allocations;
    return std::allocator<T> {}.allocate(count);
  }

  constexpr void deallocate(T *ptr, std::size_t count)
  {
    ++allocation_stats::deallocations;
    std::allocator<T> {}.deallocate(ptr, count);
  }

  template <class U>
  friend constexpr bool operator== (counting_allocator const &lhs, counting_allocator<U> const &rhs) noexcept
  {
    return lhs.tag == rhs.tag;
  }

  template <class U>
  friend constexpr bool operator!= (counting_allocator const &lhs, counting_allocator<U> const &rhs) noexcept
  {
    return !(lhs == rhs);
  }
};

void test_basic_construction()
{
  std::printf("\n=== ptrhash: basic construction ===\n");

  {
    stdex::ptr_hash<std::uint64_t> empty;
    CHECK(empty.empty());
    CHECK(empty.size() == 0);
    CHECK(empty.capacity() == 0);
    CHECK(empty.index(12345) == stdex::ptr_hash<std::uint64_t>::npos);
  }

  using fast     = stdex::ptr_hash<std::uint64_t>;
  using balanced = stdex::ptr_hash<std::uint64_t, stdex::hash<std::uint64_t>, stdex::ptr_hash_heap_option_balanced<std::allocator<std::uint64_t>>>;
  using compact  = stdex::ptr_hash<std::uint64_t, stdex::hash<std::uint64_t>, stdex::ptr_hash_heap_option_compact<std::allocator<std::uint64_t>>>;

  for (std::size_t count = 0; count <= 200; ++count) {
    std::vector<std::uint64_t> const keys = make_random_keys(count, count * 7919u + 17u);

    fast fast_table;
    CHECK(fast_table.try_build(keys));
    CHECK(fast_table.size() == count);
    CHECK(fast_table.capacity() >= count);
    CHECK(check_indices(fast_table, keys, fast_table.capacity()));
    if (count != 0) CHECK(check_absent_keys(fast_table, 8, fast_table.capacity(), count * 104729u + 3u));

    balanced balanced_table;
    CHECK(balanced_table.try_build(keys));
    CHECK(balanced_table.size() == count);
    CHECK(balanced_table.capacity() >= count);
    CHECK(check_indices(balanced_table, keys, balanced_table.size()));
    if (count != 0) CHECK(check_absent_keys(balanced_table, 8, balanced_table.size(), count * 104729u + 5u));

    compact compact_table;
    CHECK(compact_table.try_build(keys));
    CHECK(check_indices(compact_table, keys, compact_table.size()));
  }

  {
    std::vector<std::uint64_t> const keys = make_random_keys(1000, 42);
    fast table(keys.begin(), keys.end());
    CHECK(table.hash_seed() != 0);
    CHECK(table.bucket_count() > 0);
    CHECK(table.hash_function()(123) == stdex::hash<std::uint64_t> {}(123));
  }

  {
    std::vector<std::uint64_t> const keys = make_random_keys(500, 7);
    fast table;
    CHECK(table.try_build(keys));
    std::size_t const first_seed = table.hash_seed();
    CHECK(table.try_build(keys));
    CHECK(table.size() == keys.size());
    CHECK(check_indices(table, keys, table.capacity()));
    CHECK(first_seed != 0);
  }
}

void test_key_types()
{
  std::printf("\n=== ptrhash: key types ===\n");

  {
    std::vector<std::uint32_t> keys;
    for (std::uint32_t i = 0; i < 5000; ++i) keys.push_back(i * 2'654'435'761U);
    using table_type = stdex::ptr_hash<std::uint32_t, stdex::hash<std::uint32_t>, stdex::ptr_hash_heap_option_balanced<std::allocator<std::uint32_t>>>;
    table_type table(keys.begin(), keys.end());
    std::vector<unsigned char> taken(table.size(), 0);
    for (std::uint32_t key : keys) {
      std::size_t const index = table.index(key);
      CHECK(index < table.size());
      CHECK(taken[index] == 0);
      taken[index] = 1;
    }
  }

  {
    std::vector<std::int64_t> keys;
    for (std::int64_t i = -2500; i <= 2500; ++i) keys.push_back(i);
    stdex::ptr_hash<std::int64_t, stdex::hash<std::int64_t>, stdex::ptr_hash_heap_option_balanced<std::allocator<std::int64_t>>> table(keys.begin(), keys.end());
    std::vector<unsigned char> taken(table.size(), 0);
    for (std::int64_t key : keys) {
      std::size_t const index = table.index(key);
      CHECK(index < table.size());
      CHECK(taken[index] == 0);
      taken[index] = 1;
    }
  }

  {
    std::vector<std::string> keys;
    for (int i = 0; i < 3000; ++i) keys.push_back("ptrhash-key-" + std::to_string(i) + "-payload");
    using table_type = stdex::ptr_hash<std::string, stdex::hash<std::string>, stdex::ptr_hash_heap_option_balanced<std::allocator<std::string>>>;
    table_type table(keys.begin(), keys.end());
    std::vector<unsigned char> taken(table.size(), 0);
    for (std::string const &key : keys) {
      std::size_t const index = table.index(key);
      CHECK(index < table.size());
      CHECK(taken[index] == 0);
      taken[index] = 1;

      std::string_view const view = key;
      CHECK(table.hash_value(view) == table.hash_value(key));
      CHECK(table.index(view) == index);
      CHECK(table.index_unchecked(view) == index);
    }
  }

  {
    std::vector<std::string_view> keys;
    keys.emplace_back("alpha");
    keys.emplace_back("beta");
    keys.emplace_back("gamma");
    keys.emplace_back("delta");
    stdex::ptr_hash<std::string_view, stdex::hash<std::string_view>, stdex::ptr_hash_heap_option_balanced<std::allocator<std::string_view>>> table(keys.begin(), keys.end());
    std::vector<unsigned char> taken(table.size(), 0);
    for (std::string_view key : keys) {
      std::size_t const index = table.index(key);
      CHECK(index < table.size());
      CHECK(taken[index] == 0);
      taken[index] = 1;
    }
  }

  {
    int a = 0;
    int b = 0;
    int c = 0;
    std::vector<pointer_key> keys { { &a }, { &b }, { &c } };
    stdex::ptr_hash<pointer_key, pointer_key_hasher, stdex::ptr_hash_heap_option_balanced<std::allocator<pointer_key>>> table(keys.begin(), keys.end());
    std::vector<unsigned char> taken(table.size(), 0);
    for (pointer_key const &key : keys) {
      std::size_t const index = table.index(key);
      CHECK(index < table.size());
      CHECK(taken[index] == 0);
      taken[index] = 1;
    }
  }
}

void test_error_paths()
{
  std::printf("\n=== ptrhash: error paths ===\n");

  {
    std::vector<std::uint64_t> keys { 1, 2, 3, 4, 4 };
    stdex::ptr_hash<std::uint64_t> table;
    CHECK(!table.try_build(keys));
  }

  {
    std::vector<std::uint64_t> keys { 1, 2, 3, 4, 5 };
    stdex::ptr_hash<std::uint64_t, constant_hasher> table;
    CHECK(!table.try_build(keys));
  }

  {
    std::vector<std::uint64_t> keys { 10, 20, 30 };
    bool threw = false;
    try {
      stdex::ptr_hash<std::uint64_t, constant_hasher> table(keys.begin(), keys.end());
      (void) table;
    } catch (std::runtime_error const &) {
      threw = true;
    }
    CHECK(threw);
  }

  {
    std::vector<std::uint64_t> keys { 1, 2, 3 };
    stdex::ptr_hash<std::uint64_t, constant_hasher> table;
    CHECK(!table.try_build(keys.begin(), keys.end()));
  }
}

void test_copy_move_allocator()
{
  std::printf("\n=== ptrhash: copy, move, allocator ===\n");

  using table_type = stdex::ptr_hash<std::uint64_t, stdex::hash<std::uint64_t>, stdex::ptr_hash_heap_option_balanced<counting_allocator<std::uint64_t>>>;

  std::vector<std::uint64_t> const keys = make_random_keys(2000, 1234567);
  allocation_stats::allocations         = 0;
  allocation_stats::deallocations       = 0;

  {
    table_type table(keys.begin(), keys.end(), stdex::hash<std::uint64_t> {}, counting_allocator<std::uint64_t> { 77 });
    CHECK(table.get_allocator().tag == 77);
    CHECK(check_indices(table, keys, table.size()));

    table_type copy = table;
    CHECK(copy.get_allocator().tag == 77);
    CHECK(copy.size() == table.size());
    CHECK(check_indices(copy, keys, copy.size()));

    table_type moved = std::move(copy);
    CHECK(moved.get_allocator().tag == 77);
    CHECK(moved.size() == table.size());
    CHECK(check_indices(moved, keys, moved.size()));

    table_type assigned;
    assigned = table;
    CHECK(assigned.size() == table.size());
    CHECK(check_indices(assigned, keys, assigned.size()));
  }

  CHECK(allocation_stats::allocations == allocation_stats::deallocations);
}

void test_initializer_list_and_ranges()
{
  std::printf("\n=== ptrhash: initializer_list and range ===\n");

  {
    stdex::ptr_hash<std::uint64_t, stdex::hash<std::uint64_t>, stdex::ptr_hash_heap_option_balanced<std::allocator<std::uint64_t>>> table { 10, 20, 30, 40, 50, 60, 70 };
    CHECK(table.size() == 7);
    std::vector<unsigned char> taken(table.size(), 0);
    for (std::uint64_t key : { 10u, 20u, 30u, 40u, 50u, 60u, 70u }) {
      std::size_t const index = table.index(key);
      CHECK(index < table.size());
      CHECK(taken[index] == 0);
      taken[index] = 1;
    }
  }

  {
    std::vector<std::uint64_t> const keys = make_random_keys(3000, 999);
    stdex::ptr_hash<std::uint64_t, stdex::hash<std::uint64_t>, stdex::ptr_hash_heap_option_balanced<std::allocator<std::uint64_t>>> table;
    CHECK(table.try_build(keys));
    CHECK(check_indices(table, keys, table.size()));
  }

#ifdef __cpp_lib_containers_ranges
  {
    std::vector<std::uint64_t> const keys = make_random_keys(1000, 2024);

    using heap_table  = stdex::ptr_hash<std::uint64_t, stdex::hash<std::uint64_t>, stdex::ptr_hash_heap_option_balanced<std::allocator<std::uint64_t>>>;
    using stack_table = stdex::ptr_hash<std::uint64_t, stdex::hash<std::uint64_t>, stdex::ptr_hash_stack_option_balanced<1000>>;

    heap_table heap_default { std::from_range_t {}, keys };
    CHECK(heap_default.size() == keys.size());
    CHECK(check_indices(heap_default, keys, heap_default.size()));

    heap_table heap_hasher { std::from_range_t {}, keys, stdex::hash<std::uint64_t> {} };
    CHECK(heap_hasher.size() == keys.size());
    CHECK(check_indices(heap_hasher, keys, heap_hasher.size()));

    heap_table heap_allocator { std::from_range_t {}, keys, stdex::hash<std::uint64_t> {}, std::allocator<std::uint64_t> {} };
    CHECK(heap_allocator.size() == keys.size());
    CHECK(check_indices(heap_allocator, keys, heap_allocator.size()));

    stack_table stack_default { std::from_range_t {}, keys };
    CHECK(stack_default.size() == keys.size());
    CHECK(check_indices(stack_default, keys, stack_default.size()));

    stack_table stack_hasher { std::from_range_t {}, keys, stdex::hash<std::uint64_t> {} };
    CHECK(stack_hasher.size() == keys.size());
    CHECK(check_indices(stack_hasher, keys, stack_hasher.size()));
  }
#endif // __cpp_lib_containers_ranges
}

void test_stress()
{
  std::printf("\n=== ptrhash: stress ===\n");

  for (std::size_t count : { 1u, 2u, 3u, 10u, 100u, 1000u, 10'000u }) {
    std::vector<std::uint64_t> keys(count);
    for (std::size_t i = 0; i < count; ++i) keys[i] = i;

    stdex::ptr_hash<std::uint64_t, identity_hasher> table;
    CHECK(table.try_build(keys));
    CHECK(check_indices(table, keys, table.capacity()));

    using balanced_identity = stdex::ptr_hash<std::uint64_t, identity_hasher, stdex::ptr_hash_heap_option_balanced<std::allocator<std::uint64_t>>>;
    balanced_identity minimal;
    CHECK(minimal.try_build(keys));
    CHECK(check_indices(minimal, keys, minimal.size()));
  }

  {
    std::vector<std::uint64_t> const keys = make_random_keys(50'000, 20261107);
    stdex::ptr_hash<std::uint64_t> table(keys.begin(), keys.end());
    CHECK(table.size() == keys.size());
    CHECK(check_indices(table, keys, table.capacity()));

    stdex::ptr_hash<std::uint64_t, stdex::hash<std::uint64_t>, stdex::ptr_hash_heap_option_balanced<std::allocator<std::uint64_t>>> minimal(keys.begin(), keys.end());
    CHECK(minimal.size() == keys.size());
    CHECK(check_indices(minimal, keys, minimal.size()));
  }

  // Regular arithmetic progressions are the classic weak-hash trap: the
  // bucket counts stay deceptively uniform while the slot lattice becomes
  // highly correlated.
  for (std::uint64_t multiplier : { 1ULL, 1ULL << 40U, 1'000'000'000'000ULL, 94'143'178'827ULL }) {
    for (std::size_t count : { 10u, 1000u, 10'000u }) {
      std::vector<std::uint64_t> keys(count);
      for (std::size_t i = 0; i < count; ++i) keys[i] = multiplier * i;

      stdex::ptr_hash<std::uint64_t> fast_table;
      CHECK(fast_table.try_build(keys));
      CHECK(check_indices(fast_table, keys, fast_table.capacity()));

      stdex::ptr_hash<std::uint64_t, stdex::hash<std::uint64_t>, stdex::ptr_hash_heap_option_compact<std::allocator<std::uint64_t>>> compact_table;
      CHECK(compact_table.try_build(keys));
      CHECK(check_indices(compact_table, keys, compact_table.size()));
    }
  }

  {
    std::vector<std::uint64_t> keys(13'740);
    for (std::size_t i = 0; i < keys.size(); ++i) keys[i] = (i % 97U) * 7919U + i;
    stdex::ptr_hash<std::uint64_t> fast_table;
    CHECK(fast_table.try_build(keys));
    CHECK(check_indices(fast_table, keys, fast_table.capacity()));
  }
}

void test_fixed_capacity()
{
  std::printf("\n=== ptrhash: fixed capacity ===\n");

  using fixed_fast = stdex::ptr_hash<std::uint64_t, stdex::hash<std::uint64_t>, stdex::ptr_hash_stack_option_default<512>>;
  using fixed_balanced = stdex::ptr_hash<std::uint64_t, stdex::hash<std::uint64_t>, stdex::ptr_hash_stack_option_balanced<512>>;

  for (std::size_t count : { 0u, 1u, 2u, 3u, 4u, 8u, 16u, 32u, 64u, 128u, 256u, 512u }) {
    std::vector<std::uint64_t> const keys = make_random_keys(count, count * 7919u + 17u);

    fixed_fast fast_table;
    CHECK(fast_table.try_build(keys));
    CHECK(fast_table.size() == count);
    CHECK(fast_table.capacity() >= count);
    CHECK(check_indices(fast_table, keys, fast_table.capacity()));
    if (count != 0) CHECK(check_absent_keys(fast_table, 8, fast_table.capacity(), count * 104729u + 3u));

    fixed_balanced balanced_table;
    CHECK(balanced_table.try_build(keys));
    CHECK(balanced_table.size() == count);
    CHECK(check_indices(balanced_table, keys, balanced_table.size()));
    if (count != 0) CHECK(check_absent_keys(balanced_table, 8, balanced_table.size(), count * 104729u + 5u));
  }

  {
    std::vector<std::uint64_t> const keys = make_random_keys(100, 987);
    fixed_balanced table(keys.begin(), keys.end());
    CHECK(table.size() == keys.size());
    CHECK(check_indices(table, keys, table.size()));
  }

  {
    std::vector<std::uint64_t> keys(512);
    for (std::size_t i = 0; i < keys.size(); ++i) keys[i] = i;
    stdex::ptr_hash<std::uint64_t, identity_hasher, stdex::ptr_hash_stack_option_default<512>> table;
    CHECK(table.try_build(keys));
    CHECK(check_indices(table, keys, table.capacity()));
  }

  {
    std::vector<std::uint64_t> const keys = make_random_keys(128, 654);
    stdex::ptr_hash<std::uint64_t, stdex::hash<std::uint64_t>, stdex::ptr_hash_stack_option_compact<128>> table;
    CHECK(table.try_build(keys));
    CHECK(check_indices(table, keys, table.size()));
  }

  {
    std::vector<std::uint64_t> const keys = make_random_keys(16, 17);
    stdex::ptr_hash<std::uint64_t, stdex::hash<std::uint64_t>, stdex::ptr_hash_stack_option_default<16>> table;
    CHECK(table.try_build(keys));
    CHECK(check_indices(table, keys, table.capacity()));
  }

  {
    std::vector<std::uint64_t> const keys = make_random_keys(256, 321);
    using table_type = stdex::ptr_hash<std::uint64_t, stdex::hash<std::uint64_t>, stdex::ptr_hash_stack_option_balanced<256>>;
    allocation_stats::allocations   = 0;
    allocation_stats::deallocations = 0;
    table_type table(keys.begin(), keys.end());
    CHECK(table.size() == keys.size());
    CHECK(check_indices(table, keys, table.size()));
    CHECK(allocation_stats::allocations == 0);
    CHECK(allocation_stats::deallocations == 0);
  }

  {
    std::vector<std::uint64_t> const keys = make_random_keys(9, 11);
    stdex::ptr_hash<std::uint64_t, stdex::hash<std::uint64_t>, stdex::ptr_hash_stack_option_default<8>> table;
    bool threw = false;
    try {
      (void) table.try_build(keys);
    } catch (std::length_error const &) {
      threw = true;
    }
    CHECK(threw);
    CHECK(table.empty());
    CHECK(table.capacity() == 0);
  }

  {
    std::vector<std::uint64_t> keys { 1, 2, 3, 4, 5 };
    stdex::ptr_hash<std::uint64_t, constant_hasher, stdex::ptr_hash_stack_option_default<8>> table;
    CHECK(!table.try_build(keys));
    CHECK(table.empty());
    CHECK(table.capacity() == 0);
  }
}

void test_regressions()
{
  std::printf("\n=== ptrhash: regressions ===\n");

  {
    std::vector<std::uint64_t> keys { 1, 2, 3, 4, 4 };
    stdex::ptr_hash<std::uint64_t> table;
    CHECK(!table.try_build(keys));
    CHECK(table.empty());
    CHECK(table.size() == 0);
    CHECK(table.capacity() == 0);
    CHECK(table.bucket_count() == 0);
    CHECK(table.index(1) == stdex::ptr_hash<std::uint64_t>::npos);
  }

  {
    std::vector<std::uint64_t> const keys = make_random_keys(10, 11);
    stdex::ptr_hash<std::uint64_t> table;
    CHECK(table.try_build(keys));
    std::vector<std::uint64_t> const empty;
    CHECK(table.try_build(empty));
    CHECK(table.empty());
    CHECK(table.size() == 0);
    CHECK(table.capacity() == 0);
    CHECK(table.bucket_count() == 0);
  }

  {
    std::vector<std::uint64_t> keys { 1, 2, 3 };
    stdex::ptr_hash<std::uint64_t, stdex::hash<std::uint64_t>, options_zero_evictions<std::allocator<std::uint64_t>>> table;
    CHECK(table.try_build(keys));
    CHECK(check_indices(table, keys, table.capacity()));
  }

  {
    std::vector<std::uint64_t> const keys = make_random_keys(99, 17);
    stdex::ptr_hash<std::uint64_t> table;
    CHECK(table.try_build(keys));
    CHECK(table.capacity() == 100);
  }

  {
    std::vector<std::uint64_t> keys { 1, 2, 3 };
    stdex::ptr_hash<std::uint64_t, stdex::hash<std::uint64_t>, options_tiny_load<std::allocator<std::uint64_t>>> table;
    bool threw = false;
    try {
      (void) table.try_build(keys);
    } catch (std::length_error const &) {
      threw = true;
    }
    CHECK(threw);
    CHECK(table.empty());
    CHECK(table.capacity() == 0);
  }
}
} // namespace

int main()
{
  test_basic_construction();
  test_key_types();
  test_error_paths();
  test_copy_move_allocator();
  test_initializer_list_and_ranges();
  test_fixed_capacity();
  test_stress();
  test_regressions();

  std::printf("\n===============================\n");
  std::printf("PASS: %d   FAIL: %d\n", g_pass, g_fail);
  std::printf("===============================\n");
  return g_fail == 0 ? 0 : 1;
}
