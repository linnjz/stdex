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

// test_inlined_vector.cpp
//
// Covers stdex::inlined_vector (inlined_vector.hpp): API surface, element
// access, iterators, capacity, modifiers, swap, comparison, POD / non-POD,
// move-only, aliasing, exception guarantees, allocator lifecycle, constexpr,
// and differential fuzz against std::vector.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <forward_list>
#include <functional>
#include <initializer_list>
#include <memory>
#include <new>
#include <random>
#include <ranges>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "inlined_vector.hpp"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(...)                                                                        \
  do {                                                                                    \
    if (__VA_ARGS__) {                                                                    \
      ++g_pass;                                                                           \
    } else {                                                                              \
      ++g_fail;                                                                           \
      std::fprintf(stderr, "  [FAIL] %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__);     \
    }                                                                                     \
  } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))
#define SECTION(name) std::printf("\n=== %s ===\n", name)

namespace iv_test
{
// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
template <class T, std::size_t N>
using V = stdex::inlined_vector<T, N>;

template <class C>
std::vector<typename C::value_type> to_vec(C const &c)
{
  return std::vector<typename C::value_type>(c.begin(), c.end());
}

template <class C, class R>
bool equals(C const &c, R const &r)
{
  if (c.size() != r.size()) return false;
  for (std::size_t i = 0; i < r.size(); ++i) {
    if (!(c[i] == r[i])) return false;
  }
  return true;
}

// A type that is copyable but *not* three-way comparable (only `<`), to check
// the synth-three-way comparison path.
struct less_only
{
  int v;
  friend bool operator< (less_only a, less_only b) { return a.v < b.v; }
};

// A type with only `==` (no ordering): equality must still work.
struct eq_only
{
  int v;
  friend bool operator== (eq_only a, eq_only b) { return a.v == b.v; }
};

// Counts live instances and can throw on a chosen copy/move operation.
struct tracked
{
  int v;
  static inline long live      = 0;
  static inline long ops       = 0;
  static inline long throw_at  = -1;
  static void tick()
  {
    if (throw_at >= 0 && ops == throw_at) { ++ops; throw std::runtime_error("tracked"); }
    ++ops;
  }
  tracked(int x = 0) : v(x) { ++live; }
  tracked(tracked const &o) : v(o.v) { tick(); ++live; }
  tracked(tracked &&o) : v(o.v) { tick(); ++live; }
  tracked &operator= (tracked const &o) { tick(); v = o.v; return *this; }
  tracked &operator= (tracked &&o) { tick(); v = o.v; return *this; }
  ~tracked() { --live; }
  friend bool operator== (tracked const &a, tracked const &b) { return a.v == b.v; }
};

// Counting allocator for lifecycle checks.
struct alloc_state
{
  std::size_t allocations   = 0;
  std::size_t deallocations = 0;
  std::size_t attempts      = 0;
  std::size_t fail_on       = 0; // 0 = never
};

template <class T>
struct counting_alloc
{
  using value_type = T;
  using size_type  = std::size_t;
  using difference_type = std::ptrdiff_t;
  using propagate_on_container_copy_assignment = std::false_type;
  using propagate_on_container_move_assignment = std::false_type;
  using propagate_on_container_swap = std::false_type;
  using is_always_equal = std::false_type;

  template <class U>
  struct rebind { using other = counting_alloc<U>; };

  alloc_state *st_ = nullptr;
  counting_alloc() = default;
  explicit counting_alloc(alloc_state *st) noexcept : st_(st) {}
  template <class U>
  counting_alloc(counting_alloc<U> const &o) noexcept : st_(o.st_) {}

  T *allocate(std::size_t n)
  {
    ++st_->attempts;
    if (st_->fail_on != 0 && st_->attempts == st_->fail_on) throw std::bad_alloc{};
    ++st_->allocations;
    return static_cast<T *>(::operator new(n * sizeof(T)));
  }
  void deallocate(T *p, std::size_t) noexcept
  {
    ++st_->deallocations;
    ::operator delete(p);
  }
  [[nodiscard]] std::size_t max_size() const noexcept { return static_cast<std::size_t>(-1) / sizeof(T) / 2; }

  template <class U>
  friend bool operator== (counting_alloc const &, counting_alloc<U> const &) noexcept { return true; }
};

// ---------------------------------------------------------------------------
// 1. construction + type aliases (API)
// ---------------------------------------------------------------------------
void test_types_and_ctors()
{
  SECTION("types & constructors");
  using T = V<int, 4>;
  static_assert(std::is_same_v<T::value_type, int>);
  static_assert(std::is_same_v<T::size_type, std::size_t>);
  static_assert(std::is_same_v<T::difference_type, std::ptrdiff_t>);
  static_assert(std::is_same_v<T::reference, int &>);
  static_assert(std::is_same_v<T::const_reference, int const &>);
  static_assert(std::is_same_v<T::iterator, int *>);
  static_assert(std::is_same_v<T::const_iterator, int const *>);
  static_assert(std::is_same_v<T::reverse_iterator, std::reverse_iterator<int *>>);
  static_assert(std::is_same_v<T::const_reverse_iterator, std::reverse_iterator<int const *>>);
  static_assert(std::ranges::contiguous_range<T>);
  static_assert(std::ranges::random_access_range<T>);
  static_assert(std::ranges::sized_range<T>);
  static_assert(std::contiguous_iterator<T::iterator>);

  static_assert(!std::is_convertible_v<std::size_t, T>, "size ctor must be explicit");
  // push_back returns reference (inplace_vector convention; differs from std::vector's void)
  static_assert(std::is_same_v<decltype(std::declval<T &>().push_back(1)), T::reference>);
  static_assert(std::is_same_v<decltype(std::declval<T &>().emplace_back(1)), T::reference>);
  // move assignment is conditionally noexcept and true for int/std::allocator
  static_assert(std::is_nothrow_move_assignable_v<T>);
  static_assert(std::is_nothrow_move_constructible_v<T>);
  // member swap exists
  static_assert(requires(T &a, T &b) { a.swap(b); });

  { T v; CHECK(v.empty()); CHECK(v.size() == 0); CHECK(v.capacity() == 4); }
  { T v(3); CHECK_EQ(v.size(), 3u); for (int i = 0; i < 3; ++i) CHECK(v[std::size_t(i)] == 0); }
  { T v(5, 7); CHECK_EQ(v.size(), 5u); for (int i = 0; i < 5; ++i) CHECK(v[std::size_t(i)] == 7); }
  {
    int a[] = {1, 2, 3};
    T v(a, a + 3);
    CHECK(equals(v, std::vector<int>{1, 2, 3}));
  }
  { T v{4, 5, 6}; CHECK(equals(v, std::vector<int>{4, 5, 6})); }
  {
    std::allocator<int> al;
    T v(al);
    CHECK(v.empty());
  }
#ifdef __cpp_lib_containers_ranges
  {
    std::vector<int> src{7, 8, 9};
    T v(std::from_range, src);
    CHECK(equals(v, src));
  }
#endif
}

// ---------------------------------------------------------------------------
// 2. stack / promotion / heap (N boundary)
// ---------------------------------------------------------------------------
void test_storage_modes()
{
  SECTION("storage modes & boundary");
  using S = V<int, 4>;
  S v;
  CHECK(v.is_stack());
  for (int i = 0; i < 4; ++i) v.push_back(i);
  CHECK(v.is_stack());
  CHECK_EQ(v.size(), 4u);
  v.push_back(4); // boundary N -> heap
  CHECK(v.is_heap());
  CHECK_EQ(v.size(), 5u);
  CHECK(v[4] == 4);

  using H = V<int, 0>;
  H h;
  CHECK(h.is_heap());
  CHECK_EQ(h.capacity(), 0u);
  for (int i = 0; i < 100; ++i) h.push_back(i);
  CHECK_EQ(h.size(), 100u);
  for (int i = 0; i < 100; ++i) CHECK(h[std::size_t(i)] == i);
}

// ---------------------------------------------------------------------------
// 3. copy / move / assign / self
// ---------------------------------------------------------------------------
void test_copy_move()
{
  SECTION("copy / move / assign");
  using S = V<int, 4>;
  S a{1, 2, 3};
  S b = a;
  CHECK(equals(b, std::vector<int>{1, 2, 3}));
  S c = std::move(b);
  CHECK(equals(c, std::vector<int>{1, 2, 3}));
  S d;
  d = a;
  CHECK(equals(d, std::vector<int>{1, 2, 3}));
  S e;
  e = std::move(c);
  CHECK(equals(e, std::vector<int>{1, 2, 3}));
  // self copy/move
  S &alias = e;
  e = alias;
  CHECK(equals(e, std::vector<int>{1, 2, 3}));
  e = std::move(alias);
  CHECK(equals(e, std::vector<int>{1, 2, 3}));
  // initializer-list assign
  e = {9, 8};
  CHECK(equals(e, std::vector<int>{9, 8}));
  // copy/move with allocator
  std::allocator<int> al;
  S f(a, al);
  CHECK(equals(f, std::vector<int>{1, 2, 3}));
  S g(std::move(f), al);
  CHECK(equals(g, std::vector<int>{1, 2, 3}));
}

// ---------------------------------------------------------------------------
// 4. element access
// ---------------------------------------------------------------------------
void test_element_access()
{
  SECTION("element access");
  V<int, 4> v{10, 20, 30};
  CHECK(v.front() == 10);
  CHECK(v.back() == 30);
  CHECK(v[1] == 20);
  CHECK(v.at(2) == 30);
  CHECK(v.data()[0] == 10);
  bool threw = false;
  try { (void)v.at(3); } catch (std::out_of_range const &) { threw = true; }
  CHECK(threw);
  V<int, 4> const &cv = v;
  CHECK(cv.front() == 10);
  CHECK(cv.back() == 30);
  CHECK(cv[0] == 10);
  CHECK(cv.data()[2] == 30);
}

// ---------------------------------------------------------------------------
// 5. iterators
// ---------------------------------------------------------------------------
void test_iterators()
{
  SECTION("iterators");
  V<int, 8> v{5, 3, 1, 4, 2};
  CHECK_EQ(static_cast<std::size_t>(v.end() - v.begin()), v.size());
  std::sort(v.begin(), v.end());
  CHECK(equals(v, std::vector<int>{1, 2, 3, 4, 5}));
  // reverse
  std::vector<int> rev(v.rbegin(), v.rend());
  CHECK(equals(V<int, 8>(rev.begin(), rev.end()), std::vector<int>{5, 4, 3, 2, 1}));
  // const iterators + range-for
  V<int, 8> const &cv = v;
  int sum = 0;
  for (int x : cv) sum += x;
  CHECK(sum == 15);
  CHECK_EQ(static_cast<std::size_t>(cv.cend() - cv.cbegin()), cv.size());
}

// ---------------------------------------------------------------------------
// 6. capacity
// ---------------------------------------------------------------------------
void test_capacity()
{
  SECTION("capacity / reserve / shrink / resize");
  V<int, 4> v;
  CHECK_EQ(v.capacity(), 4u);
  v.reserve(100);
  CHECK(v.capacity() >= 100u);
  std::size_t const cap = v.capacity();
  v.reserve(10); // must not shrink
  CHECK(v.capacity() == cap);
  v.push_back(1);
  v.shrink_to_fit();
  CHECK(v.capacity() >= v.size());
  CHECK_EQ(v.size(), 1u);
  // resize grow/shrink
  v.resize(10, 7);
  CHECK_EQ(v.size(), 10u);
  for (std::size_t i = 1; i < 10; ++i) CHECK(v[i] == 7);
  v.resize(3);
  CHECK_EQ(v.size(), 3u);
  CHECK(v[0] == 1);
  v.resize(0);
  CHECK(v.empty());
  CHECK(v.max_size() > 1000u);
}

// ---------------------------------------------------------------------------
// 7. modifiers
// ---------------------------------------------------------------------------
void test_modifiers()
{
  SECTION("modifiers");
  V<int, 8> v;
  v.push_back(1);
  v.emplace_back(2);
  v.push_back(3);
  CHECK(equals(v, std::vector<int>{1, 2, 3}));
  v.pop_back();
  CHECK(equals(v, std::vector<int>{1, 2}));
  auto it = v.emplace(v.begin(), 0);
  CHECK_EQ(it - v.begin(), 0);
  CHECK(equals(v, std::vector<int>{0, 1, 2}));
  it = v.insert(v.begin() + 1, 9);
  CHECK_EQ(it - v.begin(), 1);
  CHECK(equals(v, std::vector<int>{0, 9, 1, 2}));
  v.insert(v.end(), std::size_t{2}, 5);
  CHECK(equals(v, std::vector<int>{0, 9, 1, 2, 5, 5}));
  it = v.insert(v.begin() + 2, {7, 7});
  CHECK_EQ(it - v.begin(), 2);
  CHECK(equals(v, std::vector<int>{0, 9, 7, 7, 1, 2, 5, 5}));
  // insert from external range
  std::vector<int> ins{100, 200};
  v.insert(v.begin(), ins.begin(), ins.end());
  CHECK(equals(v, std::vector<int>{100, 200, 0, 9, 7, 7, 1, 2, 5, 5}));
  // erase
  v.erase(v.begin());
  CHECK_EQ(v.front(), 200);
  v.erase(v.begin(), v.begin() + 3);
  CHECK(equals(v, std::vector<int>{7, 7, 1, 2, 5, 5}));
  // assign variants
  v.assign(std::size_t{3}, 4);
  CHECK(equals(v, std::vector<int>{4, 4, 4}));
  v.assign({1, 2, 3, 4, 5, 6});
  CHECK(equals(v, std::vector<int>{1, 2, 3, 4, 5, 6}));
  std::vector<int> src{8, 9};
  v.assign(src.begin(), src.end());
  CHECK(equals(v, src));
  v.append({10, 11});
  CHECK(equals(v, std::vector<int>{8, 9, 10, 11}));
  v.clear();
  CHECK(v.empty());
  // erase empty range / insert count 0
  v = {1, 2, 3};
  auto e2 = v.erase(v.begin() + 1, v.begin() + 1);
  CHECK_EQ(e2 - v.begin(), 1);
  auto i0 = v.insert(v.begin() + 1, std::size_t{0}, 9);
  CHECK_EQ(i0 - v.begin(), 1);
  CHECK_EQ(v.size(), 3u);
}

// ---------------------------------------------------------------------------
// 8. swap (member + free + self + stack/heap mix)
// ---------------------------------------------------------------------------
void test_swap()
{
  SECTION("swap");
  {
    V<int, 4> a{1, 2, 3};
    V<int, 4> b;
    for (int i = 0; i < 10; ++i) b.push_back(i + 10);
    a.swap(b); // member swap
    CHECK(equals(a, std::vector<int>({10, 11, 12, 13, 14, 15, 16, 17, 18, 19})));
    CHECK(equals(b, std::vector<int>{1, 2, 3}));
    swap(a, b); // ADL friend
    CHECK(equals(a, std::vector<int>{1, 2, 3}));
    a.swap(a); // self
    CHECK(equals(a, std::vector<int>{1, 2, 3}));
  }
  {
    V<int, 0> a{1, 2};
    V<int, 0> b{3, 4, 5, 6};
    a.swap(b);
    CHECK(equals(a, std::vector<int>{3, 4, 5, 6}));
    CHECK(equals(b, std::vector<int>{1, 2}));
  }
  static_assert(std::is_nothrow_swappable_v<V<int, 4>>);
}

// ---------------------------------------------------------------------------
// 9. comparison (three-way + synth-three-way)
// ---------------------------------------------------------------------------
void test_comparison()
{
  SECTION("comparison");
  V<int, 4> a{1, 2, 3}, b{1, 2, 4};
  CHECK(a == a);
  CHECK(a != b);
  CHECK(a < b);
  CHECK(a <= b);
  CHECK(b > a);
  CHECK(b >= a);
  CHECK((a <=> b) < 0);

  // only-`<` type must still order (synth-three-way)
  V<less_only, 4> la, lb;
  la.push_back(less_only{1});
  lb.push_back(less_only{2});
  CHECK(la < lb);
  CHECK(!(lb < la));
  CHECK(la <= lb);

  // only-`==` type must still compare equal
  V<eq_only, 4> ea, eb;
  ea.push_back(eq_only{5});
  eb.push_back(eq_only{5});
  CHECK(ea == eb);

  // strings (three-way comparable)
  V<std::string, 4> sa{std::string("a")}, sb{std::string("b")};
  CHECK(sa < sb);
}

// ---------------------------------------------------------------------------
// 10. non-trivial + move-only
// ---------------------------------------------------------------------------
void test_non_trivial()
{
  SECTION("non-trivial (std::string)");
  using S = V<std::string, 4>;
  S v;
  v.emplace_back("alpha");
  v.push_back(std::string("beta"));
  v.insert(v.begin(), "zero");
  CHECK_EQ(v.size(), 3u);
  CHECK(v[0] == "zero");
  CHECK(v[1] == "alpha");
  CHECK(v[2] == "beta");
  S copy = v;
  CHECK(copy == v);
  v.erase(v.begin());
  CHECK(v[0] == "alpha");
  v.resize(5, std::string("x"));
  CHECK_EQ(v.size(), 5u);
  CHECK(v[4] == "x");
  v.shrink_to_fit();
  CHECK(v[0] == "alpha");
}

void test_move_only()
{
  SECTION("move-only (unique_ptr)");
  using P = V<std::unique_ptr<int>, 4>;
  P v;
  for (int i = 0; i < 3; ++i) v.push_back(std::make_unique<int>(i));
  v.insert(v.begin(), std::make_unique<int>(9));
  (void)v.emplace(v.begin() + 1, new int(8));
  v.erase(v.begin() + 2);
  CHECK_EQ(v.size(), 4u);
  int sum = 0;
  for (auto &p : v) sum += *p;
  CHECK(sum > 0);
}

// ---------------------------------------------------------------------------
// 11. aliasing (value reference into the container)
// ---------------------------------------------------------------------------
void test_aliasing()
{
  SECTION("aliasing");
  {
    V<int, 0> v;
    for (int i = 0; i < 3; ++i) v.push_back(i);
    v.push_back(v.front());            // push_back(front())
    v.insert(v.begin(), v.back());     // insert(begin, back())
    v.insert(v.end(), v.front());
    CHECK(equals(v, std::vector<int>{0, 0, 1, 2, 0, 0}));
    v.assign(std::size_t{4}, v[2]);    // assign from element (grown)
    CHECK(equals(v, std::vector<int>{1, 1, 1, 1}));
    for (int i = 0; i < 100; ++i) v.emplace(v.begin(), v.back());
    CHECK_EQ(v.size(), 104u);
    CHECK(v[0] == 1 && v[103] == 1);
  }
  {
    V<std::string, 2> v;
    v.push_back("a");
    v.push_back("b");
    v.insert(v.begin(), v.back());
    CHECK(v[0] == "b" && v[2] == "b");
    v.assign(std::size_t{3}, v.front());
    CHECK_EQ(v.size(), 3u);
    for (auto &s : v) CHECK(s == "b");
  }
}

// ---------------------------------------------------------------------------
// 12. exception guarantees (strong where required) + no leaks
// ---------------------------------------------------------------------------
void test_exception_safety()
{
  SECTION("exception safety");
  using Vt = V<tracked, 2>;
  auto content = [](Vt const &v, std::initializer_list<int> il) {
    if (v.size() != il.size()) return false;
    std::size_t i = 0;
    for (int x : il) if (!(v[i++].v == x)) return false;
    return true;
  };

  // push_back reallocation: throw while copying the appended element -> strong.
  {
    Vt v; v.push_back(tracked(1)); v.push_back(tracked(2));
    tracked::live = 2; tracked::ops = 0; tracked::throw_at = 0;
    bool threw = false;
    try { v.push_back(tracked(3)); } catch (...) { threw = true; }
    tracked::throw_at = -1;
    CHECK(threw);
    CHECK(content(v, {1, 2}));
    CHECK_EQ(v.size(), 2u);
    CHECK(tracked::live == 2);
  }
  // insert(pos, value): throw copying value into tmp -> unchanged.
  {
    Vt v; v.push_back(tracked(1)); v.push_back(tracked(2));
    tracked::live = 2; tracked::ops = 0; tracked::throw_at = 0;
    bool threw = false;
    try { (void)v.insert(v.cbegin(), v[1]); } catch (...) { threw = true; }
    tracked::throw_at = -1;
    CHECK(threw);
    CHECK(content(v, {1, 2}));
    CHECK(tracked::live == 2);
  }
  // insert(pos, count, value): throw during bulk fill -> strong.
  {
    Vt v; v.push_back(tracked(1)); v.push_back(tracked(2));
    tracked::live = 2; tracked::ops = 0; tracked::throw_at = 1;
    bool threw = false;
    try { (void)v.insert(v.cbegin(), std::size_t{4}, tracked(9)); } catch (...) { threw = true; }
    tracked::throw_at = -1;
    CHECK(threw);
    CHECK(content(v, {1, 2}));
    CHECK(tracked::live == 2);
  }
  // resize grow past capacity: throw during fill -> backout, old intact.
  {
    Vt v; v.push_back(tracked(1)); v.push_back(tracked(2));
    tracked::live = 2; tracked::ops = 0; tracked::throw_at = 1;
    bool threw = false;
    try { v.resize(6, tracked(9)); } catch (...) { threw = true; }
    tracked::throw_at = -1;
    CHECK(threw);
    CHECK(content(v, {1, 2}));
    CHECK(tracked::live == 2);
  }
  // assign: basic guarantee -- valid state, no leak.
  {
    Vt v; v.push_back(tracked(1)); v.push_back(tracked(2));
    tracked::live = 2; tracked::ops = 0; tracked::throw_at = 1;
    bool threw = false;
    try { v.assign(std::size_t{5}, tracked(7)); } catch (...) { threw = true; }
    tracked::throw_at = -1;
    CHECK(threw);
    CHECK(tracked::live == static_cast<long>(v.size()));
    CHECK(v.size() <= 5u);
  }
  // insert(range): basic guarantee -- no leak.
  {
    Vt v; v.push_back(tracked(1)); v.push_back(tracked(2));
    std::vector<tracked> src; src.push_back(tracked(8)); src.push_back(tracked(9)); src.push_back(tracked(10));
    tracked::live = 5; tracked::ops = 0; tracked::throw_at = 1;
    bool threw = false;
    try { (void)v.insert(v.cbegin() + 1, src.begin(), src.end()); } catch (...) { threw = true; }
    tracked::throw_at = -1;
    CHECK(threw);
    CHECK(tracked::live == 3 + static_cast<long>(v.size()));
    src.clear(); v.clear();
    CHECK(tracked::live == 0);
  }
}

// ---------------------------------------------------------------------------
// 13. allocator lifecycle / bad_alloc
// ---------------------------------------------------------------------------
void test_allocator_lifecycle()
{
  SECTION("allocator lifecycle");
  alloc_state st;
  {
    using A = stdex::inlined_vector<int, 2, counting_alloc<int>>;
    A v{counting_alloc<int>(&st)};
    for (int i = 0; i < 50; ++i) v.push_back(i);
    CHECK(v.is_heap());
    A w = std::move(v); // steal
    CHECK_EQ(w.size(), 50u);
    A u(w); // copy (allocates)
    CHECK_EQ(u.size(), 50u);
  }
  CHECK_EQ(st.allocations, st.deallocations);

  // bad_alloc propagates; object remains valid & empty.
  alloc_state st2;
  st2.fail_on = 1;
  using A = stdex::inlined_vector<int, 0, counting_alloc<int>>;
  A v{counting_alloc<int>(&st2)};
  bool threw = false;
  try {
    for (int i = 0; i < 10; ++i) v.push_back(i);
  } catch (std::bad_alloc const &) { threw = true; }
  CHECK(threw);
  CHECK(v.empty());
  CHECK_EQ(st2.allocations, st2.deallocations);
}

// ---------------------------------------------------------------------------
// 14. constexpr (heap always; inline guarded by start_lifetime)
// ---------------------------------------------------------------------------
#if defined(__cpp_lib_constexpr_dynamic_alloc) && __cpp_lib_constexpr_dynamic_alloc >= 201907L
constexpr bool constexpr_heap_ops()
{
  stdex::inlined_vector<int, 0> v;
  v.push_back(1);
  v.push_back(2);
  v.insert(v.begin() + 1, 9);
  v.erase(v.begin());
  v.push_back(3);
  return v.size() == 3 && v[0] == 9 && v[1] == 2 && v[2] == 3;
}
static_assert(constexpr_heap_ops());
#endif

#if defined(__cpp_lib_start_lifetime) && __cpp_lib_start_lifetime >= 202603L
constexpr bool constexpr_stack_ops()
{
  stdex::inlined_vector<int, 4> v;
  v.push_back(1);
  v.push_back(2);
  v.insert(v.begin(), 0);
  return v.size() == 3 && v[0] == 0 && v[2] == 2;
}
static_assert(constexpr_stack_ops());
#endif

void test_constexpr()
{
  SECTION("constexpr");
#if defined(__cpp_lib_constexpr_dynamic_alloc) && __cpp_lib_constexpr_dynamic_alloc >= 201907L
  CHECK(constexpr_heap_ops());
#else
  CHECK(true);
#endif
#if defined(__cpp_lib_start_lifetime) && __cpp_lib_start_lifetime >= 202603L
  CHECK(constexpr_stack_ops());
#else
  CHECK(true);
#endif
}

// ---------------------------------------------------------------------------
// 15. differential fuzz vs std::vector (POD int + non-POD string)
// ---------------------------------------------------------------------------
template <class T, std::size_t N>
void fuzz_one(unsigned seed, int ops)
{
  using C = V<T, N>;
  std::mt19937 rng(seed);
  C c;
  std::vector<T> ref;
  auto mk = [&]() -> T {
    if constexpr (std::is_same_v<T, std::string>) return std::string(1 + rng() % 4, char('a' + rng() % 26));
    else return static_cast<T>(rng() % 1000);
  };
  for (int i = 0; i < ops; ++i) {
    int op = static_cast<int>(rng() % 100u);
    if (op < 24) { T x = mk(); c.push_back(x); ref.push_back(x); }
    else if (op < 30 && !ref.empty()) { c.pop_back(); ref.pop_back(); }
    else if (op < 42) {
      std::size_t idx = ref.empty() ? 0 : rng() % (ref.size() + 1);
      T x = mk();
      (void)c.insert(c.cbegin() + static_cast<std::ptrdiff_t>(idx), x);
      ref.insert(ref.begin() + static_cast<std::ptrdiff_t>(idx), x);
    } else if (op < 50) {
      std::size_t idx = ref.empty() ? 0 : rng() % (ref.size() + 1);
      std::size_t n = rng() % 5;
      T x = mk();
      (void)c.insert(c.cbegin() + static_cast<std::ptrdiff_t>(idx), n, x);
      ref.insert(ref.begin() + static_cast<std::ptrdiff_t>(idx), n, x);
    } else if (op < 58) {
      std::size_t idx = ref.empty() ? 0 : rng() % (ref.size() + 1);
      T x = mk();
      (void)c.emplace(c.cbegin() + static_cast<std::ptrdiff_t>(idx), x);
      ref.emplace(ref.begin() + static_cast<std::ptrdiff_t>(idx), x);
    } else if (op < 68) {
      std::size_t n = rng() % 6;
      std::vector<T> src; for (std::size_t k = 0; k < n; ++k) src.push_back(mk());
      std::size_t idx = ref.empty() ? 0 : rng() % (ref.size() + 1);
      (void)c.insert(c.cbegin() + static_cast<std::ptrdiff_t>(idx), src.begin(), src.end());
      ref.insert(ref.begin() + static_cast<std::ptrdiff_t>(idx), src.begin(), src.end());
    } else if (op < 76 && !ref.empty()) {
      std::size_t idx = rng() % ref.size();
      (void)c.erase(c.cbegin() + static_cast<std::ptrdiff_t>(idx));
      ref.erase(ref.begin() + static_cast<std::ptrdiff_t>(idx));
    } else if (op < 82 && !ref.empty()) {
      std::size_t a = rng() % ref.size();
      std::size_t len = rng() % (ref.size() - a + 1);
      (void)c.erase(c.cbegin() + static_cast<std::ptrdiff_t>(a), c.cbegin() + static_cast<std::ptrdiff_t>(a + len));
      ref.erase(ref.begin() + static_cast<std::ptrdiff_t>(a), ref.begin() + static_cast<std::ptrdiff_t>(a + len));
    } else if (op < 87) {
      std::size_t n = rng() % 8; T x = mk();
      c.assign(n, x); ref.assign(n, x);
    } else if (op < 91) {
      std::size_t n = rng() % 10;
      std::vector<T> src; for (std::size_t k = 0; k < n; ++k) src.push_back(mk());
      c.assign(src.begin(), src.end()); ref.assign(src.begin(), src.end());
    } else if (op < 95) {
      std::size_t n = rng() % 12; c.resize(n); ref.resize(n);
    } else if (op < 98) {
      std::size_t n = rng() % 12; T x = mk(); c.resize(n, x); ref.resize(n, x);
    } else {
      C tmp = c;        // copy
      C tmp2 = std::move(tmp);
      c.swap(tmp2);     // member swap
    }
    if (c.size() != ref.size()) { CHECK(false); return; }
    for (std::size_t k = 0; k < ref.size(); ++k) {
      if (!(c[k] == ref[k])) { CHECK(false); return; }
    }
    CHECK(c.capacity() >= c.size());
  }
  CHECK(true);
}

void test_fuzz()
{
  SECTION("fuzz vs std::vector");
  for (unsigned s = 1; s <= 3; ++s) {
    fuzz_one<int, 0>(s * 13 + 1, 700);
    fuzz_one<int, 2>(s * 13 + 2, 700);
    fuzz_one<int, 8>(s * 13 + 3, 700);
    fuzz_one<std::string, 0>(s * 13 + 4, 500);
    fuzz_one<std::string, 2>(s * 13 + 5, 500);
    fuzz_one<std::string, 8>(s * 13 + 6, 500);
  }
}
} // namespace iv_test

int main()
{
  using namespace iv_test;
  test_types_and_ctors();
  test_storage_modes();
  test_copy_move();
  test_element_access();
  test_iterators();
  test_capacity();
  test_modifiers();
  test_swap();
  test_comparison();
  test_non_trivial();
  test_move_only();
  test_aliasing();
  test_exception_safety();
  test_allocator_lifecycle();
  test_constexpr();
  test_fuzz();

  std::printf("\n===============================\n");
  std::printf("PASS: %d   FAIL: %d\n", g_pass, g_fail);
  std::printf("===============================\n");
  return g_fail == 0 ? 0 : 1;
}
