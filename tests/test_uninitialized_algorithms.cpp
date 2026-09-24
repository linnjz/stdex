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

// test_uninitialized_move.cpp
// Covers stdex::uninitialized_move (both overloads) + fancy-pointer allocator.
#include <concepts>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <memory>
#include <new>
#include <print>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "uninitialized_algorithms.hpp"

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

#define SECTION(name) std::println("\n=== {} ===", name)

// ------------------------------------------------------------------
// tracking types
// ------------------------------------------------------------------
struct move_tracker
{
  int value         = 0;
  bool moved_from   = false;
  static inline int live    = 0;
  static inline int moves   = 0;
  static inline int copies  = 0;
  static inline int destroys = 0;

  move_tracker() { ++live; }
  explicit move_tracker(int v) : value(v) { ++live; }
  move_tracker(move_tracker&& o) noexcept : value(o.value), moved_from(false)
  {
    ++live;
    ++moves;
    o.value      = -1;
    o.moved_from = true;
  }
  move_tracker(move_tracker const& o) : value(o.value) { ++live; ++copies; }
  ~move_tracker() { --live; ++destroys; }

  move_tracker& operator=(move_tracker&&) = default;
  move_tracker& operator=(move_tracker const&) = default;

  static void reset() { live = 0; moves = 0; copies = 0; destroys = 0; }
  static void reset_counts() { moves = 0; copies = 0; destroys = 0; }
};

struct thrower
{
  int value = 0;
  static inline int alive       = 0;
  static inline int move_count  = 0;
  static inline int throw_after = 1000000;
  static inline int destroyed   = 0;

  thrower() { ++alive; }
  explicit thrower(int v) : value(v) { ++alive; }
  thrower(thrower&& o) : value(o.value)
  {
    if (move_count++ >= throw_after) throw std::runtime_error("move throws");
    ++alive;
    o.value = -1;
  }
  thrower(thrower const&) = delete;
  ~thrower() { --alive; ++destroyed; }
  static void reset()
  {
    alive = 0;
    move_count = 0;
    throw_after = 1000000;
    destroyed = 0;
  }
  static void reset_counts()
  {
    move_count = 0;
    destroyed = 0;
  }
};

// ------------------------------------------------------------------
// fancy pointer + allocators
// ------------------------------------------------------------------
template <class T>
struct fancy_ptr
{
  T* p_ = nullptr;

  using value_type       = T;
  using element_type     = T;
  using difference_type  = std::ptrdiff_t;
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

namespace std
{
template <class T>
struct pointer_traits<fancy_ptr<T>>
{
  using pointer         = fancy_ptr<T>;
  using element_type    = T;
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
struct fancy_alloc
{
  using value_type      = T;
  using pointer         = fancy_ptr<T>;
  using size_type       = std::size_t;
  using difference_type = std::ptrdiff_t;

  constexpr fancy_alloc() noexcept = default;
  template <class U>
  constexpr fancy_alloc(fancy_alloc<U> const&) noexcept
  {
  }

  pointer allocate(size_type n) { return pointer(static_cast<T*>(::operator new(n * sizeof(T)))); }
  void deallocate(pointer p, size_type) noexcept { ::operator delete(std::to_address(p)); }
};

/// counting allocator (raw pointer) to observe construct/destroy calls
template <class T>
struct counting_alloc : std::allocator<T>
{
  static inline int construct_count = 0;
  static inline int destroy_count   = 0;
  static void reset() { construct_count = 0; destroy_count = 0; }

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

/// fancy allocator with custom construct/destroy counters.
/// Presence of these members forces the non-memcpy path.
template <class T>
struct fancy_counting_alloc
{
  using value_type      = T;
  using pointer         = fancy_ptr<T>;
  using size_type       = std::size_t;
  using difference_type = std::ptrdiff_t;

  static inline int construct_count = 0;
  static inline int destroy_count   = 0;
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

// ------------------------------------------------------------------
// overload-1 (no allocator): trivial memcpy path
// ------------------------------------------------------------------
static void test_plain_trivial_exact()
{
  SECTION("plain: trivial exact");
  int src[4]     = { 1, 2, 3, 4 };
  alignas(int) unsigned char buf[4 * sizeof(int)];
  int* dst = reinterpret_cast<int*>(buf);

  auto r = stdex::uninitialized_move(src, src + 4, dst, dst + 4);
  CHECK((r.in == src + 4));
  CHECK((r.out == dst + 4));
  CHECK(dst[0] == 1 && dst[1] == 2 && dst[2] == 3 && dst[3] == 4);
  std::destroy(dst, dst + 4);
}

static void test_plain_truncate_src_longer()
{
  SECTION("plain: src longer than dst");
  int src[5]     = { 1, 2, 3, 4, 5 };
  alignas(int) unsigned char buf[2 * sizeof(int)];
  int* dst = reinterpret_cast<int*>(buf);

  auto r = stdex::uninitialized_move(src, src + 5, dst, dst + 2);
  CHECK((r.in == src + 2));
  CHECK((r.out == dst + 2));
  CHECK(dst[0] == 1 && dst[1] == 2);
  std::destroy(dst, dst + 2);
}

static void test_plain_truncate_dst_longer()
{
  SECTION("plain: dst longer than src");
  int src[2]     = { 7, 8 };
  alignas(int) unsigned char buf[5 * sizeof(int)];
  int* dst = reinterpret_cast<int*>(buf);

  auto r = stdex::uninitialized_move(src, src + 2, dst, dst + 5);
  CHECK((r.in == src + 2));
  CHECK((r.out == dst + 2));
  CHECK(dst[0] == 7 && dst[1] == 8);
  std::destroy(dst, dst + 2);
}

static void test_plain_empty()
{
  SECTION("plain: empty ranges");
  int* src = nullptr;
  alignas(int) unsigned char buf[sizeof(int)];
  int* dst = reinterpret_cast<int*>(buf);
  auto r1  = stdex::uninitialized_move(src, src, dst, dst);
  CHECK((r1.in == src && r1.out == dst));
  int one  = 42;
  auto r2  = stdex::uninitialized_move(&one, &one, dst, dst); // dst empty
  CHECK((r2.in == &one && r2.out == dst));
}

static void test_plain_unreachable_sentinel()
{
  SECTION("plain: unreachable_sentinel one side");
  int src[3]     = { 1, 2, 3 };
  alignas(int) unsigned char buf[3 * sizeof(int)];
  int* dst = reinterpret_cast<int*>(buf);

  // sized src, unbounded dst
  auto r1 = stdex::uninitialized_move(src, src + 3, dst, std::unreachable_sentinel);
  CHECK((r1.in == src + 3));
  CHECK((r1.out == dst + 3));
  CHECK(dst[0] == 1 && dst[2] == 3);
  std::destroy(dst, dst + 3);

  // unbounded src, sized dst
  alignas(int) unsigned char buf2[2 * sizeof(int)];
  int* dst2 = reinterpret_cast<int*>(buf2);
  auto r2   = stdex::uninitialized_move(src, std::unreachable_sentinel, dst2, dst2 + 2);
  CHECK((r2.out == dst2 + 2));
  CHECK(dst2[0] == 1 && dst2[1] == 2);
  std::destroy(dst2, dst2 + 2);
}

static void test_plain_move_iterator_src()
{
  SECTION("plain: move_iterator source (memcpy unwrap)");
  int src[3]     = { 4, 5, 6 };
  alignas(int) unsigned char buf[3 * sizeof(int)];
  int* dst = reinterpret_cast<int*>(buf);

  std::move_iterator<int*> mfirst(src), mlast(src + 3);
  auto r = stdex::uninitialized_move(mfirst, mlast, dst, dst + 3);
  CHECK(dst[0] == 4 && dst[1] == 5 && dst[2] == 6);
  CHECK((r.out == dst + 3));
  std::destroy(dst, dst + 3);
}

static void test_plain_nontrivial_move()
{
  SECTION("plain: non-trivial move semantics");
  move_tracker::reset();
  {
    move_tracker src[3] { move_tracker(10), move_tracker(20), move_tracker(30) };
    move_tracker::reset_counts(); // drop construction counts, keep live == 3
    CHECK(move_tracker::live == 3);

    alignas(move_tracker) unsigned char buf[3 * sizeof(move_tracker)];
    auto* dst = reinterpret_cast<move_tracker*>(buf);

    auto r = stdex::uninitialized_move(src, src + 3, dst, dst + 3);
    CHECK((r.in == src + 3));
    CHECK((r.out == dst + 3));
    CHECK(move_tracker::moves == 3);
    CHECK(dst[0].value == 10 && dst[1].value == 20 && dst[2].value == 30);
    CHECK(src[0].moved_from && src[1].moved_from && src[2].moved_from);
    CHECK(src[0].value == -1);
    std::destroy(dst, dst + 3);
    CHECK(move_tracker::live == 3); // only src remains
  }
  CHECK(move_tracker::live == 0);
}

static void test_plain_no_bitcopy_fallback()
{
  SECTION("plain: size-mismatched trivials take loop path");
  // char(1B) -> int(4B): not bitcopy-compatible, must still move correctly
  char src[3]    = { 1, 2, 3 };
  alignas(int) unsigned char buf[3 * sizeof(int)];
  int* dst = reinterpret_cast<int*>(buf);
  auto r   = stdex::uninitialized_move(src, src + 3, dst, dst + 3);
  CHECK((r.in == src + 3 && r.out == dst + 3));
  CHECK(dst[0] == 1 && dst[1] == 2 && dst[2] == 3);
  std::destroy(dst, dst + 3);
}

static void test_plain_throw_backout()
{
  SECTION("plain: throwing move destroys prefix (backout)");
  thrower::reset();
  {
    thrower src[4] { thrower(1), thrower(2), thrower(3), thrower(4) };
    thrower::reset_counts();
    thrower::throw_after = 2; // third move throws
    CHECK(thrower::alive == 4);

    alignas(thrower) unsigned char buf[4 * sizeof(thrower)];
    auto* dst = reinterpret_cast<thrower*>(buf);

    bool threw = false;
    try {
      (void) stdex::uninitialized_move(src, src + 4, dst, dst + 4);
    } catch (std::runtime_error const&) {
      threw = true;
    }
    CHECK(threw);
    // backout destroyed the 2 successfully moved dest objects
    CHECK(thrower::alive == 4); // only src survivors
    CHECK(thrower::destroyed == 2);
  }
  CHECK(thrower::alive == 0);
}

static void test_plain_constexpr()
{
  SECTION("plain: constexpr evaluation (element-wise path)");
  constexpr auto run = [] {
    int src[3] = { 1, 2, 3 };
    int dst[3] = {};
    auto r     = stdex::uninitialized_move(src, src + 3, dst, dst + 3);
    return r.in == src + 3 && r.out == dst + 3 && dst[0] == 1 && dst[2] == 3;
  };
  static_assert(run());
  CHECK(run());
}

// ------------------------------------------------------------------
// overload-2 (allocator, raw pointer)
// ------------------------------------------------------------------
static void test_alloc_raw_trivial()
{
  SECTION("alloc/raw: trivial exact + truncation");
  std::allocator<int> al;
  int src[4] = { 1, 2, 3, 4 };
  int* dst   = al.allocate(4);
  auto r     = stdex::uninitialized_move(src, src + 4, dst, dst + 4, al);
  CHECK((r.in == src + 4 && r.out == dst + 4));
  CHECK(dst[0] == 1 && dst[3] == 4);
  std::destroy(dst, dst + 4);
  al.deallocate(dst, 4);

  // src longer
  int* dst2 = al.allocate(2);
  auto r2   = stdex::uninitialized_move(src, src + 4, dst2, dst2 + 2, al);
  CHECK((r2.in == src + 2 && r2.out == dst2 + 2));
  CHECK(dst2[0] == 1 && dst2[1] == 2);
  std::destroy(dst2, dst2 + 2);
  al.deallocate(dst2, 2);

  // dst longer
  int* dst3 = al.allocate(5);
  auto r3   = stdex::uninitialized_move(src, src + 2, dst3, dst3 + 5, al);
  CHECK((r3.in == src + 2 && r3.out == dst3 + 2));
  std::destroy(dst3, dst3 + 2);
  al.deallocate(dst3, 5);
}

static void test_alloc_raw_nontrivial_counting()
{
  SECTION("alloc/raw: non-trivial via counting allocator");
  counting_alloc<move_tracker>::reset();
  move_tracker::reset();
  {
    counting_alloc<move_tracker> al;
    move_tracker src[2] { move_tracker(11), move_tracker(22) };
    move_tracker::reset_counts();
    counting_alloc<move_tracker>::reset();

    move_tracker* dst = al.allocate(2);
    auto r = stdex::uninitialized_move(src, src + 2, dst, dst + 2, al);
    CHECK((r.in == src + 2 && r.out == dst + 2));
    CHECK(counting_alloc<move_tracker>::construct_count == 2);
    CHECK(dst[0].value == 11 && dst[1].value == 22);
    CHECK(src[0].moved_from);
    std::destroy(dst, dst + 2);
    al.deallocate(dst, 2);
  }
  CHECK(move_tracker::live == 0);
}

static void test_alloc_raw_custom_construct_bypasses_memcpy()
{
  SECTION("alloc/raw: custom construct forces loop for trivials");
  counting_alloc<int>::reset();
  counting_alloc<int> al;
  int src[3]  = { 5, 6, 7 };
  int* dst    = al.allocate(3);
  auto r      = stdex::uninitialized_move(src, src + 3, dst, dst + 3, al);
  CHECK((r.in == src + 3 && r.out == dst + 3));
  // custom construct present -> can_memcpy false -> exactly 3 constructs
  CHECK(counting_alloc<int>::construct_count == 3);
  CHECK(dst[0] == 5 && dst[2] == 7);
  std::destroy(dst, dst + 3);
  al.deallocate(dst, 3);
}

static void test_alloc_raw_throw_backout()
{
  SECTION("alloc/raw: throwing move calls allocator destroy on prefix");
  counting_alloc<thrower>::reset();
  thrower::reset();
  {
    counting_alloc<thrower> al;
    thrower src[4] { thrower(1), thrower(2), thrower(3), thrower(4) };
    thrower::reset_counts();
    counting_alloc<thrower>::reset();
    thrower::throw_after = 1; // second move throws

    thrower* dst = al.allocate(4);
    bool threw   = false;
    try {
      (void) stdex::uninitialized_move(src, src + 4, dst, dst + 4, al);
    } catch (std::runtime_error const&) {
      threw = true;
    }
    CHECK(threw);
    // 1 element constructed then destroyed via allocator_traits::destroy
    CHECK(counting_alloc<thrower>::construct_count == 2); // 2 attempts, 2nd threw mid-construct?
    CHECK(counting_alloc<thrower>::destroy_count == 1);   // backout destroys prefix of 1
    CHECK(thrower::alive == 4);
    al.deallocate(dst, 4);
  }
  CHECK(thrower::alive == 0);
}

static void test_alloc_raw_unreachable()
{
  SECTION("alloc/raw: unreachable sentinel one side");
  std::allocator<int> al;
  int src[3] = { 1, 2, 3 };
  int* dst   = al.allocate(3);
  auto r1    = stdex::uninitialized_move(src, src + 3, dst, std::unreachable_sentinel, al);
  CHECK((r1.in == src + 3 && r1.out == dst + 3));
  CHECK(dst[2] == 3);
  std::destroy(dst, dst + 3);
  al.deallocate(dst, 3);
}

// ------------------------------------------------------------------
// overload-2 (allocator, fancy pointer) -- the required fancy coverage
// ------------------------------------------------------------------
static void test_alloc_fancy_trivial_memcpy()
{
  SECTION("alloc/fancy: trivial memcpy path");
  fancy_alloc<int> al;
  int src[4] = { 10, 20, 30, 40 };
  fancy_ptr<int> dst  = al.allocate(4);
  fancy_ptr<int> dend = dst + 4;
  auto r = stdex::uninitialized_move(src, src + 4, dst, dend, al);
  CHECK((r.in == src + 4));
  CHECK((r.out == dend));
  CHECK(dst[0] == 10 && dst[1] == 20 && dst[2] == 30 && dst[3] == 40);
  std::destroy(std::to_address(dst), std::to_address(dend));
  al.deallocate(dst, 4);
}

static void test_alloc_fancy_truncate_both()
{
  SECTION("alloc/fancy: truncation both directions + empty");
  fancy_alloc<int> al;
  int src[5] = { 1, 2, 3, 4, 5 };

  fancy_ptr<int> d1 = al.allocate(2);
  auto r1 = stdex::uninitialized_move(src, src + 5, d1, d1 + 2, al);
  CHECK((r1.in == src + 2 && r1.out == d1 + 2));
  CHECK(d1[0] == 1 && d1[1] == 2);
  std::destroy(std::to_address(d1), std::to_address(d1 + 2));
  al.deallocate(d1, 2);

  fancy_ptr<int> d2 = al.allocate(5);
  auto r2 = stdex::uninitialized_move(src, src + 2, d2, d2 + 5, al);
  CHECK((r2.in == src + 2 && r2.out == d2 + 2));
  std::destroy(std::to_address(d2), std::to_address(d2 + 2));
  al.deallocate(d2, 5);

  fancy_ptr<int> d3 = al.allocate(1);
  auto r3 = stdex::uninitialized_move(src, src, d3, d3 + 1, al);
  CHECK((r3.in == src && r3.out == d3));
  al.deallocate(d3, 1);
}

static void test_alloc_fancy_unreachable()
{
  SECTION("alloc/fancy: unreachable sentinel");
  fancy_alloc<int> al;
  int src[3] = { 1, 2, 3 };
  fancy_ptr<int> dst = al.allocate(3);
  auto r = stdex::uninitialized_move(src, src + 3, dst, std::unreachable_sentinel, al);
  CHECK((r.in == src + 3 && r.out == dst + 3));
  CHECK(dst[0] == 1 && dst[2] == 3);
  std::destroy(std::to_address(dst), std::to_address(dst + 3));
  al.deallocate(dst, 3);

  fancy_ptr<int> dst2 = al.allocate(2);
  auto r2 = stdex::uninitialized_move(src, std::unreachable_sentinel, dst2, dst2 + 2, al);
  CHECK((r2.out == dst2 + 2));
  CHECK(dst2[0] == 1 && dst2[1] == 2);
  std::destroy(std::to_address(dst2), std::to_address(dst2 + 2));
  al.deallocate(dst2, 2);
}

static void test_alloc_fancy_nontrivial()
{
  SECTION("alloc/fancy: non-trivial move via fancy");
  fancy_alloc<move_tracker> al;
  move_tracker::reset();
  {
    move_tracker src[3] { move_tracker(1), move_tracker(2), move_tracker(3) };
    move_tracker::reset_counts();
    fancy_ptr<move_tracker> dst = al.allocate(3);
    auto r = stdex::uninitialized_move(src, src + 3, dst, dst + 3, al);
    CHECK((r.in == src + 3 && r.out == dst + 3));
    CHECK(move_tracker::moves == 3);
    CHECK(dst[0].value == 1 && dst[2].value == 3);
    CHECK(src[0].moved_from);
    std::destroy(std::to_address(dst), std::to_address(dst + 3));
    al.deallocate(dst, 3);
  }
  CHECK(move_tracker::live == 0);
}

static void test_alloc_fancy_custom_construct_no_memcpy()
{
  SECTION("alloc/fancy: custom construct disables memcpy");
  fancy_counting_alloc<int>::reset();
  fancy_counting_alloc<int> al;
  int src[3] = { 9, 8, 7 };
  fancy_ptr<int> dst = al.allocate(3);
  auto r = stdex::uninitialized_move(src, src + 3, dst, dst + 3, al);
  CHECK((r.in == src + 3 && r.out == dst + 3));
  CHECK(fancy_counting_alloc<int>::construct_count == 3);
  CHECK(dst[0] == 9 && dst[2] == 7);
  std::destroy(std::to_address(dst), std::to_address(dst + 3));
  al.deallocate(dst, 3);
}

static void test_alloc_fancy_throw_custom_destroy()
{
  SECTION("alloc/fancy: throw -> custom destroy on prefix (incl. trivial)");
  // trivially destructible int BUT allocator has custom destroy:
  // backout must still call destroy. Use thrower to trigger the path.
  fancy_counting_alloc<thrower>::reset();
  thrower::reset();
  {
    fancy_counting_alloc<thrower> al;
    thrower src[3] { thrower(1), thrower(2), thrower(3) };
    thrower::reset_counts();
    fancy_counting_alloc<thrower>::reset();
    thrower::throw_after = 1;

    fancy_ptr<thrower> dst = al.allocate(3);
    bool threw = false;
    try {
      (void) stdex::uninitialized_move(src, src + 3, dst, dst + 3, al);
    } catch (std::runtime_error const&) {
      threw = true;
    }
    CHECK(threw);
    CHECK(fancy_counting_alloc<thrower>::destroy_count == 1);
    CHECK(thrower::alive == 3);
    al.deallocate(dst, 3);
  }
  CHECK(thrower::alive == 0);
}

static void test_alloc_fancy_trivial_custom_destroy_noop_success()
{
  SECTION("alloc/fancy: success path leaves nothing for backout");
  fancy_counting_alloc<int>::reset();
  fancy_counting_alloc<int> al;
  int src[2] = { 1, 2 };
  fancy_ptr<int> dst = al.allocate(2);
  auto r = stdex::uninitialized_move(src, src + 2, dst, dst + 2, al);
  CHECK((r.out == dst + 2));
  CHECK(fancy_counting_alloc<int>::construct_count == 2);
  CHECK(fancy_counting_alloc<int>::destroy_count == 0); // released, no rollback
  std::destroy(std::to_address(dst), std::to_address(dst + 2));
  al.deallocate(dst, 2);
}

int main()
{
  test_plain_trivial_exact();
  test_plain_truncate_src_longer();
  test_plain_truncate_dst_longer();
  test_plain_empty();
  test_plain_unreachable_sentinel();
  test_plain_move_iterator_src();
  test_plain_nontrivial_move();
  test_plain_no_bitcopy_fallback();
  test_plain_throw_backout();
  test_plain_constexpr();

  test_alloc_raw_trivial();
  test_alloc_raw_nontrivial_counting();
  test_alloc_raw_custom_construct_bypasses_memcpy();
  test_alloc_raw_throw_backout();
  test_alloc_raw_unreachable();

  test_alloc_fancy_trivial_memcpy();
  test_alloc_fancy_truncate_both();
  test_alloc_fancy_unreachable();
  test_alloc_fancy_nontrivial();
  test_alloc_fancy_custom_construct_no_memcpy();
  test_alloc_fancy_throw_custom_destroy();
  test_alloc_fancy_trivial_custom_destroy_noop_success();

  std::println("\npass={} fail={}", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

