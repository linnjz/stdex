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

// test_inline_storage.cpp
// Covers stdex::inline_storage<T, N, Allocator> from inline_storage.hpp:
// ctors, observers, growth, reserve/shrink/reset/clear, insert/range helpers,
// guards, copy/move helpers, swap, storage_equal_/storage_compare_,
// exception safety, fancy pointers, heap/stack combinations, boundary cases,
// model fuzz, constexpr (heap only;
// stack constexpr needs C++26 start_lifetime, unavailable locally).
//
// The mutating surface is protected by design, so tests drive it through a
// derived fixture that re-exports each helper. Step helpers that cannot stand
// alone without private union access (promote/demote/set_size_/deallocate_)
// are covered through the public paths that sequence them; each skipped direct
// test says so where it would belong.
#include <algorithm>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdio>
#include <forward_list>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <print>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "inline_storage.hpp"

static int g_pass = 0;
static int g_fail = 0;

// Variadic so template-id commas (fix<T, N, Alloc>) inside conditions do not
// split macro arguments; #__VA_ARGS__ still prints the whole condition.
#define CHECK(...)                                                       \
  do {                                                                   \
    if (__VA_ARGS__) {                                                   \
      ++g_pass;                                                          \
    } else {                                                             \
      ++g_fail;                                                          \
      std::println(stderr, "  [FAIL] {}:{}  {}", __FILE__, __LINE__, #__VA_ARGS__); \
    }                                                                    \
  } while (0)

#define SECTION(name) std::println("\n=== {} ===", name)

// ------------------------------------------------------------------
// fixture: re-export every testable helper
// ------------------------------------------------------------------
template <class T, size_t N, class A = std::allocator<T>>
struct fix : stdex::inline_storage<T, N, A> {
  using base = stdex::inline_storage<T, N, A>;
  using base::base;
  using typename base::allocator_type;
  using typename base::size_type;

  using base::get_allocator;
  using base::empty;
  using base::size;
  using base::max_size;
  using base::capacity;
  using base::is_heap;
  using base::is_stack;
  using base::data;
  using base::begin;
  using base::end;
  using base::rbegin;
  using base::rend;
  using base::cbegin;
  using base::cend;
  using base::crbegin;
  using base::crend;
  using base::shrink_to_fit;
  using base::reset;
  using base::clear;
  using base::reserve;
  using base::resize;
  using base::resize_reallocate_;
  using base::resize_impl_;
  using base::swap;

  // Nested types need public aliases: MSVC rejects using-declarations that
  // re-export protected nested types (C2248); same-name aliases hide them.
  using move_outcome = typename base::move_outcome;
  using allocation_guard = typename base::allocation_guard;
  using destruction_guard = typename base::destruction_guard;
  using reallocation_guard = typename base::reallocation_guard;
  using base::allocators_equal_;
  using base::max_size_;
  using base::throw_length_error_;
  using base::allocate_unchecked_;
  using base::construct_element_unchecked_;
  using base::construct_elements_unchecked_;
  using base::construct_heap_n_unchecked_;
  using base::try_construct_stack_n_unchecked_;
  using base::promote_to_heap_;
  using base::emplace_reallocate_;
  using base::emplace_back_with_unused_capacity_;
  using base::emplace_one_at_back_;
  using base::append_uncounted_range_;
  using base::insert_uncounted_range_;
  using base::insert_counted_range_;
  using base::insert_range_;
  using base::source_overlaps_storage_;
  using base::destroy_element_unchecked_;
  using base::destroy_elements_unchecked_;
  using base::dispose_;
  using base::steal_heap_from_;
  using base::copy_construct_from_;
  using base::move_construct_from_;
  using base::assign_grow_from_;
  using base::assign_from_range_unchecked_;
  using base::copy_assignment_from_;
  using base::move_assignment_from_;
  using base::swap_stack_elements_;
  using base::swap_heap_to_stack_;
  using base::storage_equal_;
  using base::storage_compare_;
  using base::transfer_unchecked_;
  using base::calculate_growth_;
  using base::get_flag_;

  // NOTE: demote_to_heap_/set_size_/deallocate_unchecked_/heap_restore_on_exit
  // are sequencing steps, not standalone ops: used alone they leave the
  // heap/stack union or the size flag inconsistent (private state cannot be
  // repaired from here). They are covered through reserve/shrink/reset paths.

  constexpr void push(int v) requires std::constructible_from<T, int>
  {
    emplace_one_at_back_(v);
  }

  // live allocator access for guards/manual deallocation (get_allocator
  // returns a copy, which cannot bind the guard's allocator_type&)
  constexpr allocator_type& test_alloc() noexcept { return this->alloc_; }

  [[nodiscard]] constexpr bool invariants() const noexcept
  {
    if (size() > capacity()) return false;
    if (is_stack() && (size() > N || capacity() != N)) return false;
    if (is_heap() && capacity() < size()) return false;
    return true;
  }
};

// ------------------------------------------------------------------
// tracking types
// ------------------------------------------------------------------
struct track {
  int value = 0;
  static inline int live = 0;
  static inline int moves = 0;
  static inline int copies = 0;
  static void reset() { live = 0; moves = 0; copies = 0; }

  track() { ++live; }
  explicit track(int v) : value(v) { ++live; }
  track(track&& o) noexcept : value(o.value) { ++live; ++moves; o.value = -1; }
  track(track const& o) : value(o.value) { ++live; ++copies; }
  track& operator=(track&& o) noexcept { value = o.value; o.value = -1; return *this; }
  track& operator=(track const& o) { value = o.value; return *this; }
  ~track() { --live; }
  bool operator==(track const&) const = default;
  auto operator<=>(track const&) const = default;
  static void reset_counts() { moves = 0; copies = 0; }
};

struct copy_only {
  int value = 0;
  static inline int live = 0;
  static inline int copies = 0;
  static void reset() { live = 0; copies = 0; }

  copy_only() { ++live; }
  explicit copy_only(int v) : value(v) { ++live; }
  copy_only(copy_only const& o) : value(o.value) { ++live; ++copies; }
  copy_only(copy_only&&) = delete;
  ~copy_only() { --live; }
  bool operator==(copy_only const&) const = default;
};

struct move_only {
  int value = 0;
  static inline int live = 0;
  static inline int moves = 0;
  static void reset() { live = 0; moves = 0; }

  move_only() { ++live; }
  explicit move_only(int v) : value(v) { ++live; }
  move_only(move_only&& o) noexcept : value(o.value) { ++live; ++moves; o.value = -1; }
  move_only(move_only const&) = delete;
  move_only& operator=(move_only&& o) noexcept { value = o.value; o.value = -1; return *this; }
  move_only& operator=(move_only const&) = delete;
  ~move_only() { --live; }
  static void reset_counts() { moves = 0; }
};

struct throw_copy {
  int value = 0;
  static inline int alive = 0;
  static inline int count = 0;
  static inline int throw_after = 1000000;
  static void reset() { alive = 0; count = 0; throw_after = 1000000; }

  throw_copy() { ++alive; }
  explicit throw_copy(int v) : value(v) { ++alive; }
  throw_copy(throw_copy const& o) : value(o.value)
  {
    if (count++ >= throw_after) throw std::runtime_error("copy throws");
    ++alive;
  }
  throw_copy(throw_copy&& o) noexcept : value(o.value) { ++alive; o.value = -1; }
  throw_copy& operator=(throw_copy const& o) { value = o.value; return *this; }
  throw_copy& operator=(throw_copy&& o) noexcept { value = o.value; o.value = -1; return *this; }
  ~throw_copy() { --alive; }
};

struct throw_move {
  int value = 0;
  static inline int alive = 0;
  static inline int count = 0;
  static inline int throw_after = 1000000;
  static void reset() { alive = 0; count = 0; throw_after = 1000000; }

  throw_move() { ++alive; }
  explicit throw_move(int v) : value(v) { ++alive; }
  throw_move(throw_move const& o) : value(o.value) { ++alive; }
  throw_move(throw_move&& o) : value(o.value)
  {
    if (count++ >= throw_after) throw std::runtime_error("move throws");
    ++alive;
  }
  throw_move& operator=(throw_move&& o) { value = o.value; return *this; }
  throw_move& operator=(throw_move const& o) { value = o.value; return *this; }
  ~throw_move() { --alive; }
};

struct throw_moveonly {
  int value = 0;
  static inline int alive = 0;
  static inline int count = 0;
  static inline int throw_after = 1000000;
  static void reset() { alive = 0; count = 0; throw_after = 1000000; }
  throw_moveonly(int v) : value(v) { ++alive; }
  throw_moveonly(throw_moveonly&& o) : value(o.value)
  {
    if (count++ >= throw_after) throw std::runtime_error("move throws");
    ++alive;
  }
  throw_moveonly(throw_moveonly const&) = delete;
  ~throw_moveonly() { --alive; }
};

// Move-only with a throwing move ctor but noexcept move assignment: forces the
// transfer itself (not the source copy) to throw midway, for insert paths
// that relocate existing elements before filling the gap.
struct throw_moveonly_asn {
  int value = 0;
  static inline int alive = 0;
  static inline int count = 0;
  static inline int throw_after = 1000000;
  static void reset() { alive = 0; count = 0; throw_after = 1000000; }
  explicit throw_moveonly_asn(int v) : value(v) { ++alive; }
  throw_moveonly_asn(throw_moveonly_asn&& o) : value(o.value)
  {
    if (count++ >= throw_after) throw std::runtime_error("move throws");
    ++alive;
    o.value = -1;
  }
  throw_moveonly_asn& operator=(throw_moveonly_asn&& o) noexcept
  {
    value = o.value;
    o.value = -1;
    return *this;
  }
  throw_moveonly_asn(throw_moveonly_asn const&) = delete;
  throw_moveonly_asn& operator=(throw_moveonly_asn const&) = delete;
  ~throw_moveonly_asn() { --alive; }
};

// two-argument-constructible element for multi-arg emplace coverage
struct pair_like {
  int a = 0;
  int b = 0;
  pair_like(int x, int y) : a(x), b(y) {}
  bool operator==(pair_like const&) const = default;
};

// weak ordering category for storage_compare_ category coverage
struct weak_t {
  int v = 0;
  weak_t(int x) : v(x) {}
  bool operator==(weak_t const&) const = default;
  std::weak_ordering operator<=>(weak_t const& o) const { return v <=> o.v; }
};

// no comparison at all: instantiation alone must compile (hard-error guard)
struct bare_t {
  int v = 0;
  bare_t(int x) : v(x) {}
};

// ------------------------------------------------------------------
// stateful / capped / leak-checking allocators
// ------------------------------------------------------------------
template <class T, bool Pocca, bool Pocma, bool Pocs>
struct tagged_alloc {
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using propagate_on_container_copy_assignment = std::bool_constant<Pocca>;
  using propagate_on_container_move_assignment = std::bool_constant<Pocma>;
  using propagate_on_container_swap = std::bool_constant<Pocs>;
  using is_always_equal = std::false_type;

  template <class U>
  struct rebind {
    using other = tagged_alloc<U, Pocca, Pocma, Pocs>;
  };

  int id_ = 0;
  constexpr tagged_alloc(int id = 0) noexcept : id_(id) {}
  template <class U, bool A, bool B, bool C>
  constexpr tagged_alloc(tagged_alloc<U, A, B, C> const& o) noexcept : id_(o.id_) {}

  T* allocate(std::size_t n) { return static_cast<T*>(::operator new(n * sizeof(T))); }
  void deallocate(T* p, std::size_t) noexcept { ::operator delete(p); }
  bool operator==(tagged_alloc const& o) const noexcept { return id_ == o.id_; }
};

template <class T, std::size_t Cap>
struct capped_alloc : std::allocator<T> {
  template <class U>
  struct rebind {
    using other = capped_alloc<U, Cap>;
  };
  std::size_t max_size() const noexcept { return Cap; }
};

template <class T>
struct leak_alloc {
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  static inline long allocs = 0;
  static inline long deallocs = 0;
  static void reset() { allocs = 0; deallocs = 0; }

  leak_alloc() noexcept = default;
  template <class U>
  leak_alloc(leak_alloc<U> const&) noexcept {}
  T* allocate(std::size_t n)
  {
    ++allocs;
    return static_cast<T*>(::operator new(n * sizeof(T)));
  }
  void deallocate(T* p, std::size_t) noexcept
  {
    ++deallocs;
    ::operator delete(p);
  }
  bool operator==(leak_alloc const&) const noexcept { return true; }
};

// ------------------------------------------------------------------
// fancy pointer harness (mirrors test_uninitialized_algorithms.cpp)
// ------------------------------------------------------------------
template <class T>
struct fancy_ptr {
  T* p_ = nullptr;

  using value_type = T;
  using element_type = T;
  using difference_type = std::ptrdiff_t;
  using iterator_concept = std::contiguous_iterator_tag;
  using iterator_category = std::random_access_iterator_tag;

  constexpr fancy_ptr() noexcept = default;
  constexpr explicit fancy_ptr(T* p) noexcept : p_(p) {}

  template <class U>
    requires std::convertible_to<U*, T*>
  constexpr fancy_ptr(fancy_ptr<U> const& o) noexcept : p_(o.p_)
  {
  }

  constexpr T& operator*() const noexcept { return *p_; }
  constexpr T* operator->() const noexcept { return p_; }
  constexpr T& operator[](difference_type n) const noexcept { return p_[n]; }

  constexpr fancy_ptr& operator++() noexcept { ++p_; return *this; }
  constexpr fancy_ptr operator++(int) noexcept { auto t = *this; ++p_; return t; }
  constexpr fancy_ptr& operator--() noexcept { --p_; return *this; }
  constexpr fancy_ptr operator--(int) noexcept { auto t = *this; --p_; return t; }
  constexpr fancy_ptr& operator+=(difference_type n) noexcept { p_ += n; return *this; }
  constexpr fancy_ptr& operator-=(difference_type n) noexcept { p_ -= n; return *this; }
  constexpr fancy_ptr operator+(difference_type n) const noexcept { auto t = *this; t += n; return t; }
  constexpr fancy_ptr operator-(difference_type n) const noexcept { auto t = *this; t -= n; return t; }

  template <class U>
  constexpr difference_type operator-(fancy_ptr<U> const& o) const noexcept
  {
    return p_ - o.p_;
  }
  template <class U>
  constexpr bool operator==(fancy_ptr<U> const& o) const noexcept
  {
    return p_ == o.p_;
  }
  template <class U>
  constexpr auto operator<=>(fancy_ptr<U> const& o) const noexcept
  {
    return p_ <=> o.p_;
  }

  template <class U>
  friend struct fancy_ptr;
};

template <class T>
constexpr fancy_ptr<T> operator+(typename fancy_ptr<T>::difference_type n, fancy_ptr<T> p) noexcept
{
  return p + n;
}

namespace std {
template <class T>
struct pointer_traits<fancy_ptr<T>> {
  using pointer = fancy_ptr<T>;
  using element_type = T;
  using difference_type = ptrdiff_t;
  template <class U>
  using rebind = fancy_ptr<U>;
  static constexpr pointer pointer_to(element_type& r) noexcept
  {
    return pointer(std::addressof(r));
  }
  static constexpr T* to_address(pointer p) noexcept { return p.p_; }
};
} // namespace std

static_assert(std::contiguous_iterator<fancy_ptr<int>>);
static_assert(std::sized_sentinel_for<fancy_ptr<int>, fancy_ptr<int>>);

template <class T>
struct fancy_alloc {
  using value_type = T;
  using pointer = fancy_ptr<T>;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;

  constexpr fancy_alloc() noexcept = default;
  template <class U>
  constexpr fancy_alloc(fancy_alloc<U> const&) noexcept
  {
  }

  pointer allocate(size_type n) { return pointer(static_cast<T*>(::operator new(n * sizeof(T)))); }
  void deallocate(pointer p, size_type) noexcept { ::operator delete(std::to_address(p)); }
};

template <class T>
struct fancy_counting_alloc {
  using value_type = T;
  using pointer = fancy_ptr<T>;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;

  static inline int construct_count = 0;
  static inline int destroy_count = 0;
  static void reset() { construct_count = 0; destroy_count = 0; }

  constexpr fancy_counting_alloc() noexcept = default;
  template <class U>
  constexpr fancy_counting_alloc(fancy_counting_alloc<U> const&) noexcept
  {
  }

  pointer allocate(size_type n) { return pointer(static_cast<T*>(::operator new(n * sizeof(T)))); }
  void deallocate(pointer p, size_type) noexcept { ::operator delete(std::to_address(p)); }

  template <class... Args>
  void construct(T* p, Args&&... args)
  {
    ++construct_count;
    std::construct_at(p, std::forward<Args>(args)...);
  }
  void destroy(T* p)
  {
    ++destroy_count;
    std::destroy_at(p);
  }
};

// input-only (single pass) iterator for the uncounted range path
struct input_only_it {
  using iterator_category = std::input_iterator_tag;
  using value_type = int;
  using difference_type = std::ptrdiff_t;
  int const* p_ = nullptr;
  int operator*() const { return *p_; }
  input_only_it& operator++() { ++p_; return *this; }
  void operator++(int) { ++p_; }
  bool operator==(input_only_it const& o) const { return p_ == o.p_; }
};
static_assert(!std::forward_iterator<input_only_it>);
static_assert(std::input_iterator<input_only_it>);

// Shared-state single-pass input (istream-like): copies share the position,
// so re-reading from a copy observes the advanced position, never replays.
struct shared_input_it {
  using iterator_category = std::input_iterator_tag;
  using value_type = int;
  using difference_type = std::ptrdiff_t;
  struct shared_state {
    int const* base;
    std::size_t index;
  };
  std::shared_ptr<shared_state> state_;
  int operator*() const { return state_->base[state_->index]; }
  shared_input_it& operator++() { ++state_->index; return *this; }
  void operator++(int) { ++state_->index; }
  bool operator==(shared_input_it const& o) const { return state_ == o.state_ && state_->index == o.state_->index; }
};
static_assert(!std::forward_iterator<shared_input_it>);
static_assert(std::input_iterator<shared_input_it>);

// deterministic PRNG for fuzz
struct xorshift64 {
  unsigned long long s_;
  explicit xorshift64(unsigned long long seed) : s_(seed ? seed : 0x9E3779B97F4A7C15ULL) {}
  unsigned long long next() noexcept
  {
    unsigned long long x = s_;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return s_ = x;
  }
};

// ------------------------------------------------------------------
// 1. empty / allocator-only construction
// ------------------------------------------------------------------
static void test_empty_state()
{
  SECTION("empty: allocator-only construction");
  {
    fix<int, 4> f(std::allocator<int>{});
    CHECK(f.size() == 0 && f.empty());
    CHECK(f.capacity() == 4 && f.is_stack() && !f.is_heap());
    CHECK(f.begin() == f.end() && f.cbegin() == f.cend());
    CHECK(f.rbegin() == f.rend() && f.crbegin() == f.crend());
    CHECK(f.invariants());
    CHECK(f.max_size() > 1000000u);
    CHECK(f.inline_capacity == 4);
    CHECK(f.heap_flag == 1u && f.stack_flag == 0u);
  }
  {
    fix<int, 1> f(std::allocator<int>{});
    CHECK(f.capacity() == 1 && f.is_stack());
  }
  {
    fix<int, 0> f(std::allocator<int>{});
    CHECK(f.size() == 0 && f.empty());
    CHECK(f.capacity() == 0 && f.is_heap() && !f.is_stack());
    CHECK(f.data() == nullptr && f.begin() == f.end());
    CHECK(f.invariants());
  }
  {
    // get_allocator round-trips state
    tagged_alloc<int, false, false, false> al(7);
    fix<int, 4, tagged_alloc<int, false, false, false>> f(al);
    CHECK(f.get_allocator() == al && f.get_allocator().id_ == 7);
  }
}

// ------------------------------------------------------------------
// 2. count ctors
// ------------------------------------------------------------------
static void test_count_ctors()
{
  SECTION("ctors: (n) and (n, value)");
  {
    fix<int, 4> f(0);
    CHECK(f.empty() && f.is_stack());
  }
  {
    fix<int, 4> f(4); // exactly inline: stack, value-init
    CHECK(f.size() == 4 && f.is_stack());
    for (std::size_t i = 0; i < 4; ++i) CHECK(f.data()[i] == 0);
  }
  {
    fix<int, 4> f(5); // spills: heap
    CHECK(f.size() == 5 && f.is_heap() && f.capacity() >= 5);
    for (std::size_t i = 0; i < 5; ++i) CHECK(f.data()[i] == 0);
    CHECK(f.invariants());
  }
  {
    fix<int, 4> f(3, 42);
    CHECK(f.size() == 3 && f.is_stack());
    for (std::size_t i = 0; i < 3; ++i) CHECK(f.data()[i] == 42);
  }
  {
    fix<int, 4> f(6, 7);
    CHECK(f.size() == 6 && f.is_heap());
    for (std::size_t i = 0; i < 6; ++i) CHECK(f.data()[i] == 7);
  }
  {
    fix<int, 0> f(3, 9);
    CHECK(f.size() == 3 && f.is_heap());
    CHECK(f.data()[2] == 9);
  }
  {
    track::reset();
    {
      fix<track, 2> f(5, track(99));
      CHECK(f.size() == 5 && f.is_heap());
      for (std::size_t i = 0; i < 5; ++i) CHECK(f.data()[i].value == 99);
    }
    CHECK(track::live == 0); // dtor disposes heap elements + block
  }
  {
    // capped allocator: exact max_size + length_error before allocation
    fix<int, 4, capped_alloc<int, 10>> f((capped_alloc<int, 10>{}));
    CHECK(f.max_size() == 10);
    bool threw = false;
    try {
      fix<int, 4, capped_alloc<int, 10>> g(11);
    } catch (std::length_error const&) { threw = true; }
    CHECK(threw);
    try {
      fix<int, 4, capped_alloc<int, 10>> g(11, 1);
    } catch (std::length_error const&) { threw = threw && true; }
    CHECK(threw);
  }
}

// ------------------------------------------------------------------
// 3. forward range ctor
// ------------------------------------------------------------------
static void test_range_ctor_forward()
{
  SECTION("ctors: forward range");
  {
    std::vector<int> v;
    fix<int, 4> f(v.begin(), v.end());
    CHECK(f.empty() && f.is_stack());
  }
  {
    std::vector<int> v = { 1, 2, 3 };
    fix<int, 4> f(v.begin(), v.end());
    CHECK(f.size() == 3 && f.is_stack());
    CHECK(f.data()[0] == 1 && f.data()[2] == 3);
  }
  {
    std::vector<int> v = { 1, 2, 3, 4 }; // exactly N: stack
    fix<int, 4> f(v.begin(), v.end());
    CHECK(f.size() == 4 && f.is_stack());
  }
  {
    std::vector<int> v = { 1, 2, 3, 4, 5 }; // N+1: heap
    fix<int, 4> f(v.begin(), v.end());
    CHECK(f.size() == 5 && f.is_heap() && f.capacity() >= 5);
    for (std::size_t i = 0; i < 5; ++i) CHECK(f.data()[i] == static_cast<int>(i + 1));
    CHECK(f.invariants());
  }
  {
    std::vector<int> v = { 1, 2 };
    fix<int, 0> f(v.begin(), v.end());
    CHECK(f.size() == 2 && f.is_heap() && f.data()[1] == 2);
  }
  {
    // non-trivial elements take the same two paths
    track::reset();
    {
      std::vector<track> v;
      v.emplace_back(1);
      v.emplace_back(2);
      v.emplace_back(3);
      fix<track, 2> f(v.begin(), v.end()); // heap: copies
      CHECK(f.size() == 3 && f.is_heap());
      CHECK(f.data()[0].value == 1 && f.data()[2].value == 3);
      CHECK(track::copies == 3);
    }
    CHECK(track::live == 0);
  }
  {
    // sized sentinel mismatch is a programming error; matching one works
    int arr[3] = { 4, 5, 6 };
    fix<int, 4> f(arr, arr + 3);
    CHECK(f.size() == 3 && f.data()[0] == 4);
  }
}

// ------------------------------------------------------------------
// 4. input (uncounted) range ctor
// ------------------------------------------------------------------
static void test_range_ctor_input()
{
  SECTION("ctors: input (uncounted) range");
  {
    int arr[1] = { 0 };
    fix<int, 4> f(input_only_it{ arr }, input_only_it{ arr });
    CHECK(f.empty() && f.is_stack());
  }
  {
    int arr[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    fix<int, 4> f(input_only_it{ arr }, input_only_it{ arr + 10 });
    CHECK(f.size() == 10 && f.is_heap());
    for (int i = 0; i < 10; ++i) CHECK(f.data()[i] == i);
    CHECK(f.invariants());
  }
  {
    // single-pass across several reallocations, N == 1
    int arr[5] = { 9, 8, 7, 6, 5 };
    fix<int, 1> f(input_only_it{ arr }, input_only_it{ arr + 5 });
    CHECK(f.size() == 5 && f.is_heap());
    for (int i = 0; i < 5; ++i) CHECK(f.data()[i] == 9 - i);
  }
  {
    // move_iterator input: move-only elements move through the append path
    move_only::reset();
    {
      move_only src[3]{ move_only(1), move_only(2), move_only(3) };
      move_only::reset_counts();
      fix<move_only, 2> f(std::allocator<move_only>{});
      f.append_uncounted_range_(std::make_move_iterator(src), std::make_move_iterator(src + 3));
      CHECK(f.size() == 3 && f.is_heap());
      CHECK(f.data()[0].value == 1 && f.data()[2].value == 3);
      CHECK(move_only::moves == 5); // 3 appends + 2 transfer moves, no copies possible
    }
    CHECK(move_only::live == 0);
  }
}

// ------------------------------------------------------------------
// 5. emplace growth incl. aliasing
// ------------------------------------------------------------------
static void test_emplace_growth()
{
  SECTION("emplace: growth, aliasing, multi-arg");
  {
    fix<int, 2> f(std::allocator<int>{});
    f.emplace_one_at_back_(1);
    CHECK(f.is_stack() && f.size() == 1 && f.data()[0] == 1);
    f.emplace_one_at_back_(2);
    CHECK(f.is_stack() && f.size() == 2);
    f.emplace_one_at_back_(3); // stack -> heap
    CHECK(f.is_heap() && f.size() == 3 && f.capacity() >= 3);
    CHECK(f.data()[0] == 1 && f.data()[2] == 3);
    for (int i = 4; i <= 64; ++i) f.emplace_one_at_back_(i);
    CHECK(f.size() == 64);
    for (std::size_t i = 0; i < 64; ++i) CHECK(f.data()[i] == static_cast<int>(i + 1));
    CHECK(f.invariants());
  }
  {
    // returned reference denotes the new tail and stays usable
    fix<int, 4> f(std::allocator<int>{});
    int& r = f.emplace_one_at_back_(10);
    r = 11;
    CHECK(f.data()[0] == 11);
  }
  {
    // aliasing across reallocation: the tail is materialized first
    fix<std::string, 2> f(std::allocator<std::string>{});
    f.emplace_one_at_back_(std::string("a"));
    f.emplace_one_at_back_(std::string("b"));
    f.emplace_one_at_back_(f.data()[0]);
    CHECK(f.size() == 3);
    CHECK(f.data()[0] == "a" && f.data()[1] == "b" && f.data()[2] == "a");
    f.emplace_one_at_back_(f.data()[2]);
    CHECK(f.data()[3] == "a");
  }
  {
    // multi-arg emplace
    fix<pair_like, 2> f(std::allocator<pair_like>{});
    f.emplace_one_at_back_(1, 2);
    f.emplace_one_at_back_(3, 4);
    f.emplace_one_at_back_(5, 6);
    CHECK(f.size() == 3 && f.is_heap());
    CHECK((f.data()[2] == pair_like{ 5, 6 }));
  }
  {
    // N == 0 grows purely on the heap
    fix<int, 0> f(std::allocator<int>{});
    for (int i = 0; i < 10; ++i) f.emplace_one_at_back_(i);
    CHECK(f.size() == 10 && f.is_heap());
    for (int i = 0; i < 10; ++i) CHECK(f.data()[i] == i);
  }
  {
    // copy-only elements transfer via copy, never move
    copy_only::reset();
    {
      fix<copy_only, 1> f(std::allocator<copy_only>{});
      f.emplace_one_at_back_(1);
      f.emplace_one_at_back_(2);
      f.emplace_one_at_back_(3);
      CHECK(f.size() == 3);
      CHECK(copy_only::copies > 0);
      CHECK(f.data()[0].value == 1 && f.data()[2].value == 3);
    }
    CHECK(copy_only::live == 0);
  }
}

// ------------------------------------------------------------------
// 6. reserve
// ------------------------------------------------------------------
static void test_reserve()
{
  SECTION("reserve");
  {
    fix<int, 4> f(std::allocator<int>{});
    f.reserve(0);
    CHECK(f.is_stack() && f.capacity() == 4); // noop
    f.reserve(4);
    CHECK(f.is_stack() && f.capacity() == 4); // noop at exactly N
  }
  {
    fix<int, 4> f(std::allocator<int>{});
    f.emplace_one_at_back_(1);
    f.emplace_one_at_back_(2);
    f.reserve(8); // stack -> heap, values preserved
    CHECK(f.is_heap() && f.capacity() >= 8 && f.size() == 2);
    CHECK(f.data()[0] == 1 && f.data()[1] == 2);
    f.reserve(2); // shrinking request is a noop
    CHECK(f.capacity() >= 8 && f.size() == 2);
    f.reserve(64);
    CHECK(f.capacity() >= 64 && f.data()[1] == 2);
    CHECK(f.invariants());
  }
  {
    fix<int, 0> f(std::allocator<int>{});
    f.reserve(5);
    CHECK(f.is_heap() && f.capacity() >= 5 && f.empty());
  }
  {
    fix<int, 4, capped_alloc<int, 10>> f((capped_alloc<int, 10>{}));
    bool threw = false;
    try {
      f.reserve(11);
    } catch (std::length_error const&) { threw = true; }
    CHECK(threw && f.is_stack() && f.empty()); // state untouched
  }
  {
    // reserve preserves non-trivial elements through the transfer
    track::reset();
    {
      fix<track, 2> f(std::allocator<track>{});
      f.emplace_one_at_back_(1);
      f.emplace_one_at_back_(2);
      f.reserve(10);
      CHECK(f.is_heap() && f.size() == 2);
      CHECK(f.data()[0].value == 1 && f.data()[1].value == 2);
    }
    CHECK(track::live == 0);
  }
}

// ------------------------------------------------------------------
// 7. shrink_to_fit both overloads
// ------------------------------------------------------------------
static void test_shrink()
{
  SECTION("shrink_to_fit");
  {
    fix<int, 4> f(std::allocator<int>{}); // stack: noop
    f.emplace_one_at_back_(1);
    f.shrink_to_fit();
    CHECK(f.is_stack() && f.size() == 1 && f.data()[0] == 1);
  }
  {
    fix<int, 4> f(std::allocator<int>{});
    for (int i = 0; i < 10; ++i) f.emplace_one_at_back_(i);
    f.reserve(64);
    f.shrink_to_fit(stdex::stay_on_heap); // tighten on heap
    CHECK(f.is_heap() && f.size() == 10);
    CHECK(f.capacity() >= 10);
    for (int i = 0; i < 10; ++i) CHECK(f.data()[i] == i);
  }
  {
    fix<int, 4> f(std::allocator<int>{});
    for (int i = 0; i < 10; ++i) f.emplace_one_at_back_(i);
    f.clear();
    for (int i = 0; i < 3; ++i) f.emplace_one_at_back_(i * 2);
    f.shrink_to_fit(); // heap, count <= N: demote to stack
    CHECK(f.is_stack() && f.size() == 3 && f.capacity() == 4);
    CHECK(f.data()[0] == 0 && f.data()[2] == 4);
  }
  {
    fix<int, 4> f(std::allocator<int>{});
    for (int i = 0; i < 6; ++i) f.emplace_one_at_back_(i);
    f.shrink_to_fit(); // heap, count > N: stay-on-heap tighten
    CHECK(f.is_heap() && f.size() == 6);
    for (int i = 0; i < 6; ++i) CHECK(f.data()[i] == i);
  }
  {
    fix<int, 4> f(std::allocator<int>{});
    for (int i = 0; i < 6; ++i) f.emplace_one_at_back_(i);
    f.clear();
    f.shrink_to_fit(); // empty heap: deallocate + demote
    CHECK(f.is_stack() && f.empty() && f.capacity() == 4);
  }
  {
    // count == capacity: noop
    fix<int, 4> f(6, 1);
    auto const cap = f.capacity();
    f.shrink_to_fit();
    CHECK(f.capacity() == cap && f.size() == 6);
  }
  {
    fix<int, 0> f(std::allocator<int>{});
    for (int i = 0; i < 5; ++i) f.emplace_one_at_back_(i);
    f.shrink_to_fit(); // N == 0 routes to stay-on-heap
    CHECK(f.is_heap() && f.size() == 5);
    for (int i = 0; i < 5; ++i) CHECK(f.data()[i] == i);
    f.clear();
    f.shrink_to_fit();
    CHECK(f.empty() && f.capacity() == 0);
  }
  {
    // non-trivial elements survive heap -> stack demotion
    track::reset();
    {
      fix<track, 4> f(std::allocator<track>{});
      for (int i = 0; i < 6; ++i) f.emplace_one_at_back_(i);
      f.clear();
      f.emplace_one_at_back_(7);
      f.emplace_one_at_back_(8);
      track::reset_counts();
      f.shrink_to_fit();
      CHECK(f.is_stack() && f.size() == 2);
      CHECK(f.data()[0].value == 7 && f.data()[1].value == 8);
    }
    CHECK(track::live == 0);
  }
}

// ------------------------------------------------------------------
// 8. reset vs clear
// ------------------------------------------------------------------
static void test_reset_clear()
{
  SECTION("reset vs clear");
  {
    fix<int, 4> f(std::allocator<int>{});
    for (int i = 0; i < 6; ++i) f.emplace_one_at_back_(i);
    f.clear(); // keeps heap storage
    CHECK(f.empty() && f.is_heap() && f.capacity() >= 6);
    f.emplace_one_at_back_(42); // reusable without reallocation
    CHECK(f.size() == 1 && f.data()[0] == 42);
  }
  {
    fix<int, 4> f(std::allocator<int>{});
    for (int i = 0; i < 6; ++i) f.emplace_one_at_back_(i);
    f.reset(); // deallocates + demotes
    CHECK(f.empty() && f.is_stack() && f.capacity() == 4);
    f.emplace_one_at_back_(1);
    CHECK(f.is_stack() && f.size() == 1);
  }
  {
    fix<int, 4> f(std::allocator<int>{}); // stack reset keeps the flag
    f.emplace_one_at_back_(1);
    f.reset();
    CHECK(f.empty() && f.is_stack());
  }
  {
    fix<int, 0> f(std::allocator<int>{}); // N == 0: reset == clear
    for (int i = 0; i < 3; ++i) f.emplace_one_at_back_(i);
    auto const cap = f.capacity();
    f.reset();
    CHECK(f.empty() && f.is_heap() && f.capacity() == cap);
  }
  {
    track::reset();
    {
      fix<track, 4> f(std::allocator<track>{});
      for (int i = 0; i < 6; ++i) f.emplace_one_at_back_(i);
      f.reset();
      CHECK(track::live == 0 && f.empty());
    }
    CHECK(track::live == 0);
  }
  {
    // dispose_ directly: destroys + releases, keeps the flag
    track::reset();
    {
      fix<track, 4> f(std::allocator<track>{});
      for (int i = 0; i < 6; ++i) f.emplace_one_at_back_(i);
      f.dispose_();
      CHECK(track::live == 0 && f.empty() && f.is_heap());
    }
    CHECK(track::live == 0);
  }
}

// ------------------------------------------------------------------
// 9. element construction / destruction primitives (detached memory:
//    no size bookkeeping involved, so no repair needed afterwards)
// ------------------------------------------------------------------
static void test_element_primitives()
{
  SECTION("construct/destroy element primitives");
  {
    // stack path bypasses the allocator; heap path goes through it
    fancy_counting_alloc<track>::reset();
    {
      fix<track, 4, fancy_counting_alloc<track>> f((fancy_counting_alloc<track>{}));
      alignas(track) unsigned char buf[sizeof(track)];
      track* slot = reinterpret_cast<track*>(buf);
      f.construct_element_unchecked_(slot, 5); // stack-active: no allocator traffic
      CHECK(fancy_counting_alloc<track>::construct_count == 0);
      CHECK(slot->value == 5);
      f.destroy_element_unchecked_(slot);
      CHECK(fancy_counting_alloc<track>::destroy_count == 0);
    }
    CHECK(track::live == 0);
  }
  {
    fancy_counting_alloc<track>::reset();
    track::reset();
    {
      fix<track, 4, fancy_counting_alloc<track>> f((fancy_counting_alloc<track>{}));
      f.reserve(5); // heap-active now (reserve(4) would be a noop at N == 4)
      alignas(track) unsigned char buf[sizeof(track)];
      track* slot = reinterpret_cast<track*>(buf);
      f.construct_element_unchecked_(slot, 6); // allocator path
      CHECK(fancy_counting_alloc<track>::construct_count == 1);
      f.destroy_element_unchecked_(slot); // allocator path
      CHECK(fancy_counting_alloc<track>::destroy_count == 1);
    }
    CHECK(track::live == 0);
  }
  {
    // value / fill / copy forms, both paths
    fix<int, 4> s(std::allocator<int>{});
    int out[4] = {};
    s.construct_elements_unchecked_(out, 3);
    CHECK(out[0] == 0 && out[2] == 0);
    s.construct_elements_unchecked_(out, 3, 9);
    CHECK(out[0] == 9 && out[2] == 9);
    int src[3] = { 1, 2, 3 };
    s.construct_elements_unchecked_(out, 3, src, src + 3);
    CHECK(out[0] == 1 && out[2] == 3);

    fix<int, 0> h(std::allocator<int>{});
    h.reserve(4);
    int hout[4] = {};
    h.construct_elements_unchecked_(hout, 3);
    h.construct_elements_unchecked_(hout, 3, 9);
    CHECK(hout[0] == 9);
    h.construct_elements_unchecked_(hout, 3, src, src + 3);
    CHECK(hout[2] == 3);
  }
  {
    // destroy_ on trivially-destructible storage is a noop
    fix<int, 4> f(std::allocator<int>{});
    int out[2] = { 1, 2 };
    f.destroy_elements_unchecked_(out, 2);
    CHECK(out[0] == 1);
    f.destroy_element_unchecked_(out);
    CHECK(out[0] == 1);
  }
  {
    // heap/stack counted construction through *_n_unchecked_
    track::reset();
    {
      fix<track, 4> s(std::allocator<track>{});
      s.try_construct_stack_n_unchecked_(2);
      CHECK(s.size() == 2 && s.is_stack());
      CHECK(s.data()[0].value == 0);
      s.clear();
      s.try_construct_stack_n_unchecked_(2, track(5));
      CHECK(s.data()[1].value == 5);
      s.clear();
      track src[2]{ track(1), track(2) };
      s.try_construct_stack_n_unchecked_(2, src, src + 2);
      CHECK(s.data()[0].value == 1 && s.data()[1].value == 2);
    }
    CHECK(track::live == 0);
  }
  {
    track::reset();
    {
      fix<track, 4> h(std::allocator<track>{});
      h.promote_to_heap_();
      h.construct_heap_n_unchecked_(3);
      CHECK(h.size() == 3 && h.is_heap());
      h.dispose_();
      h.construct_heap_n_unchecked_(2, track(8));
      CHECK(h.data()[1].value == 8);
      h.dispose_();
      track src[2]{ track(1), track(2) };
      h.construct_heap_n_unchecked_(2, src, src + 2);
      CHECK(h.data()[0].value == 1);
    }
    CHECK(track::live == 0);
  }
}

// ------------------------------------------------------------------
// 10. transfer + growth arithmetic + guards + statics
// ------------------------------------------------------------------
static void test_transfer_growth_statics()
{
  SECTION("transfer/growth/statics");
  {
    // calculate_growth_ on known capacities (fresh storage is exact)
    fix<int, 4> s(std::allocator<int>{});
    CHECK(s.calculate_growth_(5) == 6);  // 4 + 4/2 geometric
    CHECK(s.calculate_growth_(7) == 7);  // geometric insufficient
    CHECK(s.calculate_growth_(4) == 6);  // geometric still applies
    fix<int, 0> z(std::allocator<int>{});
    CHECK(z.calculate_growth_(1) == 1); // 0 capacity: exact request
    CHECK(z.calculate_growth_(9) == 9);
  }
  {
    // transfer_* move values into detached heap/stack blocks
    track::reset();
    {
      fix<track, 4> f(std::allocator<track>{});
      f.emplace_one_at_back_(1);
      f.emplace_one_at_back_(2); // stack source
      auto [blk, cap] = f.allocate_unchecked_(4);
      track* dst = std::to_address(blk);
      CHECK(cap >= 4);
      f.transfer_unchecked_(f.data(), 2, dst, f.test_alloc()); // allocator path
      CHECK(dst[0].value == 1 && dst[1].value == 2);
      f.destroy_elements_unchecked_(dst, 2); // stack-flagged fixture destroys raw
      std::allocator_traits<std::allocator<track>>::deallocate(f.test_alloc(), blk, cap);
      f.clear();
    }
    CHECK(track::live == 0);
  }
  {
    // allocation_guard dismiss vs fire, balanced through leak_alloc
    leak_alloc<int>::reset();
    {
      fix<int, 4, leak_alloc<int>> f((leak_alloc<int>{}));
      {
        auto [blk, cap] = f.allocate_unchecked_(3);
        typename fix<int, 4, leak_alloc<int>>::allocation_guard g{ blk, cap, f.test_alloc() };
        (void) blk;
        (void) cap;
      } // fires
      CHECK(leak_alloc<int>::allocs == 1 && leak_alloc<int>::deallocs == 1);
      {
        auto [blk, cap] = f.allocate_unchecked_(3);
        typename fix<int, 4, leak_alloc<int>>::allocation_guard g{ blk, cap, f.test_alloc() };
        g.dismiss();
        std::allocator_traits<leak_alloc<int>>::deallocate(f.test_alloc(), blk, cap);
      }
      CHECK(leak_alloc<int>::allocs == 2 && leak_alloc<int>::deallocs == 2);
    }
    CHECK(leak_alloc<int>::allocs == leak_alloc<int>::deallocs);
  }
  {
    // allocators_equal_ / max_size_ statics
    using FA = tagged_alloc<int, false, false, false>;
    CHECK(fix<int, 4, FA>::allocators_equal_(FA(1), FA(1)));
    CHECK(!fix<int, 4, FA>::allocators_equal_(FA(1), FA(2)));
    CHECK(fix<int, 4>::allocators_equal_(std::allocator<int>{}, std::allocator<int>{}));
    fix<int, 4, capped_alloc<int, 10>> f((capped_alloc<int, 10>{}));
    CHECK(f.max_size_(f.get_allocator()) == 10);
    bool threw = false;
    try {
      fix<int, 4>::throw_length_error_();
    } catch (std::length_error const&) { threw = true; }
    CHECK(threw);
    CHECK(fix<int, 4>::inline_capacity == 4);
  }
}

// ------------------------------------------------------------------
// 11. copy/move construction helpers
// ------------------------------------------------------------------
static void test_copy_move_construct()
{
  SECTION("copy/move_construct_from_");
  {
    // empty: no_op, nothing allocated
    fix<int, 4> d(std::allocator<int>{});
    fix<int, 4> s(std::allocator<int>{});
    d.copy_construct_from_(s);
    CHECK(d.empty() && d.is_stack());
  }
  {
    // copy fits on stack
    fix<int, 4> s(3, 5);
    fix<int, 4> d(std::allocator<int>{});
    d.copy_construct_from_(s);
    CHECK(d.size() == 3 && d.is_stack());
    CHECK(d.data()[2] == 5);
  }
  {
    // copy spills to heap
    fix<int, 4> s(6, 5);
    fix<int, 4> d(std::allocator<int>{});
    d.copy_construct_from_(s);
    CHECK(d.size() == 6 && d.is_heap());
    CHECK(d.data()[5] == 5);
    CHECK(d.invariants());
  }
  {
    // N == 0 copy
    fix<int, 0> s(2, 5);
    fix<int, 0> d(std::allocator<int>{});
    d.copy_construct_from_(s);
    CHECK(d.size() == 2 && d.data()[1] == 5);
  }
  {
    // move fits on stack: elements moved, donor keeps moved-from size
    fix<std::string, 4> s(std::allocator<std::string>{});
    s.emplace_one_at_back_(std::string("x"));
    s.emplace_one_at_back_(std::string("y"));
    fix<std::string, 4> d(std::allocator<std::string>{});
    CHECK(d.move_construct_from_(std::move(s)) == fix<std::string, 4>::move_outcome::move_elements);
    CHECK(d.size() == 2 && d.data()[0] == "x" && d.data()[1] == "y");
    CHECK(s.size() == 2); // donor keeps size; elements are moved-from (valid but unspecified)
  }
  {
    // move heap: steal, donor emptied
    fix<int, 4> s(6, 5);
    fix<int, 4> d(std::allocator<int>{});
    CHECK(d.move_construct_from_(std::move(s)) == fix<int, 4>::move_outcome::heap_steal);
    CHECK(d.size() == 6 && d.data()[5] == 5);
    CHECK(s.empty() && s.data() == nullptr);
  }
  {
    // move empty: no_op
    fix<int, 4> s(std::allocator<int>{});
    fix<int, 4> d(std::allocator<int>{});
    CHECK(d.move_construct_from_(std::move(s)) == fix<int, 4>::move_outcome::no_op);
  }
  {
    // move with explicit alloc, unequal: elementwise move, donor intact
    using TA = tagged_alloc<track, false, false, false>;
    track::reset();
    {
      fix<track, 4, TA> s(TA(1));
      s.emplace_one_at_back_(1);
      s.emplace_one_at_back_(2);
      s.emplace_one_at_back_(3);
      s.emplace_one_at_back_(4);
      s.emplace_one_at_back_(5); // heap
      fix<track, 4, TA> d(TA(2));
      CHECK(d.move_construct_from_(std::move(s), TA(2)) == fix<track, 4, TA>::move_outcome::move_elements);
      CHECK(d.size() == 5 && d.data()[4].value == 5);
      CHECK(s.size() == 5); // not stolen
    }
    CHECK(track::live == 0);
  }
  {
    // move with explicit alloc, equal: steal
    using TA = tagged_alloc<int, false, false, false>;
    fix<int, 4, TA> s(6, 5, TA(9));
    fix<int, 4, TA> d(TA(9));
    CHECK(d.move_construct_from_(std::move(s), TA(9)) == fix<int, 4, TA>::move_outcome::heap_steal);
    CHECK(d.size() == 6 && s.empty());
  }
}

// ------------------------------------------------------------------
// 12. copy/move assignment helpers
// ------------------------------------------------------------------
static void test_copy_move_assign()
{
  SECTION("copy/move_assignment_from_");
  {
    // self assignment is a noop
    fix<int, 4> f(3, 5);
    f.copy_assignment_from_(f);
    CHECK(f.size() == 3 && f.data()[0] == 5);
    CHECK(f.move_assignment_from_(std::move(f)) == fix<int, 4>::move_outcome::no_op);
    CHECK(f.size() == 3);
  }
  {
    // in-place same size, stack
    fix<int, 4> a(3, 1);
    fix<int, 4> b(3, 2);
    a.copy_assignment_from_(b);
    CHECK(a.size() == 3 && a.is_stack() && a.data()[0] == 2);
  }
  {
    // grow stack -> heap
    fix<int, 4> a(2, 1);
    fix<int, 4> b(6, 2);
    a.copy_assignment_from_(b);
    CHECK(a.size() == 6 && a.is_heap() && a.data()[5] == 2);
  }
  {
    // shrink heap -> smaller heap keeps storage
    fix<int, 4> a(6, 1);
    fix<int, 4> b(5, 2);
    auto const cap = a.capacity();
    a.copy_assignment_from_(b);
    CHECK(a.size() == 5 && a.is_heap() && a.capacity() == cap);
    CHECK(a.data()[0] == 2);
  }
  {
    // shrink onto stack keeps heap storage (assign never demotes)
    fix<int, 4> a(6, 1);
    fix<int, 4> b(2, 2);
    a.copy_assignment_from_(b);
    CHECK(a.size() == 2 && a.is_heap() && a.data()[1] == 2);
  }
  {
    // pocca: unequal allocators propagate and replace storage
    using TA = tagged_alloc<int, true, false, false>;
    fix<int, 4, TA> a(TA(1));
    a.emplace_one_at_back_(1);
    fix<int, 4, TA> c(TA(2));
    c.emplace_one_at_back_(7);
    c.emplace_one_at_back_(8);
    c.copy_assignment_from_(a); // TA(2) vs TA(1): pocca replaces
    CHECK(c.get_allocator().id_ == 1);
    CHECK(c.size() == 1 && c.data()[0] == 1);
  }
  {
    // pocca == false with unequal allocators: elements copied, alloc kept
    using TA = tagged_alloc<int, false, false, false>;
    fix<int, 4, TA> a(TA(1));
    a.emplace_one_at_back_(1);
    fix<int, 4, TA> c(TA(2));
    c.emplace_one_at_back_(7);
    c.copy_assignment_from_(a);
    CHECK(c.get_allocator().id_ == 2);
    CHECK(c.size() == 1 && c.data()[0] == 1);
  }
  {
    // move steal: both heap; pocma propagates the allocator, then steals
    using TA = tagged_alloc<int, false, true, false>;
    fix<int, 4, TA> a(TA(1));
    for (int i = 0; i < 6; ++i) a.emplace_one_at_back_(i);
    fix<int, 4, TA> b(TA(9));
    for (int i = 0; i < 6; ++i) b.emplace_one_at_back_(100 + i);
    CHECK(b.move_assignment_from_(std::move(a)) == fix<int, 4, TA>::move_outcome::heap_steal);
    CHECK(b.size() == 6 && b.data()[0] == 0 && a.empty());
    CHECK(b.get_allocator().id_ == 1); // pocma moved
  }
  {
    // keep_stack: stack target + small heap source stays elementwise
    std::vector<int> v = { 7, 8 };
    fix<int, 4> b(v.begin(), v.end());
    b.reserve(8);
    CHECK(b.is_heap() && b.size() == 2);
    fix<int, 4> a(2, 1);
    CHECK(a.move_assignment_from_(std::move(b)) == fix<int, 4>::move_outcome::move_elements);
    CHECK(a.size() == 2 && a.is_stack() && a.data()[0] == 7 && a.data()[1] == 8);
  }
  {
    // move large heap into stack target: keep_stack only covers small
    // sources, so this disposes + promotes + steals
    fix<int, 4> a(2, 1);
    fix<int, 4> b(6, 2);
    CHECK(a.move_assignment_from_(std::move(b)) == fix<int, 4>::move_outcome::heap_steal);
    CHECK(a.size() == 6 && a.is_heap() && a.data()[5] == 2 && b.empty());
  }
  {
    // N == 0 assignments
    fix<int, 0> a(2, 1);
    fix<int, 0> b(4, 2);
    a.copy_assignment_from_(b);
    CHECK(a.size() == 4 && a.data()[3] == 2);
    CHECK(a.move_assignment_from_(std::move(b)) == fix<int, 0>::move_outcome::heap_steal);
    CHECK(a.size() == 4 && b.empty());
  }
}

// ------------------------------------------------------------------
// 13. swap incl. mixed heap/stack
// ------------------------------------------------------------------
static void test_swap()
{
  SECTION("swap");
  {
    // stack <-> stack, different sizes
    fix<int, 4> a(2, 1);
    fix<int, 4> b(4, 2);
    a.swap(b);
    CHECK(a.size() == 4 && b.size() == 2);
    CHECK(a.data()[0] == 2 && b.data()[0] == 1);
    CHECK(a.is_stack() && b.is_stack());
  }
  {
    // heap <-> heap exchanges blocks, not elements
    fix<int, 4> a(6, 1);
    fix<int, 4> b(8, 2);
    auto const* pa = a.data();
    auto const* pb = b.data();
    a.swap(b);
    CHECK(a.size() == 8 && b.size() == 6);
    CHECK(a.data() == pb && b.data() == pa);
    CHECK(a.data()[0] == 2 && b.data()[0] == 1);
  }
  {
    // heap <-> stack both directions
    fix<int, 4> a(6, 1);
    fix<int, 4> b(2, 2);
    a.swap(b);
    CHECK(a.size() == 2 && a.is_stack() && a.data()[1] == 2);
    CHECK(b.size() == 6 && b.is_heap() && b.data()[0] == 1);
    b.swap(a); // and back
    CHECK(a.size() == 6 && a.is_heap() && b.size() == 2 && b.is_stack());
  }
  {
    // self swap is a noop
    fix<int, 4> a(3, 5);
    a.swap(a);
    CHECK(a.size() == 3 && a.data()[0] == 5);
  }
  {
    // N == 0 heap <-> heap
    fix<int, 0> a(2, 1);
    fix<int, 0> b(3, 2);
    a.swap(b);
    CHECK(a.size() == 3 && b.size() == 2 && a.data()[0] == 2);
  }
  {
    // pocs swaps allocators too
    using TA = tagged_alloc<int, false, false, true>;
    fix<int, 4, TA> a(TA(1));
    a.emplace_one_at_back_(1);
    fix<int, 4, TA> b(TA(2));
    b.emplace_one_at_back_(2);
    a.swap(b);
    CHECK(a.get_allocator().id_ == 2 && b.get_allocator().id_ == 1);
    CHECK(a.data()[0] == 2 && b.data()[0] == 1);
  }
  {
    // non-trivial elements survive all four combinations
    track::reset();
    {
      fix<track, 4> a(std::allocator<track>{});
      fix<track, 4> b(std::allocator<track>{});
      for (int i = 0; i < 6; ++i) a.emplace_one_at_back_(i); // heap
      for (int i = 0; i < 2; ++i) b.emplace_one_at_back_(100 + i); // stack
      a.swap(b);
      CHECK(a.size() == 2 && a.data()[0].value == 100);
      CHECK(b.size() == 6 && b.data()[5].value == 5);
      b.swap(a);
      CHECK(a.size() == 6 && b.size() == 2);
    }
    CHECK(track::live == 0);
  }
}

// ------------------------------------------------------------------
// 14. storage_equal_ / storage_compare_
// ------------------------------------------------------------------
static void test_equal_compare()
{
  SECTION("storage_equal_/storage_compare_");
  {
    fix<int, 4> a(3, 5);
    CHECK(a.storage_equal_(a)); // self, same storage
  }
  {
    fix<int, 4> a(std::allocator<int>{});
    fix<int, 4> b(std::allocator<int>{});
    CHECK(a.storage_equal_(b)); // both empty
    CHECK(a.storage_compare_(b) == std::strong_ordering::equivalent);
  }
  {
    fix<int, 4> a(3, 5);
    fix<int, 4> b(3, 5);
    fix<int, 4> c(3, 6);
    fix<int, 4> d(4, 5);
    CHECK(a.storage_equal_(b) && !(a.storage_equal_(c)) && !(a.storage_equal_(d)));
    CHECK(a.storage_compare_(b) == std::strong_ordering::equivalent);
    CHECK((a.storage_compare_(c) < 0) && (c.storage_compare_(a) > 0));
    CHECK((a.storage_compare_(d) < 0)); // prefix equal, shorter is less
  }
  {
    // cross capacity / cross heap-stack placement
    fix<int, 4> a(3, 5);
    fix<int, 8> b(3, 5);
    CHECK(a.storage_equal_(b));
    CHECK(a.storage_compare_(b) == std::strong_ordering::equivalent);
    fix<int, 4> h(6, 5);
    CHECK(!a.storage_equal_(h));
  }
  {
    // weak ordering degrades the common category, conversions hold
    fix<weak_t, 4> a(std::allocator<weak_t>{});
    fix<weak_t, 4> b(std::allocator<weak_t>{});
    a.emplace_one_at_back_(1);
    b.emplace_one_at_back_(2);
    static_assert(!noexcept(a.storage_compare_(b)));
    CHECK((a.storage_compare_(b) < 0));
    CHECK(a.storage_compare_(a) == std::weak_ordering::equivalent);
  }
  {
    // nothrow strong ordering keeps noexcept
    fix<track, 4> a(std::allocator<track>{});
    fix<track, 4> b(std::allocator<track>{});
    a.emplace_one_at_back_(1);
    static_assert(noexcept(a.storage_compare_(b)));
    static_assert(noexcept(a.storage_equal_(b)));
    CHECK((a.storage_compare_(b) > 0));
  }
  {
    // types without any comparison still instantiate the base
    fix<bare_t, 4> f(std::allocator<bare_t>{});
    f.emplace_one_at_back_(1);
    CHECK(f.size() == 1 && f.data()[0].v == 1);
  }
}

// ------------------------------------------------------------------
// 15. exception safety
// ------------------------------------------------------------------
static void test_exceptions()
{
  SECTION("exception safety");
  {
    // range ctor throws midway: nothing leaks, no object ever exists
    throw_copy::reset();
    int alive_after = -1;
    {
      std::vector<throw_copy> src;
      src.emplace_back(1);
      src.emplace_back(2);
      src.emplace_back(3);
      src.emplace_back(4);
      throw_copy::count = 0;
      throw_copy::throw_after = 1; // second heap copy throws
      try {
        fix<throw_copy, 2> f(src.begin(), src.end());
      } catch (std::runtime_error const&) {
        alive_after = throw_copy::alive - 4; // minus the source vector
      }
    }
    CHECK(alive_after == 0);
    CHECK(throw_copy::alive == 0);
    throw_copy::reset();
  }
  {
    // throwing tail: size/contents stable, new-buffer bulk destroyed
    throw_copy::reset();
    {
      fix<throw_copy, 2> f(std::allocator<throw_copy>{});
      f.emplace_one_at_back_(1);
      f.emplace_one_at_back_(2);
      throw_copy::throw_after = 1000000;
      throw_copy bad(999);
      throw_copy::count = 0;
      throw_copy::throw_after = 0; // next copy throws
      bool threw = false;
      try {
        f.emplace_one_at_back_(bad);
      } catch (std::runtime_error const&) { threw = true; }
      CHECK(threw && f.size() == 2);
      CHECK(f.data()[0].value == 1 && f.data()[1].value == 2);
      CHECK(throw_copy::alive == 3); // 2 in f + bad; bulk rolled back
    }
    CHECK(throw_copy::alive == 0);
    throw_copy::reset();
  }
  {
    // throwing move with available copy: transfer copies, old storage intact
    throw_move::reset();
    {
      fix<throw_move, 2> f(std::allocator<throw_move>{});
      f.emplace_one_at_back_(1);
      f.emplace_one_at_back_(2);
      throw_move::throw_after = 0; // moves throw, copies do not
      f.reserve(16); // strong: old elements untouched
      CHECK(f.size() == 2 && f.capacity() >= 16);
      CHECK(f.data()[0].value == 1 && f.data()[1].value == 2);
    }
    CHECK(throw_move::alive == 0);
    throw_move::reset();
  }
  {
    // move-only throwing move during transfer: basic guarantee, no leak
    throw_moveonly::reset();
    {
      fix<throw_moveonly, 4> f(std::allocator<throw_moveonly>{});
      f.emplace_one_at_back_(1);
      f.emplace_one_at_back_(2);
      f.emplace_one_at_back_(3);
      throw_moveonly::throw_after = 1; // second transfer move throws
      bool threw = false;
      try {
        f.reserve(16);
      } catch (std::runtime_error const&) { threw = true; }
      CHECK(threw);
      CHECK(f.size() == 3); // bookkeeping untouched
      CHECK(throw_moveonly::alive == 3); // partial new buffer rolled back
    }
    CHECK(throw_moveonly::alive == 0);
  }
}

// ------------------------------------------------------------------
// 16. fancy pointers end to end (raw-pointer observers; iterators need
//     implicit raw->fancy conversion, which the explicit fancy ctor blocks)
// ------------------------------------------------------------------
static void test_fancy()
{
  SECTION("fancy pointers");
  {
    fancy_counting_alloc<track>::reset();
    track::reset();
    {
      using F = fix<track, 4, fancy_counting_alloc<track>>;
      F f((fancy_counting_alloc<track>{}));
      CHECK(f.empty() && f.is_stack() && f.capacity() == 4);
      f.emplace_one_at_back_(1);
      f.emplace_one_at_back_(2);
      CHECK(fancy_counting_alloc<track>::construct_count == 0); // stack bypasses alloc
      for (int i = 3; i <= 6; ++i) f.emplace_one_at_back_(i); // spill to heap
      CHECK(f.is_heap() && f.size() == 6);
      CHECK(fancy_counting_alloc<track>::construct_count == 6);
      for (int i = 0; i < 6; ++i) CHECK(f.data()[i].value == i + 1);
      CHECK(std::to_address(f.data()) != nullptr);
      f.reserve(32);
      CHECK(f.capacity() >= 32 && f.data()[5].value == 6);
      f.shrink_to_fit(stdex::stay_on_heap);
      CHECK(f.is_heap() && f.size() == 6);
      f.clear();
      CHECK(f.empty() && f.is_heap());
      // 6 emplace + 6 reserve-transfer + 6 shrink-transfer constructs;
      // old-stack destroy bypasses the allocator, the rest go through it
      CHECK(fancy_counting_alloc<track>::construct_count == 18);
      CHECK(fancy_counting_alloc<track>::destroy_count == 18);
    }
    CHECK(track::live == 0);
  }
  {
    // range ctor + copy/move helpers on fancy storage
    using F = fix<int, 4, fancy_alloc<int>>;
    std::vector<int> v = { 1, 2, 3, 4, 5 };
    F h(v.begin(), v.end());
    CHECK(h.is_heap() && h.size() == 5 && h.data()[4] == 5);
    F d((fancy_alloc<int>{}));
    d.copy_construct_from_(h);
    CHECK(d.size() == 5 && d.data()[0] == 1);
    F m((fancy_alloc<int>{}));
    CHECK(m.move_construct_from_(std::move(d)) == F::move_outcome::heap_steal);
    CHECK(m.size() == 5 && d.empty());
    F s((fancy_alloc<int>{}));
    s.emplace_one_at_back_(9);
    m.swap(s);
    CHECK(m.size() == 1 && s.size() == 5 && s.data()[4] == 5);
    s.reset();
    CHECK(s.empty() && s.is_stack());
  }
  {
    // N == 0 fancy: pure heap from the first element
    using F = fix<int, 0, fancy_alloc<int>>;
    F f((fancy_alloc<int>{}));
    CHECK(f.data() == nullptr);
    for (int i = 0; i < 5; ++i) f.emplace_one_at_back_(i);
    CHECK(f.size() == 5 && f.data()[4] == 4);
  }
}

// ------------------------------------------------------------------
// 17. boundary values
// ------------------------------------------------------------------
static void test_boundary()
{
  SECTION("boundaries");
  {
    // n == N / N + 1 cutover for several N
    fix<int, 1> a(1);
    CHECK(a.is_stack() && a.size() == 1);
    fix<int, 1> b(2);
    CHECK(b.is_heap() && b.size() == 2);
  }
  {
    // single element through every path
    std::vector<int> v = { 7 };
    fix<int, 4> f(v.begin(), v.end());
    CHECK(f.size() == 1 && f.data()[0] == 7);
    int arr[1] = { 7 };
    fix<int, 4> g(input_only_it{ arr }, input_only_it{ arr + 1 });
    CHECK(g.size() == 1 && g.data()[0] == 7);
  }
  {
    // reserve(0) / reserve(capacity()) areatform noop; reserve(size()+0) noop
    fix<int, 4> f(3, 1);
    f.reserve(0);
    CHECK(f.is_stack());
    f.reserve(3);
    CHECK(f.is_stack() && f.size() == 3);
  }
  {
    // emplace onto exactly-full storage reallocates (assert path in debug)
    fix<int, 2> f(2, 1);
    CHECK(f.size() == f.capacity());
    f.emplace_one_at_back_(2);
    CHECK(f.size() == 3 && f.is_heap());
  }
  {
    // shrink with size == capacity is a noop
    fix<int, 4> f(6, 1);
    auto const before = f.data();
    auto const cap = f.capacity();
    f.shrink_to_fit();
    CHECK(f.data() == before && f.capacity() == cap);
  }
  {
    // assign exact-capacity and empty-model assigns
    fix<int, 4> f(4, 1);
    std::vector<int> v = { 9, 8, 7, 6 };
    f.assign_from_range_unchecked_(v.data(), static_cast<fix<int, 4>::size_type>(v.size()));
    CHECK(f.size() == 4 && f.data()[0] == 9);
    std::vector<int> e;
    f.assign_from_range_unchecked_(e.data(), 0);
    CHECK(f.empty() && f.is_stack());
  }
  {
    // iterators: arithmetic, const conversion, reverse
    fix<int, 4> f(3, 0);
    f.data()[0] = 1;
    f.data()[1] = 2;
    f.data()[2] = 3;
    CHECK(f.end() - f.begin() == 3);
    CHECK(f.begin()[2] == 3 && *(f.begin() + 1) == 2);
    fix<int, 4>::const_iterator cit = f.begin();
    CHECK(*cit == 1);
    CHECK(*f.cbegin() == 1 && f.cend() - f.cbegin() == 3);
    CHECK(*f.rbegin() == 3 && f.crend() - f.crbegin() == 3);
    auto rit = f.rbegin();
    CHECK(*rit == 3 && *(++rit) == 2);
    fix<int, 4> const& cf = f;
    CHECK(cf.data()[0] == 1 && *cf.begin() == 1 && *cf.rbegin() == 3);
  }
  {
    // observers on moved-from / stolen donors
    fix<int, 4> s(6, 5);
    fix<int, 4> d(std::allocator<int>{});
    CHECK(d.move_construct_from_(std::move(s)) == fix<int, 4>::move_outcome::heap_steal);
    CHECK(s.empty() && s.data() == nullptr && s.capacity() == 0);
  }
}

// ------------------------------------------------------------------
// 18. model fuzz vs std::vector
// ------------------------------------------------------------------
template <size_t N>
static void fuzz_one(unsigned long long seed, int ops)
{
  xorshift64 rng(seed);
  fix<int, N> a(std::allocator<int>{});
  fix<int, N> b(std::allocator<int>{});
  std::vector<int> ma;
  std::vector<int> mb;
  auto sync_check = [&](int line) {
    if (a.size() != ma.size() || b.size() != mb.size()) {
      std::println(stderr, "  [FAIL] fuzz size line={} seed={}", line, seed);
      ++g_fail;
      return;
    }
    ++g_pass;
    for (std::size_t i = 0; i < ma.size(); ++i) {
      if (a.data()[i] != ma[i]) {
        std::println(stderr, "  [FAIL] fuzz elem a line={} seed={}", line, seed);
        ++g_fail;
        return;
      }
    }
    for (std::size_t i = 0; i < mb.size(); ++i) {
      if (b.data()[i] != mb[i]) {
        std::println(stderr, "  [FAIL] fuzz elem b line={} seed={}", line, seed);
        ++g_fail;
        return;
      }
    }
    ++g_pass;
    if (!a.invariants() || !b.invariants()) {
      std::println(stderr, "  [FAIL] fuzz invariant line={} seed={}", line, seed);
      ++g_fail;
      return;
    }
    ++g_pass;
  };

  for (int i = 0; i < ops; ++i) {
    unsigned kind = static_cast<unsigned>(rng.next() % 10);
    int v = static_cast<int>(rng.next() % 1000);
    switch (kind) {
    case 0: // emplace
      a.emplace_one_at_back_(v);
      ma.push_back(v);
      break;
    case 1: { // reserve
      std::size_t k = static_cast<std::size_t>(rng.next() % 40);
      a.reserve(k);
      ma.reserve(k);
      break;
    }
    case 2: // shrink demote-or-tighten
      a.shrink_to_fit();
      ma.shrink_to_fit();
      break;
    case 3: // shrink stay on heap
      a.shrink_to_fit(stdex::stay_on_heap);
      ma.shrink_to_fit();
      break;
    case 4: // clear keeps storage
      a.clear();
      ma.clear();
      break;
    case 5: // reset releases
      a.reset();
      ma.clear();
      ma.shrink_to_fit();
      break;
    case 6: { // assign model contents (grow first when needed)
      if (ma.size() > a.capacity()) a.reserve(ma.size());
      a.assign_from_range_unchecked_(ma.data(), static_cast<typename fix<int, N>::size_type>(ma.size()));
      break;
    }
    case 7: { // copy-assign from a fresh snapshot
      fix<int, N> tmp(ma.begin(), ma.end());
      a.copy_assignment_from_(tmp);
      break;
    }
    case 8: { // move-assign from a fresh snapshot
      fix<int, N> tmp(ma.begin(), ma.end());
      (void) a.move_assignment_from_(std::move(tmp)); // post-state verified below
      break;
    }
    case 9: // swap roles
    default:
      a.swap(b);
      ma.swap(mb);
      break;
    }
    sync_check(__LINE__);
  }
}

static void test_fuzz()
{
  SECTION("fuzz vs vector model");
  fuzz_one<0>(0x12345678ULL, 3000);
  fuzz_one<1>(0x9E3779B9ULL, 3000);
  fuzz_one<4>(0xDEADBEEFULL, 3000);
  fuzz_one<4>(0x0BADF00DULL, 3000);
}

static void test_fuzz_leaks()
{
  SECTION("fuzz leaks (tracked type + counting allocator)");
  leak_alloc<track>::reset();
  track::reset();
  {
    xorshift64 rng(0xC0FFEEULL);
    fix<track, 2, leak_alloc<track>> a((leak_alloc<track>{}));
    std::vector<int> model;
    for (int i = 0; i < 2000; ++i) {
      unsigned kind = static_cast<unsigned>(rng.next() % 6);
      int v = static_cast<int>(rng.next() % 100);
      switch (kind) {
      case 0:
        a.emplace_one_at_back_(v);
        model.push_back(v);
        break;
      case 1:
        a.reserve(static_cast<std::size_t>(rng.next() % 30));
        break;
      case 2:
        a.shrink_to_fit();
        break;
      case 3:
        a.clear();
        model.clear();
        break;
      case 4:
        a.reset();
        model.clear();
        break;
      default: {
        if (model.size() > a.capacity()) a.reserve(model.size());
        std::vector<track> tmp;
        for (int x : model) tmp.emplace_back(x);
        a.assign_from_range_unchecked_(tmp.data(), static_cast<typename fix<track, 2, leak_alloc<track>>::size_type>(tmp.size()));
        break;
      }
      }
      if (a.size() != model.size()) {
        std::println(stderr, "  [FAIL] fuzz-leak size drift");
        ++g_fail;
        return;
      }
      ++g_pass;
      for (std::size_t k = 0; k < model.size(); ++k) {
        if (a.data()[k].value != model[k]) {
          std::println(stderr, "  [FAIL] fuzz-leak value drift");
          ++g_fail;
          return;
        }
      }
      ++g_pass;
    }
  }
  CHECK(track::live == 0);
  CHECK(leak_alloc<track>::allocs == leak_alloc<track>::deallocs);
  CHECK(leak_alloc<track>::allocs > 0); // the run really allocated
}

// ------------------------------------------------------------------
// 20. insert_uncounted_range_ (append + rotate)
// ------------------------------------------------------------------

// int-content comparison with invariant check for the insert sections below.
template <size_t N, class A = std::allocator<int>>
[[nodiscard]] static bool int_content_matches_(fix<int, N, A>& storage, std::vector<int> const& values)
{
  if (storage.size() != values.size()) return false;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (storage.data()[i] != values[i]) return false;
  }
  return storage.invariants();
}

static void test_insert_uncounted_range()
{
  SECTION("insert_uncounted_range_");
  {
    // empty source is a no-op at any position
    fix<int, 4> f(3, 1);
    int arr[1] = { 0 };
    f.insert_uncounted_range_(0, arr, arr);
    f.insert_uncounted_range_(3, arr, arr);
    CHECK(f.size() == 3 && f.is_stack());
    CHECK(int_content_matches_(f, { 1, 1, 1 }));
  }
  {
    // front / middle / back insertion through rotate
    fix<int, 8> f(std::allocator<int>{});
    int src[3] = { 10, 20, 30 };
    f.insert_uncounted_range_(0, src, src + 3);
    CHECK(int_content_matches_(f, { 10, 20, 30 }));
    int mid[2] = { 11, 12 };
    f.insert_uncounted_range_(1, mid, mid + 2);
    CHECK(int_content_matches_(f, { 10, 11, 12, 20, 30 }));
    int tail[1] = { 99 };
    f.insert_uncounted_range_(5, tail, tail + 1); // append-at-end path
    CHECK(int_content_matches_(f, { 10, 11, 12, 20, 30, 99 }));
  }
  {
    // single-pass source across reallocations
    fix<int, 4> f(2, -1);
    int arr[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    f.insert_uncounted_range_(1, input_only_it{ arr }, input_only_it{ arr + 10 });
    CHECK(f.size() == 12 && f.is_heap());
    CHECK(f.data()[0] == -1 && f.data()[1] == 0 && f.data()[2] == 1);
    CHECK(f.data()[10] == 9 && f.data()[11] == -1); // trailing original element
    CHECK(f.invariants());
  }
  {
    // non-trivial elements survive append + rotate without leaks
    track::reset();
    {
      fix<track, 4> f(std::allocator<track>{});
      f.emplace_one_at_back_(1);
      track src[2]{ track(7), track(8) };
      f.insert_uncounted_range_(0, src, src + 2);
      CHECK(f.size() == 3 && f.is_stack());
      CHECK(f.data()[0].value == 7 && f.data()[1].value == 8 && f.data()[2].value == 1);
      CHECK(f.invariants());
    }
    CHECK(track::live == 0);
  }
}

// ------------------------------------------------------------------
// 21. insert_counted_range_ (single-growth path)
// ------------------------------------------------------------------
static void test_insert_counted_range()
{
  SECTION("insert_counted_range_");
  {
    // count == 0 is a no-op; front/middle in-place insertion
    fix<int, 8> f(std::allocator<int>{});
    int src[3] = { 10, 20, 30 };
    f.insert_counted_range_(0, src, 0);
    CHECK(f.empty() && f.is_stack());
    f.insert_counted_range_(0, src, 3);
    CHECK(int_content_matches_(f, { 10, 20, 30 }));
    int mid[2] = { 11, 12 };
    f.insert_counted_range_(1, mid, 2);
    CHECK(int_content_matches_(f, { 10, 11, 12, 20, 30 }));
  }
  {
    // single element, count <= tail ([1,2,3,4] insert 5 at pos 2)
    fix<int, 8> f(std::allocator<int>{});
    for (int i = 1; i <= 4; ++i) f.emplace_one_at_back_(i);
    int five[1] = { 5 };
    f.insert_counted_range_(2, five, 1);
    CHECK(int_content_matches_(f, { 1, 2, 5, 3, 4 }) && f.is_stack());
  }
  {
    // spill in place (count > tail, fits) and count == tail boundary
    fix<int, 8> f(std::allocator<int>{});
    for (int i = 1; i <= 4; ++i) f.emplace_one_at_back_(i);
    int src[4] = { 10, 11, 12, 13 };
    f.insert_counted_range_(1, src, 4);
    CHECK(int_content_matches_(f, { 1, 10, 11, 12, 13, 2, 3, 4 }) && f.is_stack());
  }
  {
    // append-at-end in place (tail == 0 pure spill construction)
    fix<int, 8> f(std::allocator<int>{});
    for (int i = 1; i <= 4; ++i) f.emplace_one_at_back_(i);
    int tail[2] = { 20, 21 };
    f.insert_counted_range_(4, tail, 2);
    CHECK(int_content_matches_(f, { 1, 2, 3, 4, 20, 21 }) && f.is_stack());
  }
  {
    // stack -> heap and heap -> heap reallocation preserve order
    fix<int, 4> f(std::allocator<int>{});
    int a[2] = { 1, 2 };
    f.insert_counted_range_(0, a, 2);
    int b[5] = { 7, 8, 9, 10, 11 };
    f.insert_counted_range_(1, b, 5);
    CHECK(f.is_heap() && int_content_matches_(f, { 1, 7, 8, 9, 10, 11, 2 }));
    int c[4] = { 21, 22, 23, 24 };
    f.insert_counted_range_(0, c, 4);
    CHECK(int_content_matches_(f, { 21, 22, 23, 24, 1, 7, 8, 9, 10, 11, 2 }));
  }
  {
    // self-insert across reallocation: source aliases old storage
    fix<int, 4> f(std::allocator<int>{});
    for (int i = 1; i <= 4; ++i) f.emplace_one_at_back_(i);
    f.insert_counted_range_(2, f.data(), 2);
    CHECK(f.is_heap() && int_content_matches_(f, { 1, 2, 1, 2, 3, 4 }));
  }
  {
    // fitting self-insert reroutes to realloc (alias-safe), still exact
    fix<int, 8> f(std::allocator<int>{});
    for (int i = 1; i <= 4; ++i) f.emplace_one_at_back_(i);
    f.insert_counted_range_(1, f.data(), 2);
    CHECK(f.is_heap() && int_content_matches_(f, { 1, 1, 2, 2, 3, 4 }));
  }
  {
    // N == 0 pure-heap counted insert
    fix<int, 0> z(std::allocator<int>{});
    int src[3] = { 5, 6, 7 };
    z.insert_counted_range_(0, src, 3);
    CHECK(z.size() == 3 && z.is_heap() && z.data()[2] == 7);
    int more[2] = { 8, 9 };
    z.insert_counted_range_(1, more, 2);
    CHECK(z.size() == 5 && z.data()[0] == 5 && z.data()[1] == 8 && z.data()[3] == 6);
    CHECK(z.invariants());
  }
  {
    // length_error leaves state untouched
    fix<int, 4, capped_alloc<int, 10>> f((capped_alloc<int, 10>{}));
    f.emplace_one_at_back_(1);
    int src[3] = { 1, 2, 3 };
    bool threw = false;
    try {
      f.insert_counted_range_(0, src, 10);
    } catch (std::length_error const&) { threw = true; }
    CHECK(threw && f.size() == 1 && f.data()[0] == 1 && f.is_stack());
  }
  {
    // non-trivial elements across realloc + in-place without leaks
    track::reset();
    {
      fix<track, 2> f(std::allocator<track>{});
      track src[3]{ track(1), track(2), track(3) };
      f.insert_counted_range_(0, src, 3);
      CHECK(f.size() == 3 && f.is_heap());
      track more[1]{ track(9) };
      f.insert_counted_range_(3, more, 1);
      CHECK(f.size() == 4 && f.data()[3].value == 9 && f.data()[0].value == 1);
      CHECK(f.invariants());
    }
    CHECK(track::live == 0);
  }
  {
    // allocation balance through a reallocating insert
    leak_alloc<int>::reset();
    {
      fix<int, 4, leak_alloc<int>> f((leak_alloc<int>{}));
      int src[6] = { 1, 2, 3, 4, 5, 6 };
      f.insert_counted_range_(0, src, 6);
      CHECK(f.size() == 6 && f.is_heap());
    }
    CHECK(leak_alloc<int>::allocs == leak_alloc<int>::deallocs);
    CHECK(leak_alloc<int>::allocs > 0);
  }
  {
    // throwing source copy during reallocation: old storage untouched (strong)
    throw_copy::reset();
    fix<throw_copy, 2> f(std::allocator<throw_copy>{});
    f.emplace_one_at_back_(1);
    f.emplace_one_at_back_(2);
    throw_copy src[4]{ throw_copy(10), throw_copy(20), throw_copy(30), throw_copy(40) };
    throw_copy::count = 0;
    throw_copy::throw_after = 1; // second gap copy throws
    bool threw = false;
    try {
      f.insert_counted_range_(1, src, 4);
    } catch (std::runtime_error const&) { threw = true; }
    CHECK(threw && f.size() == 2 && f.is_stack());
    CHECK(f.data()[0].value == 1 && f.data()[1].value == 2);
    CHECK(throw_copy::alive == 2 + 4); // container + sources, new block rolled back
    throw_copy::reset();
  }
  {
    // partial relocation move throws: inner backout destroys the half-built
    // destination, size_ still covers the (moved-from but live) sources
    throw_moveonly_asn::reset();
    fix<throw_moveonly_asn, 8> f(std::allocator<throw_moveonly_asn>{});
    f.emplace_one_at_back_(1);
    f.emplace_one_at_back_(2);
    f.emplace_one_at_back_(3);
    f.emplace_one_at_back_(4);
    throw_moveonly_asn src[2]{ throw_moveonly_asn(10), throw_moveonly_asn(20) };
    throw_moveonly_asn::count = 0;
    throw_moveonly_asn::throw_after = 1; // first relocation move ok, second throws
    bool threw = false;
    try {
      f.insert_counted_range_(1, std::make_move_iterator(src), 2);
    } catch (std::runtime_error const&) { threw = true; }
    CHECK(threw && f.size() == 4);
    CHECK(throw_moveonly_asn::alive == 4 + 2); // container + sources, all accounted
    throw_moveonly_asn::reset();
  }
  {
    // throwing suffix construction in place: the holder destroys the shifted
    // tail, size_ still covers exactly the old elements (basic values, exact)
    throw_copy::reset();
    fix<throw_copy, 8> f(std::allocator<throw_copy>{});
    f.emplace_one_at_back_(1);
    f.emplace_one_at_back_(2);
    f.emplace_one_at_back_(3);
    throw_copy src[4]{ throw_copy(10), throw_copy(20), throw_copy(30), throw_copy(40) };
    throw_copy::count = 0;
    throw_copy::throw_after = 0; // suffix copy throws after shift + prefix assign
    bool threw = false;
    try {
      f.insert_counted_range_(1, src, 4);
    } catch (std::runtime_error const&) { threw = true; }
    CHECK(threw && f.size() == 3);
    CHECK(f.data()[0].value == 1 && f.data()[1].value == 10 && f.data()[2].value == 20);
    CHECK(throw_copy::alive == 3 + 4); // container exact, source intact: no leak
    throw_copy::reset();
  }
}

// ------------------------------------------------------------------
// 22. insert_range_ dispatch (sized / forward / single-pass)
// ------------------------------------------------------------------
static void test_insert_range_dispatch()
{
  SECTION("insert_range_ dispatch");
  {
    // sized sentinels (vector iterators): single-growth counted path
    fix<int, 8> f(std::allocator<int>{});
    std::vector<int> v = { 1, 2, 3 };
    f.insert_range_(0, v.begin(), v.end());
    CHECK(int_content_matches_(f, { 1, 2, 3 }));
  }
  {
    // forward but unsized (forward_list): counted via distance
    fix<int, 8> f(std::allocator<int>{});
    f.emplace_one_at_back_(1);
    std::forward_list<int> l = { 4, 5 };
    f.insert_range_(1, l.begin(), l.end());
    CHECK(int_content_matches_(f, { 1, 4, 5 }));
  }
  {
    // single-pass input: uncounted path, growth across reallocations
    fix<int, 4> f(2, -1);
    int arr[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    f.insert_range_(1, input_only_it{ arr }, input_only_it{ arr + 10 });
    CHECK(f.size() == 12 && f.is_heap());
    CHECK(f.data()[0] == -1 && f.data()[1] == 0 && f.data()[10] == 9 && f.data()[11] == -1);
    CHECK(f.invariants());
  }
  {
    // empty ranges are no-ops on every dispatch path
    fix<int, 8> f(std::allocator<int>{});
    std::vector<int> v = { 1 };
    f.insert_range_(0, v.begin(), v.begin());
    std::forward_list<int> l = { 2 };
    f.insert_range_(0, l.begin(), l.begin());
    int arr[1] = { 0 };
    f.insert_range_(0, input_only_it{ arr }, input_only_it{ arr });
    CHECK(f.empty() && f.is_stack());
  }
}

// ------------------------------------------------------------------
// 23. source_overlaps_storage_ predicate
// ------------------------------------------------------------------
static void test_source_overlaps()
{
  SECTION("source_overlaps_storage_");
  {
    // empty storage and empty ranges never overlap
    fix<int, 4> f(std::allocator<int>{});
    int ext[4] = { 1, 2, 3, 4 };
    CHECK(!f.source_overlaps_storage_(ext, 4));
    CHECK(!f.source_overlaps_storage_(ext, 0));
    fix<int, 4> g(3, 1);
    CHECK(!g.source_overlaps_storage_(ext, 0)); // count == 0 short-circuits
  }
  {
    // sources starting inside [data(), data() + size()) overlap, even when
    // count runs past the end (no src + count is formed on this path)
    fix<int, 4> g(3, 1);
    CHECK(g.source_overlaps_storage_(g.data(), 2));
    CHECK(g.source_overlaps_storage_(g.data() + 1, 2));
    CHECK(g.source_overlaps_storage_(g.data() + 2, 100));
    // one-past-the-end only touches: no overlap
    CHECK(!g.source_overlaps_storage_(g.data() + 3, 2));
  }
  {
    // disjoint external storage never overlaps
    fix<int, 4> g(3, 1);
    int ext[4] = { 1, 2, 3, 4 };
    CHECK(!g.source_overlaps_storage_(ext, 4));
  }
  {
    // non-contiguous iterators always report false, even over our own data
    fix<int, 4> g(3, 1);
    CHECK(!g.source_overlaps_storage_(input_only_it{ g.data() }, 2));
  }
  {
    // N == 0 heap storage overlaps its own blocks
    fix<int, 0> z(2, 5);
    CHECK(z.source_overlaps_storage_(z.data(), 1));
    int ext[1] = { 9 };
    CHECK(!z.source_overlaps_storage_(ext, 1));
  }
}

// ------------------------------------------------------------------
// 24. destruction_guard adopt-and-release
// ------------------------------------------------------------------
static void test_destruction_guard()
{
  SECTION("destruction_guard");
  {
    // heap mode fires: adopted range destroyed via the allocator
    track::reset();
    {
      fix<track, 4> f(std::allocator<track>{});
      auto [blk, cap] = f.allocate_unchecked_(4);
      track* raw      = std::to_address(blk);
      auto& alloc     = f.test_alloc();
      std::allocator_traits<std::allocator<track>>::construct(alloc, raw, 1);
      std::allocator_traits<std::allocator<track>>::construct(alloc, raw + 1, 2);
      CHECK(track::live == 2);
      {
        typename fix<track, 4>::destruction_guard g{ raw, raw + 2, alloc, true };
      } // fires on scope exit
      CHECK(track::live == 0);
      CHECK(raw->value == 1); // storage untouched, only lifetimes ended
      std::allocator_traits<std::allocator<track>>::deallocate(alloc, blk, cap);
    }
    CHECK(track::live == 0);
  }
  {
    // dismiss() hands responsibility back: nothing destroyed
    track::reset();
    {
      fix<track, 4> f(std::allocator<track>{});
      auto [blk, cap] = f.allocate_unchecked_(4);
      track* raw      = std::to_address(blk);
      auto& alloc     = f.test_alloc();
      std::allocator_traits<std::allocator<track>>::construct(alloc, raw, 5);
      {
        typename fix<track, 4>::destruction_guard g{ raw, raw + 1, alloc, true };
        g.dismiss();
      }
      CHECK(track::live == 1 && raw->value == 5);
      f.destroy_elements_unchecked_(raw, 1); // stack-active fixture: plain destroy
      CHECK(track::live == 0);
      std::allocator_traits<std::allocator<track>>::deallocate(alloc, blk, cap);
    }
    CHECK(track::live == 0);
  }
  {
    // stack mode (use_allocator == false) destroys without allocator traffic
    track::reset();
    {
      fix<track, 4> f(std::allocator<track>{});
      alignas(track) unsigned char buf[2 * sizeof(track)];
      track* slot = reinterpret_cast<track*>(buf);
      std::construct_at(slot, 7);
      std::construct_at(slot + 1, 8);
      CHECK(track::live == 2);
      {
        typename fix<track, 4>::destruction_guard g{ slot, slot + 2, f.test_alloc(), false };
      }
      CHECK(track::live == 0);
    }
    CHECK(track::live == 0);
  }
  {
    // custom-destroy allocators are honored even for trivially-destructible types
    fancy_counting_alloc<int>::reset();
    fix<int, 4, fancy_counting_alloc<int>> f((fancy_counting_alloc<int>{}));
    auto [blk, cap] = f.allocate_unchecked_(2);
    {
      typename fix<int, 4, fancy_counting_alloc<int>>::destruction_guard g{
        std::to_address(blk), std::to_address(blk) + 2, f.test_alloc(), true
      };
    }
    CHECK(fancy_counting_alloc<int>::destroy_count == 2);
    f.test_alloc().deallocate(blk, cap);
  }
  {
    // trivially-destructible ranges: fire and dismiss are both silent no-ops
    fix<int, 4> f(std::allocator<int>{});
    auto [blk, cap] = f.allocate_unchecked_(4);
    int* raw        = std::to_address(blk);
    {
      typename fix<int, 4>::destruction_guard g{ raw, raw + 4, f.test_alloc(), true };
    }
    {
      typename fix<int, 4>::destruction_guard g{ raw, raw + 4, f.test_alloc(), false };
      g.dismiss();
    }
    std::allocator_traits<std::allocator<int>>::deallocate(f.test_alloc(), blk, cap);
    CHECK(f.invariants() && f.empty());
  }
  {
    // empty range fires trivially
    fix<int, 4> f(std::allocator<int>{});
    auto [blk, cap] = f.allocate_unchecked_(4);
    int* raw        = std::to_address(blk);
    {
      typename fix<int, 4>::destruction_guard g{ raw, raw, f.test_alloc(), true };
    }
    std::allocator_traits<std::allocator<int>>::deallocate(f.test_alloc(), blk, cap);
    CHECK(true);
  }
}

// ------------------------------------------------------------------
// 25. resize + resize_reallocate_ + resize_impl_
// ------------------------------------------------------------------
static void test_resize()
{
  SECTION("resize");
  {
    // stack value-init growth, trim, equal no-op, clear to zero
    fix<int, 4> f(std::allocator<int>{});
    f.resize(3);
    CHECK(f.size() == 3 && f.is_stack());
    CHECK(f.data()[0] == 0 && f.data()[2] == 0);
    f.resize(2, 9); // trim ignores the fill value
    CHECK(f.size() == 2 && f.is_stack() && f.data()[1] == 0);
    f.resize(4, 7); // grow within capacity
    CHECK(f.size() == 4 && f.is_stack());
    CHECK(f.data()[2] == 7 && f.data()[3] == 7 && f.data()[0] == 0);
    f.resize(4); // equal: no-op, values untouched
    CHECK(f.size() == 4 && f.data()[0] == 0);
    f.resize(0);
    CHECK(f.empty() && f.is_stack() && f.capacity() == 4);
    CHECK(f.invariants());
  }
  {
    // non-trivial lifetimes through trim and growth
    track::reset();
    {
      fix<track, 4> f(std::allocator<track>{});
      f.resize(3);
      CHECK(track::live == 3);
      f.resize(1);
      CHECK(track::live == 1 && f.size() == 1);
      f.resize(4, track(5));
      CHECK(track::live == 4);
      CHECK(f.data()[3].value == 5 && f.data()[0].value == 0);
      CHECK(f.invariants());
    }
    CHECK(track::live == 0);
  }
  {
    // stack -> heap reallocation (value-init and fill), contents preserved
    fix<int, 4> f(std::allocator<int>{});
    f.resize(2, 1);
    f.resize(10);
    CHECK(f.is_heap() && f.size() == 10);
    CHECK(f.data()[0] == 1 && f.data()[1] == 1 && f.data()[2] == 0 && f.data()[9] == 0);
    CHECK(f.capacity() >= 10 && f.invariants());
    f.resize(12, 3); // heap -> heap reallocation with fill tail
    CHECK(f.is_heap() && f.size() == 12);
    CHECK(f.data()[0] == 1 && f.data()[10] == 3 && f.data()[11] == 3);
    f.resize(11); // heap trim keeps heap storage and capacity
    CHECK(f.is_heap() && f.size() == 11 && f.data()[0] == 1);
    auto const cap = f.capacity();
    f.resize(11, 9); // equal on heap: no-op, fill value ignored
    CHECK(f.data()[0] == 1 && f.capacity() == cap);
    CHECK(f.invariants());
  }
  {
    // heap growth within capacity: no reallocation, storage stable
    fix<int, 4> f(std::allocator<int>{});
    f.resize(8, 2);
    auto const* p   = f.data();
    auto const cap  = f.capacity();
    f.resize(6);
    f.resize(7, 5);
    CHECK(f.data() == p && f.capacity() == cap && f.is_heap());
    CHECK(f.data()[6] == 5 && f.data()[0] == 2);
  }
  {
    // fill value aliasing old storage across reallocation
    fix<std::string, 2> f(std::allocator<std::string>{});
    f.resize(2, std::string("ab"));
    f.resize(6, f.data()[0]);
    CHECK(f.size() == 6 && f.is_heap());
    CHECK(f.data()[0] == "ab" && f.data()[2] == "ab" && f.data()[5] == "ab");
    CHECK(f.invariants());
  }
  {
    // N == 0 pure-heap paths
    fix<int, 0> f(std::allocator<int>{});
    f.resize(3);
    CHECK(f.is_heap() && f.size() == 3 && f.data()[2] == 0);
    f.resize(5, 4);
    CHECK(f.size() == 5 && f.data()[4] == 4 && f.data()[0] == 0);
    f.resize(2);
    CHECK(f.size() == 2 && f.is_heap());
    f.resize(2);
    CHECK(f.size() == 2);
    f.resize(0);
    CHECK(f.empty() && f.is_heap() && f.invariants());
  }
  {
    // length_error leaves state untouched (both overloads)
    fix<int, 4, capped_alloc<int, 10>> f((capped_alloc<int, 10>{}));
    bool threw = false;
    try {
      f.resize(11);
    } catch (std::length_error const&) { threw = true; }
    CHECK(threw && f.empty() && f.is_stack());
    threw = false;
    try {
      f.resize(11, 1);
    } catch (std::length_error const&) { threw = true; }
    CHECK(threw && f.empty() && f.is_stack());
  }
  {
    // throwing fill during reallocation: old stack contents intact
    throw_copy::reset();
    fix<throw_copy, 2> f(std::allocator<throw_copy>{});
    f.emplace_one_at_back_(1);
    f.emplace_one_at_back_(2);
    throw_copy bad(999);
    throw_copy::count = 0;
    throw_copy::throw_after = 0;
    bool threw = false;
    try {
      f.resize(5, bad);
    } catch (std::runtime_error const&) { threw = true; }
    CHECK(threw && f.size() == 2 && f.is_stack());
    CHECK(f.data()[0].value == 1 && f.data()[1].value == 2);
    CHECK(throw_copy::alive == 3); // 2 in f + bad; new block fully rolled back
    throw_copy::reset();
  }
  {
    // throwing fill in place (heap, spare capacity): size/contents stable
    throw_copy::reset();
    fix<throw_copy, 2> f(std::allocator<throw_copy>{});
    f.emplace_one_at_back_(1);
    f.emplace_one_at_back_(2);
    f.reserve(8);
    throw_copy bad(7);
    throw_copy::count = 0;
    throw_copy::throw_after = 0;
    bool threw = false;
    try {
      f.resize(5, bad);
    } catch (std::runtime_error const&) { threw = true; }
    CHECK(threw && f.size() == 2 && f.is_heap());
    CHECK(f.data()[0].value == 1 && f.data()[1].value == 2);
    CHECK(throw_copy::alive == 3);
    throw_copy::reset();
  }
  {
    // throwing move with viable copy: transfer copies, old storage intact
    throw_move::reset();
    fix<throw_move, 2> f(std::allocator<throw_move>{});
    f.emplace_one_at_back_(1);
    f.emplace_one_at_back_(2);
    throw_move::throw_after = 0; // moves throw, copies do not
    f.resize(8); // value-init tail cannot throw; transfer falls back to copy
    CHECK(f.size() == 8 && f.is_heap());
    CHECK(f.data()[0].value == 1 && f.data()[1].value == 2);
    CHECK(throw_move::alive == 8);
    throw_move::reset();
  }
  {
    // resize_reallocate_ directly: value-init and fill, stack -> heap
    fix<int, 4> f(2, 5);
    f.resize_reallocate_(6);
    CHECK(f.is_heap() && f.size() == 6);
    CHECK(f.data()[0] == 5 && f.data()[1] == 5 && f.data()[5] == 0);
    CHECK(f.invariants());
    fix<int, 4> g(2, 5);
    g.resize_reallocate_(6, 7);
    CHECK(g.is_heap() && g.size() == 6);
    CHECK(g.data()[0] == 5 && g.data()[2] == 7 && g.data()[5] == 7);
    CHECK(g.invariants());
  }
  {
    // resize_reallocate_ directly: heap -> heap keeps old contents
    fix<int, 4> f(6, 1);
    auto const old_cap = f.capacity();
    (void) old_cap;
    f.resize_reallocate_(9);
    CHECK(f.is_heap() && f.size() == 9 && f.capacity() >= 9);
    for (std::size_t i = 0; i < 6; ++i) CHECK(f.data()[i] == 1);
    CHECK(f.data()[8] == 0 && f.invariants());
  }
  {
    // resize_impl_ directly: trim, in-place growth (both arities), reallocation
    fix<int, 4> f(2, 5);
    f.resize_impl_(4, 7);
    CHECK(f.is_stack() && f.size() == 4);
    CHECK(f.data()[0] == 5 && f.data()[2] == 7);
    f.resize_impl_(1);
    CHECK(f.size() == 1 && f.data()[0] == 5);
    f.resize_impl_(6);
    CHECK(f.is_heap() && f.size() == 6 && f.data()[0] == 5 && f.data()[5] == 0);
    CHECK(f.invariants());
  }
  {
    // allocation balance through a reallocating resize
    leak_alloc<int>::reset();
    {
      fix<int, 4, leak_alloc<int>> f((leak_alloc<int>{}));
      f.resize(2, 1);
      f.resize(10);
      CHECK(f.size() == 10 && f.is_heap());
    }
    CHECK(leak_alloc<int>::allocs == leak_alloc<int>::deallocs);
    CHECK(leak_alloc<int>::allocs > 0);
  }
}

// ------------------------------------------------------------------
// 26. reallocation_guard adopt-and-release
// ------------------------------------------------------------------
static void test_reallocation_guard()
{
  SECTION("reallocation_guard");
  {
    // fires: adopted tail destroyed via the allocator, block deallocated
    track::reset();
    leak_alloc<track>::reset();
    {
      fix<track, 4, leak_alloc<track>> f((leak_alloc<track>{}));
      auto [blk, cap] = f.allocate_unchecked_(4);
      track* raw      = std::to_address(blk);
      auto& alloc     = f.test_alloc();
      std::allocator_traits<leak_alloc<track>>::construct(alloc, raw, 1);
      std::allocator_traits<leak_alloc<track>>::construct(alloc, raw + 1, 2);
      std::allocator_traits<leak_alloc<track>>::construct(alloc, raw + 2, 3);
      CHECK(track::live == 3);
      {
        typename fix<track, 4, leak_alloc<track>>::reallocation_guard g{ blk, cap, raw, raw + 3, alloc };
      } // fires: destroys 3 + deallocates
      CHECK(track::live == 0);
    }
    CHECK(track::live == 0);
    CHECK(leak_alloc<track>::allocs == leak_alloc<track>::deallocs);
    CHECK(leak_alloc<track>::allocs > 0);
  }
  {
    // partial adopted range: only the covered suffix is destroyed
    track::reset();
    {
      fix<track, 4> f(std::allocator<track>{});
      auto [blk, cap] = f.allocate_unchecked_(4);
      track* raw      = std::to_address(blk);
      auto& alloc     = f.test_alloc();
      std::allocator_traits<std::allocator<track>>::construct(alloc, raw, 1);
      std::allocator_traits<std::allocator<track>>::construct(alloc, raw + 1, 2);
      std::allocator_traits<std::allocator<track>>::construct(alloc, raw + 2, 3);
      {
        typename fix<track, 4>::reallocation_guard g{ blk, cap, raw + 1, raw + 3, alloc };
      } // fires: destroys [1, 3), deallocates
      CHECK(track::live == 1 && raw->value == 1);
      std::allocator_traits<std::allocator<track>>::destroy(alloc, raw);
      CHECK(track::live == 0);
    }
    CHECK(track::live == 0);
  }
  {
    // dismiss(): neither destroys nor deallocates; owner keeps both duties
    track::reset();
    leak_alloc<track>::reset();
    {
      fix<track, 4, leak_alloc<track>> f((leak_alloc<track>{}));
      auto [blk, cap] = f.allocate_unchecked_(4);
      track* raw      = std::to_address(blk);
      auto& alloc     = f.test_alloc();
      std::allocator_traits<leak_alloc<track>>::construct(alloc, raw, 9);
      {
        typename fix<track, 4, leak_alloc<track>>::reallocation_guard g{ blk, cap, raw, raw + 1, alloc };
        g.dismiss();
      }
      CHECK(track::live == 1 && raw->value == 9);
      CHECK(leak_alloc<track>::deallocs == 0);
      std::allocator_traits<leak_alloc<track>>::destroy(alloc, raw);
      std::allocator_traits<leak_alloc<track>>::deallocate(alloc, blk, cap);
    }
    CHECK(track::live == 0);
    CHECK(leak_alloc<track>::allocs == leak_alloc<track>::deallocs);
  }
  {
    // custom-destroy allocators are honored even for trivially-destructible types
    fancy_counting_alloc<int>::reset();
    fix<int, 4, fancy_counting_alloc<int>> f((fancy_counting_alloc<int>{}));
    auto [blk, cap] = f.allocate_unchecked_(4);
    {
      typename fix<int, 4, fancy_counting_alloc<int>>::reallocation_guard g{
        blk, cap, std::to_address(blk) + 1, std::to_address(blk) + 3, f.test_alloc()
      };
    } // fires: destroys covered pair through the allocator, frees the block
    CHECK(fancy_counting_alloc<int>::destroy_count == 2);
  }
  {
    // trivially-destructible, default allocator: fire is deallocate-only
    leak_alloc<int>::reset();
    {
      fix<int, 4, leak_alloc<int>> f((leak_alloc<int>{}));
      auto [blk, cap] = f.allocate_unchecked_(4);
      {
        typename fix<int, 4, leak_alloc<int>>::reallocation_guard g{
          blk, cap, std::to_address(blk), std::to_address(blk) + 4, f.test_alloc()
        };
      }
      CHECK(leak_alloc<int>::allocs == 1 && leak_alloc<int>::deallocs == 1);
    }
    CHECK(leak_alloc<int>::allocs == leak_alloc<int>::deallocs);
  }
  {
    // empty adopted range fires as deallocate-only (block already freed)
    fix<int, 4> f(std::allocator<int>{});
    auto [blk, cap] = f.allocate_unchecked_(4);
    int* raw        = std::to_address(blk);
    {
      typename fix<int, 4>::reallocation_guard g{ blk, cap, raw, raw, f.test_alloc() };
    }
    CHECK(true); // reached without touching an empty range
  }
}

// ------------------------------------------------------------------
// 19. constexpr evaluation (heap paths only; stack constexpr needs
//     C++26 start_lifetime, unavailable in this toolchain)
// ------------------------------------------------------------------
consteval bool heap_ce_basic()
{
  fix<int, 0> s(std::allocator<int>{});
  if (s.size() != 0 || !s.empty() || !s.is_heap()) return false;
  s.emplace_one_at_back_(1);
  s.emplace_one_at_back_(2);
  s.emplace_one_at_back_(3);
  if (s.size() != 3 || s.data()[0] != 1 || s.data()[2] != 3) return false;
  if (!s.storage_equal_(s)) return false;
  if (!(s.storage_compare_(s) == std::strong_ordering::equivalent)) return false;
  s.reserve(16);
  if (s.capacity() < 16 || s.data()[1] != 2) return false;
  s.shrink_to_fit(stdex::stay_on_heap);
  if (s.size() != 3 || s.capacity() < 3) return false;
  s.clear();
  if (!s.empty()) return false;
  s.emplace_one_at_back_(9);
  if (s.data()[0] != 9) return false;
  s.reset(); // N == 0: reset == clear, storage retained
  return s.empty() && s.is_heap();
}

consteval bool heap_ce_ctors()
{
  fix<int, 0> a(3, 7);
  if (a.size() != 3 || a.data()[2] != 7) return false;
  int arr[4] = { 4, 3, 2, 1 };
  fix<int, 0> b(arr, arr + 4);
  if (b.size() != 4 || b.data()[0] != 4) return false;
  if (!a.storage_equal_(a) || a.storage_equal_(b)) return false;
  if (!((a.storage_compare_(b) > 0))) return false;
  for (int i = 0; i < 20; ++i) b.emplace_one_at_back_(i); // constexpr realloc chain
  if (b.size() != 24 || b.data()[23] != 19) return false;
  return true;
}

static void test_constexpr()
{
  SECTION("constexpr (heap)");
  static_assert(heap_ce_basic());
  static_assert(heap_ce_ctors());
  CHECK(heap_ce_basic());
  CHECK(heap_ce_ctors());
  CHECK(fix<int, 0>::inline_capacity == 0);
}

int main()
{
  test_empty_state();
  test_count_ctors();
  test_range_ctor_forward();
  test_range_ctor_input();
  test_emplace_growth();
  test_reserve();
  test_shrink();
  test_reset_clear();
  test_element_primitives();
  test_transfer_growth_statics();
  test_copy_move_construct();
  test_copy_move_assign();
  test_swap();
  test_equal_compare();
  test_exceptions();
  test_insert_uncounted_range();
  test_insert_counted_range();
  test_insert_range_dispatch();
  test_source_overlaps();
  test_destruction_guard();
  test_resize();
  test_reallocation_guard();
  test_fancy();
  test_boundary();
  test_fuzz();
  test_fuzz_leaks();
  test_constexpr();

  std::println("\npass={} fail={}", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
