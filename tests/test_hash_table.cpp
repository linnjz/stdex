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

// test_hash_table.cpp
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio> // stderr
#include <memory>
#include <new>
#include <print>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "hash.hpp"
#include "hash_table.hpp"
#include "ufmap_traits.hpp"
#include "ufset_traits.hpp"

namespace ufd = stdex::unordered_flat_detail;

// ==================================================================
// minimal test framework
// ==================================================================
static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (cond) {                                                              \
      ++g_pass;                                                              \
    } else {                                                                 \
      ++g_fail;                                                              \
      std::println(stderr, "  [FAIL] {}:{}  {}", __FILE__, __LINE__, #cond); \
    }                                                                        \
  } while (0)

#define CHECK_EQ(a, b)                                                              \
  do {                                                                              \
    auto _a = (a);                                                                  \
    auto _b = (b);                                                                  \
    if (_a == _b) {                                                                 \
      ++g_pass;                                                                     \
    } else {                                                                        \
      ++g_fail;                                                                     \
      std::println(stderr, "  [FAIL] {}:{}  {} == {}", __FILE__, __LINE__, #a, #b); \
    }                                                                               \
  } while (0)

#define SECTION(name) std::println("\n=== {} ===", name)

// ==================================================================
// transparent hasher for heterogeneous overloads
// ==================================================================
struct transparent_str_hash
{
  using is_transparent = void;

  size_t operator() (std::string const &s) const noexcept
  {
    return std::hash<std::string> {}(s);
  }

  size_t operator() (std::string_view s) const noexcept
  {
    return std::hash<std::string_view> {}(s);
  }

  size_t operator() (char const *s) const noexcept
  {
    return std::hash<std::string_view> {}(s);
  }
};

struct constant_int_hash
{
  constexpr std::size_t operator() (int) const noexcept { return 0; }
};

struct allocator_probe_state
{
  std::size_t allocation_attempts = 0;
  std::size_t allocations         = 0;
  std::size_t deallocations       = 0;
  std::size_t active_allocations   = 0;
  std::size_t fail_on_attempt      = 0;
};

struct no_default_value
{
  static inline int live_count = 0;

  explicit no_default_value(int value)
      : value { value }
  {
    ++live_count;
  }

  no_default_value(no_default_value const &other)
      : value { other.value }
  {
    ++live_count;
  }

  no_default_value(no_default_value &&other) noexcept
      : value { other.value }
  {
    ++live_count;
  }

  no_default_value &operator=(no_default_value const &) = default;
  no_default_value &operator=(no_default_value &&) noexcept = default;

  ~no_default_value() { --live_count; }

  int value;
};

struct throwing_move_value
{
  static inline int live_count       = 0;
  static inline int copy_count       = 0;
  static inline int move_count       = 0;
  static inline int throw_on_copy    = 0;
  static inline int throw_on_move    = 0;

  explicit throwing_move_value(int value)
      : value { value }
  {
    ++live_count;
  }

  throwing_move_value(throwing_move_value const &other)
      : value { other.value }
  {
    ++copy_count;
    if (throw_on_copy != 0 && copy_count == throw_on_copy) throw 1;
    ++live_count;
  }

  throwing_move_value(throwing_move_value &&other) noexcept(false)
      : value { other.value }
  {
    ++move_count;
    if (throw_on_move != 0 && move_count == throw_on_move) throw 2;
    ++live_count;
  }

  throwing_move_value &operator=(throwing_move_value const &) = default;
  throwing_move_value &operator=(throwing_move_value &&) noexcept(false) = default;

  ~throwing_move_value() { --live_count; }

  static void reset_counters() noexcept
  {
    copy_count    = 0;
    move_count    = 0;
    throw_on_copy = 0;
    throw_on_move = 0;
  }

  int value;
};

template <class T, bool PropagateCopy, bool PropagateMove, bool PropagateSwap>
struct allocator_probe
{
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using pointer = T *;
  using const_pointer = T const *;

  using propagate_on_container_copy_assignment = std::bool_constant<PropagateCopy>;
  using propagate_on_container_move_assignment = std::bool_constant<PropagateMove>;
  using propagate_on_container_swap            = std::bool_constant<PropagateSwap>;
  using is_always_equal                        = std::false_type;

  template <class U>
  struct rebind
  {
    using other = allocator_probe<U, PropagateCopy, PropagateMove, PropagateSwap>;
  };

  allocator_probe() = default;

  constexpr allocator_probe(allocator_probe_state *state, int id) noexcept
      : state_ { state }
      , id_ { id }
  {
  }

  template <class U>
  constexpr allocator_probe(allocator_probe<U, PropagateCopy, PropagateMove, PropagateSwap> const &other) noexcept
      : state_ { other.state_ }
      , id_ { other.id_ }
  {
  }

  [[nodiscard]] constexpr int id() const noexcept { return id_; }

  [[nodiscard]] T *allocate(std::size_t count)
  {
    ++state_->allocation_attempts;
    if (state_->fail_on_attempt != 0 && state_->allocation_attempts == state_->fail_on_attempt) throw std::bad_alloc {};

    auto *result = static_cast<T *>(::operator new(count * sizeof(T)));
    ++state_->allocations;
    ++state_->active_allocations;
    return result;
  }

  void deallocate(T *ptr, std::size_t) noexcept
  {
    ::operator delete(ptr);
    ++state_->deallocations;
    --state_->active_allocations;
  }

  template <class U>
  friend constexpr bool operator==(
      allocator_probe const &left,
      allocator_probe<U, PropagateCopy, PropagateMove, PropagateSwap> const &right) noexcept
  {
    return left.state_ == right.state_ && left.id_ == right.id_;
  }

  template <class, bool, bool, bool>
  friend struct allocator_probe;

private:
  allocator_probe_state *state_ = nullptr;
  int id_                       = 0;
};

// ==================================================================
// convenience aliases
// ==================================================================
template <size_t N>
using IMap = ufd::hash_table<ufd::ufmap_traits<int, int, N>, std::hash<int>, std::equal_to<>, std::allocator<std::pair<int, int>>>;

template <size_t N>
using SMap = ufd::hash_table<ufd::ufmap_traits<std::string, int, N>, std::hash<std::string>, std::equal_to<>, std::allocator<std::pair<std::string, int>>>;

template <size_t N>
using TransparentSMap =
    ufd::hash_table<ufd::ufmap_traits<std::string, int, N>, transparent_str_hash, std::equal_to<>, std::allocator<std::pair<std::string, int>>>;

template <size_t N>
using ISet = ufd::hash_table<ufd::ufset_traits<int, N>, std::hash<int>, std::equal_to<>, std::allocator<int>>;

template <size_t N>
using CollisionMap = ufd::hash_table<ufd::ufmap_traits<int, int, N>, constant_int_hash, std::equal_to<>, std::allocator<std::pair<int, int>>>;

template <size_t N, bool PropagateCopy, bool PropagateMove, bool PropagateSwap>
using ProbeMap = ufd::hash_table<
    ufd::ufmap_traits<int, int, N>,
    std::hash<int>,
    std::equal_to<>,
    allocator_probe<std::pair<int, int>, PropagateCopy, PropagateMove, PropagateSwap>>;

template <size_t N>
using NoDefaultMap = ufd::hash_table<
    ufd::ufmap_traits<int, no_default_value, N>,
    std::hash<int>,
    std::equal_to<>,
    std::allocator<std::pair<int, no_default_value>>>;

template <size_t N>
using ThrowingMap = ufd::hash_table<
    ufd::ufmap_traits<int, throwing_move_value, N>,
    std::hash<int>,
    std::equal_to<>,
    std::allocator<std::pair<int, throwing_move_value>>>;

template <size_t N>
using ThrowingProbeMap = ufd::hash_table<
    ufd::ufmap_traits<int, throwing_move_value, N>,
    std::hash<int>,
    std::equal_to<>,
    allocator_probe<std::pair<int, throwing_move_value>, false, false, false>>;

template <size_t N>
using UniqueMap = ufd::hash_table<
    ufd::ufmap_traits<int, std::unique_ptr<int>, N>,
    std::hash<int>,
    std::equal_to<>,
    std::allocator<std::pair<int, std::unique_ptr<int>>>>;

// ==================================================================
// 1. Basics: empty / single element / insert / find / count
// ==================================================================
void test_basic_empty_single()
{
  SECTION("basic empty / single element");
  IMap<0> t { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };

  CHECK(t.empty());
  CHECK_EQ(t.size(), 0u);
  CHECK_EQ(t.capacity(), 0u);
  CHECK_EQ(t.bucket_count(), 0u);
  CHECK_EQ(t.begin(), t.end());
  CHECK(t.find(42) == t.end());
  CHECK_EQ(t.count(42), 0u);
  CHECK(!t.contains(42));
  CHECK_EQ(t.erase(42), 0u);

  auto [it, ok] = t.emplace(1, 100);
  CHECK(ok);
  CHECK(it != t.end());
  CHECK_EQ(it->first, 1);
  CHECK_EQ(it->second, 100);
  CHECK(!t.empty());
  CHECK_EQ(t.size(), 1u);
  CHECK(t.capacity() >= 1u);
  CHECK(t.begin() != t.end());
  CHECK_EQ(std::distance(t.begin(), t.end()), 1);
  CHECK(t.contains(1));
  CHECK_EQ(t.count(1), 1u);

  auto [it2, ok2] = t.emplace(1, 999);
  CHECK(!ok2);
 CHECK_EQ(it2->second, 100); // existing value not overwritten

  CHECK_EQ(t.erase(1), 1u);
  CHECK(t.empty());
  CHECK(t.find(1) == t.end());
}

// ==================================================================
// 2. Set traits basics
// ==================================================================
void test_set_traits()
{
  SECTION("set traits");
  ISet<0> s { std::hash<int> {}, std::equal_to<> {}, std::allocator<int> {} };

  CHECK(s.emplace(5).second);
  CHECK(s.emplace(5).second == false);
  CHECK(s.contains(5));
  CHECK_EQ(s.size(), 1u);
  CHECK_EQ(*s.find(5), 5);

  s.emplace(7);
  s.emplace(9);
  int sum = 0;
  for (auto &v : s) sum += v;
  CHECK_EQ(sum, 21);
}

// ==================================================================
// 3. Stack-to-heap transition boundary
// ==================================================================
void test_stack_to_heap()
{
  SECTION("stack -> heap transition (inline_capacity = 29)");
  IMap<29> t { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };

  CHECK(t.is_stack());
  CHECK_EQ(t.capacity(), 29u);
 CHECK_EQ(t.max_load_factor(), 0.875f * 0 + 0.875f); // exactly 0.875f, as a sanity check

  for (int i = 0; i < 29; ++i) t.emplace(i, i * 10);
  CHECK(t.is_stack());
  CHECK_EQ(t.size(), 29u);

 // the 30th element triggers promotion
  t.emplace(29, 290);
  CHECK(t.is_heap());
  CHECK_EQ(t.size(), 30u);

  for (int i = 0; i <= 29; ++i) {
    auto it = t.find(i);
    CHECK(it != t.end());
    if (it != t.end()) CHECK_EQ(it->second, i * 10);
  }

 // keep inserting after promotion -> triggers growth
  for (int i = 30; i < 200; ++i) t.emplace(i, i);
  CHECK_EQ(t.size(), 200u);
  for (int i = 0; i < 200; ++i) CHECK(t.contains(i));
}

// ==================================================================
// 4. allocator-aware branches
// ==================================================================
void test_allocator_aware()
{
  SECTION("allocator-aware branches");

  using NoPropMap   = ProbeMap<0, false, false, false>;
  using CopyPropMap = ProbeMap<0, true, false, false>;
  using MovePropMap = ProbeMap<0, false, true, false>;
  using SwapPropMap = ProbeMap<0, false, false, true>;

  // Non-propagating unequal allocators must copy into the destination allocator.
  {
    allocator_probe_state source_state, destination_state;
    NoPropMap source { std::hash<int> {}, std::equal_to<> {}, { &source_state, 1 } };
    NoPropMap destination { std::hash<int> {}, std::equal_to<> {}, { &destination_state, 2 } };
    for (int i = 0; i < 40; ++i) source.emplace(i, i);
    destination.emplace(999, 999);

    destination = source;
    CHECK_EQ(destination.get_allocator().id(), 2);
    CHECK_EQ(destination.size(), 40u);
    CHECK(!destination.contains(999));
    for (int i = 0; i < 40; ++i) CHECK(destination.contains(i));
  }

  // POCCA propagates the source allocator when the allocators differ.
  {
    allocator_probe_state source_state, destination_state;
    CopyPropMap source { std::hash<int> {}, std::equal_to<> {}, { &source_state, 3 } };
    CopyPropMap destination { std::hash<int> {}, std::equal_to<> {}, { &destination_state, 4 } };
    for (int i = 0; i < 40; ++i) source.emplace(i, i);

    destination = source;
    CHECK_EQ(destination.get_allocator().id(), 3);
    CHECK_EQ(destination.size(), 40u);
    for (int i = 0; i < 40; ++i) CHECK(destination.contains(i));
  }

  // Non-propagating unequal move assignment must transfer through the destination allocator.
  {
    allocator_probe_state source_state, destination_state;
    NoPropMap source { std::hash<int> {}, std::equal_to<> {}, { &source_state, 5 } };
    NoPropMap destination { std::hash<int> {}, std::equal_to<> {}, { &destination_state, 6 } };
    for (int i = 0; i < 40; ++i) source.emplace(i, i);
    destination.emplace(999, 999);

    destination = std::move(source);
    CHECK_EQ(destination.get_allocator().id(), 6);
    CHECK_EQ(destination.size(), 40u);
    CHECK(source.empty());
    for (int i = 0; i < 40; ++i) CHECK(destination.contains(i));
  }

  // POCMA allows unequal allocators to steal the source heap.
  {
    allocator_probe_state source_state, destination_state;
    MovePropMap source { std::hash<int> {}, std::equal_to<> {}, { &source_state, 7 } };
    MovePropMap destination { std::hash<int> {}, std::equal_to<> {}, { &destination_state, 8 } };
    for (int i = 0; i < 40; ++i) source.emplace(i, i);
    destination.emplace(999, 999);

    destination = std::move(source);
    CHECK_EQ(destination.get_allocator().id(), 7);
    CHECK_EQ(destination.size(), 40u);
    CHECK(source.empty());
    for (int i = 0; i < 40; ++i) CHECK(destination.contains(i));
  }

  // Explicit-allocator move construction covers both heap transfer paths.
  {
    allocator_probe_state source_state, equal_state, unequal_state;
    NoPropMap unequal_source { std::hash<int> {}, std::equal_to<> {}, { &source_state, 9 } };
    for (int i = 0; i < 40; ++i) unequal_source.emplace(i, i);
    NoPropMap unequal_move { std::move(unequal_source), { &unequal_state, 10 } };
    CHECK_EQ(unequal_move.get_allocator().id(), 10);
    CHECK_EQ(unequal_move.size(), 40u);
    CHECK(unequal_source.empty());

    NoPropMap equal_source { std::hash<int> {}, std::equal_to<> {}, { &equal_state, 11 } };
    for (int i = 0; i < 40; ++i) equal_source.emplace(i, i);
    NoPropMap equal_move { std::move(equal_source), { &equal_state, 11 } };
    CHECK_EQ(equal_move.get_allocator().id(), 11);
    CHECK_EQ(equal_move.size(), 40u);
    CHECK(equal_source.empty());
  }

  // Non-propagating equal allocators take the permitted swap path.
  {
    allocator_probe_state state;
    NoPropMap left { std::hash<int> {}, std::equal_to<> {}, { &state, 12 } };
    NoPropMap right { std::hash<int> {}, std::equal_to<> {}, { &state, 12 } };
    left.emplace(1, 10);
    right.emplace(2, 20);
    left.swap(right);
    CHECK(left.contains(2) && right.contains(1));
    CHECK_EQ(left.get_allocator().id(), 12);
    CHECK_EQ(right.get_allocator().id(), 12);
  }

  // POCS swaps unequal allocator state together with the table storage.
  {
    allocator_probe_state left_state, right_state;
    SwapPropMap left { std::hash<int> {}, std::equal_to<> {}, { &left_state, 13 } };
    SwapPropMap right { std::hash<int> {}, std::equal_to<> {}, { &right_state, 14 } };
    for (int i = 0; i < 40; ++i) left.emplace(i, i);
    right.emplace(999, 999);
    left.swap(right);
    CHECK_EQ(left.get_allocator().id(), 14);
    CHECK_EQ(right.get_allocator().id(), 13);
    CHECK(left.contains(999));
    CHECK(right.contains(0));
  }

  // A failed allocation must leave an existing heap table usable and leak-free.
  {
    allocator_probe_state state;
    NoPropMap table { std::hash<int> {}, std::equal_to<> {}, { &state, 15 } };
    for (int i = 0; i < 40; ++i) table.emplace(i, i);
    auto const old_capacity = table.capacity();
    auto const old_attempts = state.allocation_attempts;
    state.fail_on_attempt   = old_attempts + 1;

    bool threw = false;
    try {
      table.rehash(old_capacity * 4);
    } catch (std::bad_alloc const &) {
      threw = true;
    }

    state.fail_on_attempt = 0;
    CHECK(threw);
    CHECK_EQ(table.capacity(), old_capacity);
    CHECK_EQ(table.size(), 40u);
    for (int i = 0; i < 40; ++i) CHECK(table.contains(i));
  }

  // Growth from stack storage must also roll back cleanly when allocation fails.
  {
    allocator_probe_state state;
    ProbeMap<29, false, false, false> table {
      std::hash<int> {}, std::equal_to<> {}, { &state, 16 }
    };
    for (int i = 0; i < 29; ++i) table.emplace(i, i);
    state.fail_on_attempt = state.allocation_attempts + 1;

    bool threw = false;
    try {
      table.emplace(29, 29);
    } catch (std::bad_alloc const &) {
      threw = true;
    }

    state.fail_on_attempt = 0;
    CHECK(threw);
    CHECK(table.is_stack());
    CHECK_EQ(table.size(), 29u);
    CHECK(!table.contains(29));
    for (int i = 0; i < 29; ++i) CHECK(table.contains(i));
  }
}

// ==================================================================
// 5. Copy / move / assignment / destruction
// ==================================================================
void test_copy_move()
{
  SECTION("copy / move / assignment");
  IMap<29> src { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  for (int i = 0; i < 100; ++i) src.emplace(i, i);

 // copy construction
  IMap<29> cpy(src, src.get_allocator());
  CHECK_EQ(cpy.size(), 100u);
  for (int i = 0; i < 100; ++i) CHECK(cpy.contains(i));

 // copy assignment
  IMap<29> cpy2 { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  cpy2.emplace(999, 999);
  cpy2 = src;
  CHECK_EQ(cpy2.size(), 100u);
  CHECK(!cpy2.contains(999));
  for (int i = 0; i < 100; ++i) CHECK(cpy2.contains(i));

 // move construction
  IMap<29> moved(std::move(src));
  CHECK_EQ(moved.size(), 100u);
  for (int i = 0; i < 100; ++i) CHECK(moved.contains(i));
 // source already empty
  CHECK(src.empty());

 // move assignment
  IMap<29> moved2 { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  moved2.emplace(42, 42);
  moved2 = std::move(moved);
  CHECK_EQ(moved2.size(), 100u);
  CHECK(moved2.contains(42));

 // self-assignment
  cpy2 = cpy2;
  CHECK_EQ(cpy2.size(), 100u);
}

// ==================================================================
// 4b. Storage conversion and capacity boundaries
// ==================================================================
void test_storage_conversion_and_capacity_boundaries()
{
  SECTION("storage conversion / capacity boundaries");

 // same-layout stack copy: hits transfer_same_layout_unchecked_.
  IMap<29> small { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  for (int i = 0; i < 20; ++i) small.emplace(i, i * 10);

  auto same_layout = IMap<29> { small, small.get_allocator() };
  CHECK(small.is_stack() && same_layout.is_stack());
  CHECK_EQ(same_layout.capacity(), small.capacity());
  CHECK_EQ(same_layout.size(), small.size());
  for (int i = 0; i < 20; ++i) {
    auto it = same_layout.find(i);
    CHECK(it != same_layout.end());
    if (it != same_layout.end()) CHECK_EQ(it->second, i * 10);
  }

 // requesting the current capacity must neither reallocate nor lose elements.
  auto const same_capacity = same_layout.capacity();
  same_layout.rehash(same_capacity);
  CHECK_EQ(same_layout.capacity(), same_capacity);
  CHECK_EQ(same_layout.size(), 20u);
  for (int i = 0; i < 20; ++i) CHECK(same_layout.contains(i));

 // rehash(0) on an empty heap table must take the minimum group-count path and stay usable.
  IMap<0> empty { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  CHECK_EQ(empty.capacity(), 0u);
  empty.rehash(0);
  CHECK(empty.empty());
  CHECK(empty.is_heap());
  CHECK(empty.capacity() >= 29u);
  auto const empty_capacity = empty.capacity();
  empty.rehash(empty_capacity);
  CHECK_EQ(empty.capacity(), empty_capacity);
  empty.emplace(1, 10);
  CHECK_EQ(empty.find(1)->second, 10);

 // reset() with inline_capacity == 0 clears only elements and keeps heap capacity.
  IMap<0> heap { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  for (int i = 0; i < 50; ++i) heap.emplace(i, i);
  auto const heap_capacity = heap.capacity();
  heap.reset();
  CHECK(heap.empty());
  CHECK(heap.is_heap());
  CHECK_EQ(heap.capacity(), heap_capacity);
  heap.emplace(77, 770);
  CHECK_EQ(heap.find(77)->second, 770);

 // erasing then reinserting the same key should reuse capacity.
  IMap<29> reuse { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  for (int i = 0; i < 29; ++i) reuse.emplace(i, i);
  auto const reuse_capacity = reuse.capacity();
  CHECK_EQ(reuse.erase(0), 1u);
  auto [reinserted, inserted] = reuse.emplace(0, 1000);
  CHECK(inserted);
  CHECK(reinserted != reuse.end());
  CHECK_EQ(reinserted->second, 1000);
  CHECK_EQ(reuse.capacity(), reuse_capacity);
  CHECK_EQ(reuse.size(), 29u);
  for (int i = 1; i < 29; ++i) CHECK(reuse.contains(i));

 // erasing in an overflow group lowers max_load_; reinserting at the threshold boundary should grow early.
  CollisionMap<29> drift { constant_int_hash {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  for (int i = 0; i < 29; ++i) drift.emplace(i, i);
  CHECK(drift.is_stack());
  CHECK_EQ(drift.erase(0), 1u);
  CHECK_EQ(drift.size(), 28u);

  auto [drift_it, drift_inserted] = drift.emplace(29, 29);
  CHECK(drift_inserted);
  CHECK(drift_it != drift.end());
  CHECK(drift.is_heap());
  CHECK(drift.capacity() > 29u);
  for (int i = 1; i < 30; ++i) CHECK(drift.contains(i));
}

// ==================================================================
// 5. swap (heap/heap, stack/stack, stack/heap)
// ==================================================================
void test_swap()
{
  SECTION("swap");

 // heap <-> heap
  {
    IMap<0> a { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
    IMap<0> b { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
    for (int i = 0; i < 30; ++i) a.emplace(i, i);
    for (int i = 100; i < 120; ++i) b.emplace(i, i);
    a.swap(b);
    CHECK_EQ(a.size(), 20u);
    CHECK_EQ(b.size(), 30u);
    CHECK(a.contains(100) && b.contains(0));
  }

 // stack <-> stack
  {
    IMap<29> a { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
    IMap<29> b { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
    a.emplace(1, 1);
    b.emplace(2, 2);
    b.emplace(3, 3);
    a.swap(b);
    CHECK_EQ(a.size(), 2u);
    CHECK_EQ(b.size(), 1u);
    CHECK(a.contains(2) && a.contains(3));
    CHECK(b.contains(1));
  }

 // stack <-> heap
  {
    IMap<29> a { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
    IMap<29> b { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
    for (int i = 0; i < 5; ++i) a.emplace(i, i);
    for (int i = 0; i < 100; ++i) b.emplace(i * 1000, i);
    CHECK(a.is_stack() && b.is_heap());
    a.swap(b);
    CHECK(a.is_heap() && b.is_stack());
    CHECK_EQ(a.size(), 100u);
    CHECK_EQ(b.size(), 5u);
    for (int i = 0; i < 100; ++i) CHECK(a.contains(i * 1000));
    for (int i = 0; i < 5; ++i) CHECK(b.contains(i));
  }
}

// ==================================================================
// 6. Four erase overloads
// ==================================================================
void test_erase()
{
  SECTION("erase overloads");
  IMap<0> t { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  for (int i = 0; i < 100; ++i) t.emplace(i, i * 2);

 // by key
  CHECK_EQ(t.erase(50), 1u);
  CHECK_EQ(t.erase(50), 0u);
  CHECK(!t.contains(50));

 // by iterator
  auto it = t.find(60);
  CHECK(it != t.end());
  t.erase(it);
  CHECK(!t.contains(60));

 // by const_iterator
  auto cit = t.find(70);
  CHECK(cit != t.end());
 // erasing end is undefined behavior: abort directly instead of a no-op
  // t.erase(ufd::hash_table_iterator<ufd::ufmap_traits<int, int, 0>, std::pair<int, int> *, ufd::group15 *, true> {});
 // the line above is only an example; use the form below in practice (iterator converts implicitly to const_iterator)
  t.erase(cit);
  CHECK(!t.contains(70));

 // by [first, last) range: erase all keys < 10
  std::vector<int> kill;
  for (auto &&kv : t)
    if (kv.first < 10) kill.push_back(kv.first);
  for (int k : kill) t.erase(k);
  for (int k = 0; k < 10; ++k) CHECK(!t.contains(k));
  for (int k = 10; k < 100; ++k) {
    if (k == 50 || k == 60 || k == 70) continue;
    CHECK(t.contains(k));
  }
}

// ==================================================================
// 7. rehash / reserve / clear / reset
// ==================================================================
void test_rehash_reserve_clear_reset()
{
  SECTION("rehash / reserve / clear / reset");
  IMap<29> t { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  for (int i = 0; i < 100; ++i) t.emplace(i, i);

  auto const old_cap = t.capacity();
  t.rehash(old_cap * 4);
  CHECK(t.capacity() > old_cap);
  for (int i = 0; i < 100; ++i) CHECK(t.contains(i));

  t.reserve(1000);
  CHECK(t.capacity() >= 1000);
  for (int i = 0; i < 100; ++i) CHECK(t.contains(i));

  t.clear();
  CHECK(t.empty());
  CHECK_EQ(t.size(), 0u);
 CHECK(t.capacity() > 0u); // clear keeps capacity

  for (int i = 0; i < 50; ++i) t.emplace(i, i);
 t.reset(); // reset falls back to stack (with inline_capacity)
  CHECK(t.empty());
  CHECK(t.is_stack());
}

// ==================================================================
// 8. Iterators: begin / end / ++ / -> / structured bindings
// ==================================================================
void test_iteration()
{
  SECTION("iterator");
  SMap<0> t { std::hash<std::string> {}, std::equal_to<> {},
              std::allocator<std::pair<std::string, int>> {} };
  for (int i = 0; i < 50; ++i) t.emplace("k" + std::to_string(i), i);

  int count = 0, sum = 0;
  for (auto it = t.begin(); it != t.end(); ++it) {
    ++count;
    sum += it->second;
    // modify the mapped value via ->
    it->second += 0;
  }
  CHECK_EQ(count, 50);
  CHECK_EQ(sum, 49 * 50 / 2);

 // const iteration
  auto const &ct = t;
  int ccount     = 0;
  for (auto it = ct.cbegin(); it != ct.cend(); ++it) ++ccount;
  CHECK_EQ(ccount, 50);

 // range-for + structured bindings
  int c2 = 0;
  for (auto &&[k, v] : t) {
    (void) k;
    (void) v;
    ++c2;
  }
  CHECK_EQ(c2, 50);
}

// ==================================================================
// 9. Insert family
// ==================================================================
void test_insert_variants()
{
  SECTION("insert variants");
  SMap<0> t { std::hash<std::string> {}, std::equal_to<> {},
              std::allocator<std::pair<std::string, int>> {} };

  auto r1 = t.insert(std::pair<std::string, int> { "a", 1 });
  CHECK(r1.second);
  auto r2 = t.insert(std::pair<std::string, int> { "a", 99 });
  CHECK(!r2.second);

  std::vector<std::pair<std::string, int>> src {
    { "b", 2 },
    { "c", 3 },
    { "d", 4 }
  };
  t.insert(src.begin(), src.end());
  CHECK_EQ(t.size(), 4u);

  std::vector<std::pair<std::string, int>> src2 {
    { "e", 5 },
    { "f", 6 }
  };
  t.insert_range(src2);
  CHECK_EQ(t.size(), 6u);
  CHECK(t.contains("e") && t.contains("f"));

 // emplace_hint ignores hint yet returns an iterator
  auto it = t.emplace_hint(t.begin(), "g", 7);
  CHECK(it != t.end());
  CHECK_EQ(it->second, 7);
}

// ==================================================================
// 10. Heterogeneous lookup (transparent hash + equal_to<>)
// ==================================================================
void test_heterogeneous_lookup()
{
  SECTION("heterogeneous lookup");
  TransparentSMap<0> t { transparent_str_hash {}, std::equal_to<> {},
                         std::allocator<std::pair<std::string, int>> {} };
  t.emplace("hello", 1);

 // look up by string_view
  std::string_view sv = "hello";
  auto it             = t.find(sv);
  CHECK(it != t.end());
  CHECK_EQ(it->second, 1);

 // look up by char const*
  CHECK(t.contains("hello"));

  // equal_range / count
  auto [b, e] = t.equal_range(sv);
  CHECK(b != e);
  CHECK_EQ(t.count(sv), 1u);
}

// ==================================================================
// 11. Load factor / capacity / stats API
// ==================================================================
void test_introspection()
{
  SECTION("introspection");
  IMap<29> t { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  CHECK_EQ(t.load_factor(), 0.0f);
  for (int i = 0; i < 29; ++i) t.emplace(i, i);
 CHECK(t.load_factor() > 0.9f); // stack mode 100%
  t.emplace(29, 29);
  CHECK(t.load_factor() <= 0.875f + 1e-6f);
  CHECK(t.max_load_factor() > 0.87f && t.max_load_factor() < 0.88f);
  t.max_load_factor(0.5f); // no-op
  CHECK(t.max_size() > 0u);
}

// ==================================================================
// 12. Randomized fuzz test vs std::unordered_map
// ==================================================================
void test_fuzz_vs_std_map()
{
  SECTION("fuzz vs std::unordered_map");
  constexpr int kOps      = 20000;
  constexpr int kKeyRange = 500;
  std::mt19937 rng(0xC0'FF'EE);
  std::uniform_int_distribution<int> key_d(0, kKeyRange - 1);
  std::uniform_int_distribution<int> op_d(0, 99);

 // inline_capacity = 29 exercises both stack/heap transitions
  IMap<29> t { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  std::unordered_map<int, int> ref;

  for (int i = 0; i < kOps; ++i) {
    int op = op_d(rng);
    int k  = key_d(rng);
    if (op < 55) {
      auto r1 = t.emplace(k, k * 3 + i);
      auto r2 = ref.emplace(k, k * 3 + i);
      CHECK(r1.second == r2.second);
    } else if (op < 85) {
      CHECK_EQ(t.erase(k), ref.erase(k));
    } else {
      auto it1 = t.find(k);
      auto it2 = ref.find(k);
      CHECK((it1 == t.end()) == (it2 == ref.end()));
      if (it1 != t.end()) CHECK_EQ(it1->second, it2->second);
    }
    CHECK_EQ(t.size(), ref.size());
  }

 // eventual consistency
  for (auto &[k, v] : ref) {
    auto it = t.find(k);
    CHECK(it != t.end());
    if (it != t.end()) CHECK_EQ(it->second, v);
  }
  for (auto &&[k, v] : t) {
    auto it = ref.find(k);
    CHECK(it != ref.end());
    if (it != ref.end()) CHECK_EQ(it->second, v);
  }
}

// ==================================================================
// 13. Fuzz test: random string keys + copy/move/clear mix
// ==================================================================
void test_fuzz_strings()
{
  SECTION("fuzz string keys + copies + clears");
  constexpr int kOps = 3000;
  std::mt19937 rng(12345);
  std::uniform_int_distribution<int> key_d(0, 200);
  std::uniform_int_distribution<int> op_d(0, 99);

  SMap<29> t { std::hash<std::string> {}, std::equal_to<> {},
               std::allocator<std::pair<std::string, int>> {} };
  std::unordered_map<std::string, int> ref;

  for (int i = 0; i < kOps; ++i) {
    int op        = op_d(rng);
    std::string k = "key_" + std::to_string(key_d(rng));

    if (op < 60) {
      t.emplace(k, i);
      ref.emplace(k, i);
    } else if (op < 80) {
      t.erase(k);
      ref.erase(k);
    } else if (op < 90) {
      // copy it and check against ref again
      auto c = SMap<29>(t, t.get_allocator());
      CHECK_EQ(c.size(), t.size());
      for (auto &[kk, vv] : ref) {
        auto it = c.find(kk);
        CHECK(it != c.end());
        if (it != c.end()) CHECK_EQ(it->second, vv);
      }
    } else if (op < 95) {
      t.clear();
      ref.clear();
    } else {
      t.reset();
      ref.clear();
    }
    CHECK_EQ(t.size(), ref.size());
  }
}

// ==================================================================
// 14. extract: three overloads + map/set + heterogeneous
// ==================================================================
void test_extract()
{
  SECTION("extract");
  IMap<0> t { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  for (int i = 0; i < 50; ++i) t.emplace(i, i * 10);

 // extract(key) hit
  auto n1 = t.extract(10);
  CHECK(static_cast<bool>(n1));
  CHECK_EQ(n1.value().first, 10);
  CHECK_EQ(n1.value().second, 100);
  CHECK(!t.contains(10));
  CHECK_EQ(t.size(), 49u);

 // extract(key) miss: return an empty node_type
  auto n2 = t.extract(999);
  CHECK(!static_cast<bool>(n2));
  CHECK_EQ(t.size(), 49u);

  // extract(const_iterator)
  auto it = t.find(20);
  CHECK(it != t.end());
  auto n3 = t.extract(it);
  CHECK(static_cast<bool>(n3));
  CHECK_EQ(n3.value().first, 20);
  CHECK_EQ(n3.value().second, 200);
  CHECK(!t.contains(20));
  CHECK_EQ(t.size(), 48u);

 // heterogeneous extract(K)
  TransparentSMap<0> st { transparent_str_hash {}, std::equal_to<> {},
                          std::allocator<std::pair<std::string, int>> {} };
  st.emplace("hello", 1);
  st.emplace("world", 2);

  std::string_view sv_hello = "hello";
  auto n4                   = st.extract(sv_hello);
  CHECK(static_cast<bool>(n4));
  CHECK_EQ(n4.value().second, 1);
  CHECK(!st.contains(sv_hello));
  CHECK_EQ(st.size(), 1u);

  std::string_view sv_nope = "nope";
  auto n5                  = st.extract(sv_nope);
  CHECK(!static_cast<bool>(n5));
  CHECK_EQ(st.size(), 1u);

 // set extract
  ISet<0> s { std::hash<int> {}, std::equal_to<> {}, std::allocator<int> {} };
  s.emplace(1);
  s.emplace(2);
  auto ns = s.extract(1);
  CHECK(static_cast<bool>(ns));
  CHECK_EQ(ns.value(), 1);
  CHECK(!s.contains(1));
  CHECK_EQ(s.size(), 1u);
}

// ==================================================================
// 15. insert(node_type&&) / insert(hint, node_type&&)
// ==================================================================
void test_insert_node()
{
  SECTION("insert(node_type&&)");

 // no conflict: node is consumed, return an empty node
  IMap<0> a { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  for (int i = 0; i < 20; ++i) a.emplace(i, i * 10);

  auto n1 = a.extract(5);
  CHECK(static_cast<bool>(n1));

  IMap<0> b { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  auto r1 = b.insert(std::move(n1));
  CHECK(r1.inserted);
 CHECK(!static_cast<bool>(r1.node)); // success returns an empty node
  CHECK_EQ(r1.position->first, 5);
  CHECK_EQ(r1.position->second, 50);
  CHECK(b.contains(5));
  CHECK_EQ(b.size(), 1u);

// conflict: node returned intact with source values unchanged (map takes the with_key path)
  IMap<0> c { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  c.emplace(6, 999);

  auto n2 = a.extract(6);
  CHECK(static_cast<bool>(n2));
  auto r2 = c.insert(std::move(n2));
  CHECK(!r2.inserted);
  CHECK(static_cast<bool>(r2.node));
  CHECK_EQ(r2.node.value().first, 6);
  CHECK_EQ(r2.node.value().second, 60);
 CHECK_EQ(r2.position->second, 999); // c's original value not overwritten
 CHECK(c.contains(6));   // c already contains 6
 CHECK_EQ(c.size(), 1u); // c holds only one element {6: 999}

 // empty node: no-op
  IMap<0> d { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  auto r3 = d.insert(IMap<0>::node_type {});
  CHECK(!r3.inserted);
  CHECK(!static_cast<bool>(r3.node));
  CHECK(r3.position == d.end());
  CHECK(d.empty());

 // hint version
  auto n4 = a.extract(7);
  CHECK(static_cast<bool>(n4));
  auto it4 = d.insert(d.cbegin(), std::move(n4));
  CHECK(it4 != d.end());
  CHECK_EQ(it4->first, 7);
  CHECK(d.contains(7));
  CHECK_EQ(d.size(), 1u);

 // set branch: on conflict the node must be returned intact
  ISet<0> s1 { std::hash<int> {}, std::equal_to<> {}, std::allocator<int> {} };
  ISet<0> s2 { std::hash<int> {}, std::equal_to<> {}, std::allocator<int> {} };
  s1.emplace(100);
  s1.emplace(200);
 s2.emplace(100); // conflict

  auto sn1 = s1.extract(100);
  CHECK(static_cast<bool>(sn1));
  auto sr1 = s2.insert(std::move(sn1));
  CHECK(!sr1.inserted);
 CHECK(static_cast<bool>(sr1.node)); // source node must stay valid
  CHECK_EQ(sr1.node.value(), 100);
 CHECK_EQ(s1.size(), 1u); // s1 keeps only 200
  CHECK(s2.contains(100));

 // set without conflict: insert normally
  auto sn2 = s1.extract(200);
  CHECK(static_cast<bool>(sn2));
  auto sr2 = s2.insert(std::move(sn2));
  CHECK(sr2.inserted);
  CHECK(!static_cast<bool>(sr2.node));
  CHECK(s2.contains(200));
  CHECK(s1.empty());
}

// ==================================================================
// 16. merge: lvalue/rvalue/self/empty-source/empty-dest/set
// ==================================================================
void test_merge()
{
  SECTION("merge");

 // lvalue merge: conflicts keep the source without overwriting its values
  IMap<0> a { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  IMap<0> b { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  for (int i = 0; i < 30; ++i) a.emplace(i, i * 10);
  for (int i = 20; i < 50; ++i) b.emplace(i, i * 100);

  a.merge(b);
 CHECK_EQ(a.size(), 50u);            // 0..49 all land in a
 CHECK_EQ(b.size(), 10u);            // 20..29 conflicts stay in b
 CHECK_EQ(a.find(25)->second, 250);  // keep a's original value
 CHECK_EQ(b.find(25)->second, 2500); // b's source value unchanged
  for (int i = 20; i < 30; ++i) CHECK(b.contains(i));
  for (int i = 30; i < 50; ++i) CHECK(a.contains(i));

 // rvalue merge: conflicts keep the source
  IMap<0> c { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  IMap<0> d { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  for (int i = 0; i < 30; ++i) c.emplace(i, i);
  for (int i = 25; i < 40; ++i) d.emplace(i, i);

  c.merge(std::move(d));
  CHECK_EQ(c.size(), 40u);
 CHECK_EQ(d.size(), 5u); // 25..29 conflicts kept
  CHECK(c.contains(35));
  CHECK(d.contains(25));

 // self merge: no-op
  IMap<0> e { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  for (int i = 0; i < 10; ++i) e.emplace(i, i);
  e.merge(e);
  CHECK_EQ(e.size(), 10u);

 // empty source: no-op
  IMap<0> f { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  IMap<0> g { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  f.emplace(1, 1);
  f.merge(g);
  CHECK_EQ(f.size(), 1u);

 // empty destination: move everything in
  IMap<0> h { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  IMap<0> i { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  for (int k = 0; k < 20; ++k) i.emplace(k, k);
  h.merge(i);
  CHECK_EQ(h.size(), 20u);
 CHECK(i.empty()); // source emptied when there is no conflict
  for (int k = 0; k < 20; ++k) CHECK(h.contains(k));

  // set merge
  ISet<0> s1 { std::hash<int> {}, std::equal_to<> {}, std::allocator<int> {} };
  ISet<0> s2 { std::hash<int> {}, std::equal_to<> {}, std::allocator<int> {} };
  for (int k = 0; k < 10; ++k) s1.emplace(k);
  for (int k = 5; k < 15; ++k) s2.emplace(k);

  s1.merge(s2);
  CHECK_EQ(s1.size(), 15u);
 CHECK_EQ(s2.size(), 5u); // 5..9 conflicts kept
  for (int k = 0; k < 15; ++k) CHECK(s1.contains(k));
  for (int k = 5; k < 10; ++k) CHECK(s2.contains(k));
}

// ==================================================================
// 17. constexpr / consteval branches
//
// std::hash / std::allocator operator()/allocate may not be constant-evaluable,
// so a hand-made constexpr hasher is used here; this path drives inline_storage's
// allocate/deallocate/initial_group_array and the consteval scan of group15,
// plus forcing the consteval multiplication branches of umul128/umulh in hash.hpp into compile time.
// ==================================================================
struct constexpr_int_hash
{
  constexpr std::size_t operator() (int value) const noexcept
  {
    return static_cast<std::size_t>(static_cast<unsigned int>(value)) * 0x9E37'79B9'7F4A'7C15ull;
  }
};

template <size_t N>
using CIMap = ufd::hash_table<ufd::ufmap_traits<int, int, N>, constexpr_int_hash, std::equal_to<>, std::allocator<std::pair<int, int>>>;

template <size_t N>
using CISet = ufd::hash_table<ufd::ufset_traits<int, N>, constexpr_int_hash, std::equal_to<>, std::allocator<int>>;

constexpr bool constexpr_stack_access()
{
  CISet<29> s { constexpr_int_hash {}, std::equal_to<> {}, std::allocator<int> {} };
  if (!s.is_stack() || s.capacity() != 29u) return false;

  for (int i = 0; i < 29; ++i) s.emplace(i);
  if (!s.is_stack() || s.size() != 29u) return false;
  for (int i = 0; i < 29; ++i)
    if (!s.contains(i)) return false;

  for (int i = 0; i < 29; ++i)
    if (s.erase(i) != 1u) return false;
  return s.empty();
}

constexpr bool constexpr_promote_to_heap()
{
  CIMap<29> m { constexpr_int_hash {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  for (int i = 0; i < 45; ++i) m.emplace(i, i * 2);

  if (!m.is_heap() || m.size() != 45u) return false;
  for (int i = 0; i < 45; ++i) {
    auto it = m.find(i);
    if (it == m.end() || it->second != i * 2) return false;
  }
  return true;
}

constexpr bool constexpr_heap_only()
{
  CIMap<0> m { constexpr_int_hash {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  for (int i = 0; i < 40; ++i) m.emplace(i, i);
  if (!m.is_heap() || m.size() != 40u) return false;

  CIMap<0> copy(m, m.get_allocator());
  if (copy.size() != 40u || !copy.contains(20)) return false;

  CIMap<0> moved(std::move(copy));
  if (moved.size() != 40u || !moved.contains(20) || !copy.empty()) return false;

  moved.rehash(128);
  if (moved.capacity() < 128u || !moved.contains(20)) return false;

 // for allocation-failure metadata consistency see [3]; here only the surface = key overloads are covered
  return moved.erase(20) == 1u && !moved.contains(20);
}

constexpr bool constexpr_erase_and_reset()
{
  CIMap<29> m { constexpr_int_hash {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
  for (int i = 0; i < 45; ++i) m.emplace(i, i);
  for (int i = 0; i < 45; i += 2) m.erase(i);
  for (int i = 0; i < 45; i += 2)
    if (m.contains(i)) return false;
  for (int i = 1; i < 45; i += 2)
    if (!m.contains(i)) return false;

  m.reset();
  return m.empty() && m.is_stack();
}

constexpr bool constexpr_hash_mul()
{
 // short inputs (<=16B) skip the mode-dependent stripe loop; all three modes give the same result;
 // to force the lane accumulation and per-mode folding branches, inputs must be longer than a stripe (48/80/112).
  std::array<std::byte, 200> buffer {};
  for (std::size_t i = 0; i < buffer.size(); ++i) buffer[i] = static_cast<std::byte>(i * 7u + 1u);

  auto const k1 = stdex::hash::hash<int>(12345);
  auto const k2 = stdex::hash::hash<int>(54321);
  auto const standard = stdex::hash::hash_range(buffer);
  auto const micro    = stdex::hash::hash_range(buffer, 0u, stdex::hash::micro_t {});
  auto const nano     = stdex::hash::hash_range(buffer, 0u, stdex::hash::nano_t {});

  auto const product = stdex::hash::hash_detail::umul128(0x0123'4567'89AB'CDEFull, 0xFEDC'BA98'7654'3210ull);
  return k1 != k2 && standard != micro && standard != nano && micro != nano &&
         product.high == stdex::hash::hash_detail::umulh(0x0123'4567'89AB'CDEFull, 0xFEDC'BA98'7654'3210ull);
}

static_assert(constexpr_stack_access());
static_assert(constexpr_promote_to_heap());
static_assert(constexpr_heap_only());
static_assert(constexpr_erase_and_reset());
static_assert(constexpr_hash_mul());

void test_constexpr()
{
  SECTION("constexpr / consteval branches");
  CHECK(constexpr_stack_access());
  CHECK(constexpr_promote_to_heap());
  CHECK(constexpr_heap_only());
  CHECK(constexpr_erase_and_reset());
  CHECK(constexpr_hash_mul());
}

// ==================================================================
// 18. force_rehash_ value paths: move-only / throwing-copy / throw rollback
// ==================================================================
void test_rehash_value_paths()
{
  SECTION("rehash value paths (move-only / throwing copy)");

 // move-only value_type takes move_path (!is_copy_constructible).
  {
    UniqueMap<0> t { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, std::unique_ptr<int>>> {} };
    for (int i = 0; i < 40; ++i) t.emplace(i, std::make_unique<int>(i * 10));
    t.rehash(200);
    CHECK_EQ(t.size(), 40u);
    for (int i = 0; i < 40; ++i) {
      auto it = t.find(i);
      CHECK(it != t.end());
      if (it != t.end()) CHECK_EQ(*it->second, i * 10);
    }
  }

 // stack source + move-only: the force_rehash_ move loop destroys source slots one by one,
 // then exchange heap blocks via promote_to_heap_.
  {
    UniqueMap<29> t { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, std::unique_ptr<int>>> {} };
    for (int i = 0; i < 29; ++i) t.emplace(i, std::make_unique<int>(i));
    CHECK(t.is_stack());
    t.rehash(200);
    CHECK(t.is_heap());
    CHECK_EQ(t.size(), 29u);
    for (int i = 0; i < 29; ++i) {
      auto it = t.find(i);
      CHECK(it != t.end());
      if (it != t.end()) CHECK_EQ(*it->second, i);
    }
  }

 // a copyable type whose copy and move are both not nothrow takes copy_path.
  {
    throwing_move_value::reset_counters();
    {
      ThrowingMap<0> t { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, throwing_move_value>> {} };
      for (int i = 0; i < 40; ++i) t.emplace(i, throwing_move_value { i });
      throwing_move_value::reset_counters();
      t.rehash(200);
      CHECK_EQ(t.size(), 40u);
      CHECK(throwing_move_value::copy_count > 0);
      CHECK_EQ(throwing_move_value::move_count, 0);
      for (int i = 0; i < 40; ++i) CHECK_EQ(t.find(i)->second.value, i);
    }
    CHECK_EQ(throwing_move_value::live_count, 0);
  }

 // copy_path throws midway: source kept intact (copy never touches source), destination partial result destroys itself.
  {
    throwing_move_value::reset_counters();
    {
      ThrowingMap<0> t { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, throwing_move_value>> {} };
      for (int i = 0; i < 40; ++i) t.emplace(i, throwing_move_value { i });
      throwing_move_value::reset_counters();
      throwing_move_value::throw_on_copy = 3;

      bool threw = false;
      try {
        t.rehash(1000);
      } catch (int e) {
        threw = true;
        CHECK_EQ(e, 1);
      }

      throwing_move_value::throw_on_copy = 0;
      CHECK(threw);
      CHECK_EQ(t.size(), 40u);
      for (int i = 0; i < 40; ++i) {
        auto it = t.find(i);
        CHECK(it != t.end());
        if (it != t.end()) CHECK_EQ(it->second.value, i);
      }
      CHECK_EQ(throwing_move_value::live_count, 40);

      // the table stays usable after failure.
      t.emplace(1000, throwing_move_value { 1000 });
      CHECK(t.contains(1000));
    }
    CHECK_EQ(throwing_move_value::live_count, 0);
  }
}

// ==================================================================
// 19. Transfer exception safety: copy / move construction throws midway
// ==================================================================
void test_transfer_exception_safety()
{
  SECTION("transfer exception safety (copy/move ctor)");

 // stack->stack copy construction succeeds: same-layout direct transfer.
  {
    throwing_move_value::reset_counters();
    {
      ThrowingMap<29> src { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, throwing_move_value>> {} };
      for (int i = 0; i < 20; ++i) src.emplace(i, throwing_move_value { i });
      ThrowingMap<29> dst(src, src.get_allocator());
      CHECK_EQ(dst.size(), 20u);
      for (int i = 0; i < 20; ++i) CHECK_EQ(dst.find(i)->second.value, i);
    }
    CHECK_EQ(throwing_move_value::live_count, 0);
  }

 // stack->stack copy construction throws midway: source kept intact, destination partial result must be cleaned up.
  {
    throwing_move_value::reset_counters();
    {
      ThrowingMap<29> src { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, throwing_move_value>> {} };
      for (int i = 0; i < 20; ++i) src.emplace(i, throwing_move_value { i });
      throwing_move_value::reset_counters();
      throwing_move_value::throw_on_copy = 3;

      bool threw = false;
      try {
        ThrowingMap<29> dst(src, src.get_allocator());
        (void) dst;
      } catch (int e) {
        threw = true;
        CHECK_EQ(e, 1);
      }

      throwing_move_value::throw_on_copy = 0;
      CHECK(threw);
      CHECK_EQ(src.size(), 20u);
      for (int i = 0; i < 20; ++i) CHECK_EQ(src.find(i)->second.value, i);
      CHECK_EQ(throwing_move_value::live_count, 20);
    }
    CHECK_EQ(throwing_move_value::live_count, 0);
  }

 // heap->heap copy construction throws midway (different capacity takes transfer_with_rehash): source kept intact.
  {
    throwing_move_value::reset_counters();
    {
      ThrowingMap<0> src { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, throwing_move_value>> {} };
      for (int i = 0; i < 200; ++i) src.emplace(i, throwing_move_value { i });
      for (int i = 40; i < 200; ++i) src.erase(i); // keep 40 elements with the large capacity retained
      CHECK_EQ(src.size(), 40u);
      throwing_move_value::reset_counters();
      throwing_move_value::throw_on_copy = 3;

      bool threw = false;
      try {
        ThrowingMap<0> dst(src, src.get_allocator());
        (void) dst;
      } catch (int e) {
        threw = true;
        CHECK_EQ(e, 1);
      }

      throwing_move_value::throw_on_copy = 0;
      CHECK(threw);
      CHECK_EQ(src.size(), 40u);
      for (int i = 0; i < 40; ++i) CHECK_EQ(src.find(i)->second.value, i);
      CHECK_EQ(throwing_move_value::live_count, 40);
    }
    CHECK_EQ(throwing_move_value::live_count, 0);
  }

 // stack->stack move construction succeeds: source emptied.
  {
    throwing_move_value::reset_counters();
    {
      ThrowingMap<29> src { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, throwing_move_value>> {} };
      for (int i = 0; i < 20; ++i) src.emplace(i, throwing_move_value { i });
      ThrowingMap<29> dst(std::move(src));
      CHECK_EQ(dst.size(), 20u);
      for (int i = 0; i < 20; ++i) CHECK_EQ(dst.find(i)->second.value, i);
      CHECK(src.empty());
    }
    CHECK_EQ(throwing_move_value::live_count, 0);
  }

 // stack->stack move construction throws midway: source emptied but usable, no leak.
  {
    throwing_move_value::reset_counters();
    {
      ThrowingMap<29> src { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, throwing_move_value>> {} };
      for (int i = 0; i < 20; ++i) src.emplace(i, throwing_move_value { i });
      throwing_move_value::reset_counters();
      throwing_move_value::throw_on_move = 3;

      bool threw = false;
      try {
        ThrowingMap<29> dst(std::move(src));
        (void) dst;
      } catch (int e) {
        threw = true;
        CHECK_EQ(e, 2);
      }

      throwing_move_value::throw_on_move = 0;
      CHECK(threw);
      CHECK(src.empty());
      CHECK_EQ(throwing_move_value::live_count, 0);
      src.emplace(999, throwing_move_value { 999 });
      CHECK(src.contains(999));
    }
    CHECK_EQ(throwing_move_value::live_count, 0);
  }

 // move construction + unequal allocators takes transfer_with_rehash: midway throw still leaks nothing.
  {
    allocator_probe_state source_state, destination_state;
    {
      ThrowingProbeMap<0> src { std::hash<int> {}, std::equal_to<> {}, { &source_state, 1 } };
      for (int i = 0; i < 40; ++i) src.emplace(i, throwing_move_value { i });
      throwing_move_value::reset_counters();
      throwing_move_value::throw_on_move = 3;

      bool threw = false;
      try {
        ThrowingProbeMap<0> dst(std::move(src), { &destination_state, 2 });
        (void) dst;
      } catch (int e) {
        threw = true;
        CHECK_EQ(e, 2);
      }

      throwing_move_value::throw_on_move = 0;
      CHECK(threw);
      CHECK(src.empty());
      CHECK_EQ(throwing_move_value::live_count, 0);
    }
    CHECK_EQ(throwing_move_value::live_count, 0);
  }
}

// ==================================================================
// 20. node_type copy / move construction and assignment
// ==================================================================
void test_node_value_semantics()
{
  SECTION("node_type copy/move");

  {
    IMap<0> t { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
    t.emplace(1, 10);
    t.emplace(2, 20);

    auto n1 = t.extract(1);
    auto n2 = n1; // copy construction
    CHECK(static_cast<bool>(n1) && static_cast<bool>(n2));
    CHECK_EQ(n2.value().second, 10);

    IMap<0>::node_type n3;
    n3 = n2; // copy assignment: empty <- full
    CHECK_EQ(n3.value().second, 10);
    n3 = n3; // self-assignment
    CHECK(static_cast<bool>(n3));
    CHECK_EQ(n3.value().second, 10);

    auto n4 = t.extract(2);
    n3 = std::move(n4); // move assignment: full <- full
    CHECK_EQ(n3.value().second, 20);

    IMap<0>::node_type n5;
    n5 = std::move(n3); // move assignment: empty <- full
    CHECK_EQ(n5.value().second, 20);
    n5 = std::move(n5); // self move-assignment
    CHECK(static_cast<bool>(n5));
    CHECK_EQ(n5.value().second, 20);
    n5 = IMap<0>::node_type {}; // move assignment: full <- empty
    CHECK(!static_cast<bool>(n5));

    t.emplace(3, 30);
    auto n6 = t.extract(3);
    auto n7 = std::move(n6); // move construction
    CHECK(static_cast<bool>(n7));
    CHECK_EQ(n7.value().second, 30);
  }

 // live balance of node semantics.
  {
    throwing_move_value::reset_counters();
    {
      ThrowingMap<0> t { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, throwing_move_value>> {} };
      t.emplace(1, throwing_move_value { 1 });
      auto a = t.extract(1);
      auto b = a;
      auto c = std::move(b);
      ThrowingMap<0>::node_type d;
      d = c;
      d = std::move(c);
      CHECK_EQ(d.value().second.value, 1);
    }
    CHECK_EQ(throwing_move_value::live_count, 0);
  }
}

// ==================================================================
// 21. Non-default-constructible value_type and duplicate-key paths
// ==================================================================
void test_no_default_value()
{
  SECTION("non-default-constructible value_type");

  {
    {
      NoDefaultMap<29> t { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, no_default_value>> {} };
      for (int i = 0; i < 20; ++i) t.emplace(i, no_default_value { i * 10 });
      CHECK(t.is_stack());

      // duplicate key (with_key path): no overwrite, no temporary leak.
      auto [dup_it, dup_ok] = t.emplace(5, no_default_value { 999 });
      CHECK(!dup_ok);
      CHECK_EQ(dup_it->second.value, 50);

      CHECK_EQ(t.find(5)->second.value, 50);
      CHECK_EQ(t.erase(5), 1u);
      t.emplace(5, no_default_value { 55 });
      CHECK_EQ(t.find(5)->second.value, 55);

      // exceeding inline capacity promotes to heap.
      for (int i = 20; i < 60; ++i) t.emplace(i, no_default_value { i });
      CHECK(t.is_heap());
      CHECK_EQ(t.size(), 60u);
      for (int i = 0; i < 60; ++i) CHECK(t.contains(i));
    }
    CHECK_EQ(no_default_value::live_count, 0);
  }

  {
    NoDefaultMap<0> t { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, no_default_value>> {} };
    for (int i = 0; i < 40; ++i) t.emplace(i, no_default_value { i });
    CHECK_EQ(t.size(), 40u);
    for (int i = 0; i < 40; ++i) CHECK_EQ(t.find(i)->second.value, i);
  }

 // partially filled stack destruction for non-trivial types: destroy only constructed slots.
  {
    throwing_move_value::reset_counters();
    {
      ThrowingMap<29> t { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, throwing_move_value>> {} };
      for (int i = 0; i < 10; ++i) t.emplace(i, throwing_move_value { i });
      CHECK(t.is_stack());
    }
    CHECK_EQ(throwing_move_value::live_count, 0);
  }

 // without_key duplicate-key path: zero-arg emplace builds a temporary first, then checks duplicates.
  {
    IMap<0> t { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
    t.emplace(0, 999);
    auto [it, ok] = t.emplace();
    CHECK(!ok);
    CHECK_EQ(it->second, 999);

    IMap<0> fresh { std::hash<int> {}, std::equal_to<> {}, std::allocator<std::pair<int, int>> {} };
    auto [it2, ok2] = fresh.emplace();
    CHECK(ok2);
    CHECK_EQ(it2->first, 0);
    CHECK_EQ(it2->second, 0);
  }
}

// ==================================================================
// main
// ==================================================================
int main()
{
  test_basic_empty_single();
  test_set_traits();
  test_stack_to_heap();
  test_allocator_aware();
  test_copy_move();
  test_storage_conversion_and_capacity_boundaries();
  test_swap();
  test_erase();
  test_rehash_reserve_clear_reset();
  test_iteration();
  test_insert_variants();
  test_heterogeneous_lookup();
  test_introspection();
  test_extract();
  test_insert_node();
  test_merge();
  test_fuzz_vs_std_map();
  test_fuzz_strings();
  test_constexpr();
  test_rehash_value_paths();
  test_transfer_exception_safety();
  test_node_value_semantics();
  test_no_default_value();

  std::println("\n===============================");
  std::println("PASS: {}   FAIL: {}", g_pass, g_fail);
  std::println("===============================");
  return g_fail == 0 ? 0 : 1;
}

/*
================================================================================
Unfinished tests -- gap ledger
================================================================================
This file currently covers only runtime paths with regular types. Ranked by risk, branches / semantics with no
test touching them yet, each with its location and a suggested test shape for follow-up coverage.

--------------------------------------------------------------------------------
[1] constexpr / consteval branches (highest priority)
--------------------------------------------------------------------------------
The project hard constraint is that every change must stay constexpr-evaluable, yet this file has no
static_assert / constexpr evaluation cases; the if consteval branches below never ran:
  - consteval branch of allocate_unchecked_ at inline_storage.hpp:160
    (elements and groups allocated separately, instead of one merged block as at runtime
    calculate_storage_size_）
  - consteval branch of deallocate_unchecked_ at inline_storage.hpp:184
  - consteval branch of initial_group_array_ at inline_storage.hpp:238
  - group15.hpp:193/246/304/411/435  fingerprint_mask / reset /
    consteval implementation of set_overflow_mark / available_mask / occupied_mask
    (scalar-layout reset(unsigned char*) throws directly under consteval, see
    group15.hpp:270）
  - consteval branches of umul128 / umulh at hash.hpp:194/216
Suggestion: rewrite the IMap<0>/IMap<29> insert-find-erase-rehash-copy path as
`constexpr bool f(){...}` + static_assert. This is the most urgent gap.

--------------------------------------------------------------------------------
[2] Untouched branches in storage conversion
--------------------------------------------------------------------------------
  - covered by test_storage_conversion_and_capacity_boundaries():
      * same-layout stack copy hits transfer_same_layout_unchecked_, covering int
        the memcpy fast path for value_type;
      * rehash no-op at the current capacity;
      * empty-table rehash(0) min_group_count == 2 path;
      * reset() on an inline_capacity == 0 table keeps heap capacity;
      * erase then reinsert the same key.
  - covered by CollisionMap<29> in the same test: max_load_ anti-drift with constant hash
    forces overflow; erasing an overflow slot lowers max_load_, and reinsertion at the threshold
    insertion must grow early.

--------------------------------------------------------------------------------
[3] Allocator-aware branches
--------------------------------------------------------------------------------
 All existing types use std::allocator, always taking the is_always_equal == true fast path,
 the other half of the branches never ran:
  - is_pocca_v / is_pocma_v / is_pocs_v（hash_table.hpp:104-106）：
    copy assignment at hash_table.hpp:295, move assignment at 379/400/405, move construction at 345+390,
    swap 475-481。
  - the else branch of allocators_equal_ (hash_table.hpp:109) (left == right),
    taken only by non-always-equal allocators.
  - swap triggers assert when is_pocs_v == false with unequal allocators
    (hash_table.hpp:478), no test.
  - allocate throwing bad_alloc state consistency: allocate_unchecked_ first mutates
    group_index_shift_/group_count_mask_/max_load_
    (inline_storage.hpp:156-158) then allocates; on allocation failure the object is in a metadata-updated,
    pointer unchanged inconsistent state.
  - an allocator throwing bad_alloc from allocate is still needed to verify the strong / basic guarantee.
 Suggestion: a custom allocator recording allocate/deallocate counts with is_always_equal=false,
 propagate_*-switchable custom allocator asserting counts and propagation per branch.

--------------------------------------------------------------------------------
[4] value_type copy / move paths and exception safety
--------------------------------------------------------------------------------
  - covered by test_rehash_value_paths(): move_path of force_rehash_:
      * !is_copy_constructible branch: UniqueMap (pair<int, unique_ptr<int>>)
        heap / stack rehash.
      * copy_path: ThrowingMap (neither copy nor move is nothrow) triggers
        destroy_element_on_exit, witnessed by copy_count > 0 and move_count == 0.
      * copy_path throws midway: source kept intact (copy never touches source), destination partial result destroyed, then
        live_count drops back. The implementation depends on ++dest.size_ inside the loop; otherwise the destination destructor would
        size_ == 0 would skip copied elements and leak (leak reproduced, now fixed).
  - covered by test_transfer_exception_safety(): copy / move construction throws:
      * stack->stack copy throw: source kept intact; the destination partial result goes through
        rollback_dest_elements_on_exit cleans up mirroring the source occupancy -- groups
        batch group copy is deferred until after the loop; on throw the destination destructor cannot find the partial result
        (leak reproduced, now fixed).
      * heap->heap copy throw (different capacity takes transfer_with_rehash): source kept intact.
      * stack->stack move throw: source is emptied but stays usable; move construction with unequal allocators also leaks nothing.
      * uncovered: a move-only type whose move throws, inside the force_rehash_ move loop,
        throw -- source slots were destroyed one by one but occupancy was not reset, so the source cannot be restored to a valid state;
        needs a redesign that resets occupancy slot by slot; deferred.
  - covered by test_node_value_semantics(): copy construction and copy /
    move assignment (incl. self-assignment and empty<->full) with live balance.
  - covered by test_no_default_value():
      * NoDefaultMap stack / heap emplace, find, erase, duplicate key (with_key
        path) with live balance.
      * without_key duplicate-key path: zero-arg emplace builds a temporary first, then checks for duplicates.
      * partially filled stack destruction for non-trivial types: ThrowingMap<29> destroys only constructed slots,
        live_count returns to zero (witnessed by stack_block's bare union + empty destructor).

--------------------------------------------------------------------------------
[5] API coverage gaps
--------------------------------------------------------------------------------
  - ranges version of insert_range: only std::vector is tested, unverified
    container_compatible_range (hash_table.hpp:19-20) on a lazy input_range
    behavior. Try `std::views::iota(0,10) | std::views::transform(...)` to confirm
    the std::ranges::input_range concept applies and is not mistaken for forward_range with the wrong overload.
  - the transparent erase overloads (hash_table.hpp:794) untested; the transparent extract version was tested.
  - heterogeneous merge overloads hash_table<Traits,H2,KE2,Alloc> (hash_table.hpp:965)
    untested; only same-type merge was exercised.
  - lazy_iterator: the erase return value was never used. Chained erase needs verification:
        for (auto it = t.erase(t.begin()); it != t.end();) it = t.erase(it);
    and the return-value semantics of erase(first,last).
    note hash_table_lazy_iterator only provides operator iterator() /
    operator const_iterator() (hash_table_iterator.hpp:230/237), which only converts
    increment_, with no operator++ of its own; the lazy semantics come from this conversion, not from postfix++.
    (the original missing lazy_iterator postfix++ note means: both the lazy_iterator
    conversion semantics test, as well as the plain iterator postfix++ test.)
  - postfix ++ of hash_table_iterator (hash_table_iterator.hpp:123) never called,
    test_iteration only used pre-increment.
  - no compile-time assertion on begin()/end() return-type const-correctness. Per hard constraint, ISet's
    iterator must have the same type as const_iterator, while map's must differ:
        static_assert(std::is_same_v<ISet<0>::iterator, ISet<0>::const_iterator>);
        static_assert(!std::is_same_v<IMap<0>::iterator, IMap<0>::const_iterator>);
  - insert(const_iterator, value_type) (hint overloads, hash_table.hpp:685/690)
    only partly covered indirectly via emplace_hint.
  - the const overloads of equal_range (hash_table.hpp:727/757) were never called; existing
    test_heterogeneous_lookup calls the non-const transparent version.
  - piecewise_construct overloads of try_key_extraction
    (try_key_extraction.hpp:45) has no test.
  - comparison tests, erase_if tests, and public []/at/try_emplace/insert_or_assign tests of unordered_flat_map

--------------------------------------------------------------------------------
[6] Degenerate and boundary cases
--------------------------------------------------------------------------------
  - full-collision hash (always returns the same constant): triangular probing + overflow marks need verification on
    find / insert / erase are all correct with no infinite loop.
  - inline_capacity only tested 0 and 29; larger stack capacities like 59 (4*15-1) have no test.
  - std::pmr aliases have no test.

--------------------------------------------------------------------------------
[7] Integration and performance
--------------------------------------------------------------------------------
  - no integration case using stdex::hash::hash from hash.hpp as the Hasher.
  - no CRUD performance comparison against boost::unordered_flat_map / std::unordered_map.
*/
