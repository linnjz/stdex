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

// test_inplace_array.cpp
//
// Tests for stdex::inplace_array (include/inplace_array.hpp), rewritten for the
// final design: a fixed-size, std::array-like container whose [0, N) elements are
// always alive. Covered here:
//   * every constructor: default (value-init) / uninitialized / in_place (single
//     and multi argument) / initializer_list / iterator-pair / from_range
//   * wrong-size sources throw std::length_error
//   * element access, iterators (std::array's checked iterators), fill, swap
//   * comparison ==/!=/< /<= />/>=/<=>
//   * copy/move construction + assignment for trivial, non-trivial, move-only and
//     non-default-constructible element types; exception safety with a throwing
//     element type
//   * N == 0
//   * triviality / trivial-copyability guarantees and constexpr usability
//
// Self-contained: CHECK accumulates pass/fail; main() returns non-zero on failure.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <version>

#include "inplace_array.hpp"

namespace
{
int g_pass = 0;
int g_fail = 0;

#define CHECK(...)                                                                         \
  do {                                                                                     \
    if (static_cast<bool>(__VA_ARGS__)) {                                                  \
      ++g_pass;                                                                            \
    } else {                                                                               \
      ++g_fail;                                                                            \
      if (g_fail <= 80) std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__); \
    }                                                                                      \
  } while (0)

// ------------------------------------------------------------------
// element types
// ------------------------------------------------------------------
struct Trivial
{
  int value;
  friend constexpr auto operator<=> (Trivial const &, Trivial const &) = default;
};

struct TwoInts
{
  int a;
  int b;
  constexpr TwoInts(int x, int y)
      : a { x }
      , b { y }
  {
  }
};

struct NoDefault
{
  int value;
  explicit constexpr NoDefault(int v)
      : value { v }
  {
  }
  constexpr NoDefault(NoDefault const &)             = default;
  constexpr NoDefault &operator= (NoDefault const &) = default;
};

// Assignment vs construction counter, to prove fill() assigns instead of
// reconstructing over live objects.
struct AssignTrack
{
  int v = 0;
  static inline int ctors   = 0;
  static inline int assigns = 0;
  AssignTrack()             = default;
  AssignTrack(int x)
      : v { x }
  {
    ++ctors;
  }
  AssignTrack(AssignTrack const &o)
      : v { o.v }
  {
    ++ctors;
  }
  AssignTrack &operator= (AssignTrack const &o)
  {
    v = o.v;
    ++assigns;
    return *this;
  }
};

// Trivially move-constructible + trivially destructible, but NON-trivially copy
// constructible. Exercises the relocation ("iter_move_cat") fast path and the
// "bitcopy-constructible but not trivially copyable" situation.
struct Relocatable
{
  int value = 0;
  constexpr Relocatable()                            = default;
  constexpr Relocatable(int v)
      : value { v }
  {
  }
  constexpr Relocatable(Relocatable const &o)
      : value { o.value }
  {
  } // non-trivial copy ctor
  constexpr Relocatable(Relocatable &&)                 = default; // trivial move ctor
  constexpr Relocatable &operator= (Relocatable const &) = default;
  constexpr Relocatable &operator= (Relocatable &&)      = default;
  constexpr ~Relocatable()                               = default; // trivial dtor
};

// Trivially copy-constructible but NON-trivially copy-assignable. Exercises the
// "trivially copy-constructible but not trivially copyable" copy fast path.
struct NonTrivialCopyAssign
{
  int value = 0;
  constexpr NonTrivialCopyAssign() = default;
  constexpr NonTrivialCopyAssign(int v)
      : value { v }
  {
  }
  constexpr NonTrivialCopyAssign(NonTrivialCopyAssign const &) = default;
  constexpr NonTrivialCopyAssign(NonTrivialCopyAssign &&)      = default;
  constexpr NonTrivialCopyAssign &operator= (NonTrivialCopyAssign const &o)
  {
    value = o.value;
    return *this;
  }
  constexpr NonTrivialCopyAssign &operator= (NonTrivialCopyAssign &&) = default;
  constexpr ~NonTrivialCopyAssign()                                   = default;
};

// Throwing element for exception-safety coverage (ctor and copy ctor throw once
// the shared budget is exhausted).
struct Counted
{
  int v;
  inline static int live   = 0;
  inline static int budget = -1;

  static void step()
  {
    if (budget == 0) throw std::runtime_error("Counted");
    if (budget > 0) --budget;
  }

  explicit Counted(int x = 0)
      : v { x }
  {
    step();
    ++live;
  }
  Counted(Counted const &o)
      : v { o.v }
  {
    step();
    ++live;
  }
  Counted(Counted &&o) noexcept
      : v { o.v }
  {
    ++live;
  }
  Counted &operator= (Counted const &o) noexcept
  {
    v = o.v;
    return *this;
  }
  Counted &operator= (Counted &&o) noexcept
  {
    v = o.v;
    return *this;
  }
  ~Counted() { --live; }
};

// Single-pass input iterator (input_iterator only), to exercise the uncounted
// range path.
struct input_only_it
{
  using iterator_category = std::input_iterator_tag;
  using value_type        = int;
  using difference_type   = std::ptrdiff_t;
  int const *p_           = nullptr;

  int operator* () const { return *p_; }
  input_only_it &operator++ ()
  {
    ++p_;
    return *this;
  }
  void operator++ (int) { ++p_; }
  bool operator== (input_only_it const &o) const { return p_ == o.p_; }
};
static_assert(std::input_iterator<input_only_it>);
static_assert(!std::forward_iterator<input_only_it>);

/// Build an element of type T from an int seed, mirroring the inplace_vector
/// tests so the fuzz model can be shared.
template <class T>
T make_value(int v)
{
  if constexpr (std::is_same_v<std::remove_cv_t<T>, std::string>) {
    return std::to_string(v);
  } else {
    return T { v };
  }
}

/// Compare an inplace_array to a std::vector reference model element-wise.
template <class A, class T>
bool matches(A const &a, std::vector<T> const &ref)
{
  if (a.size() != ref.size()) return false;
  for (std::size_t i = 0; i < ref.size(); ++i) {
    if (!(a[i] == ref[i])) return false;
  }
  return true;
}

// ===========================================================================
// 1. trivial element: constructors / access / iterators / modifiers / compare
// ===========================================================================
void test_trivial()
{
  using A = stdex::inplace_array<int, 4>;

  // static capacity + size type
  CHECK(A::max_size() == 4);

  // default: value-initializes all N
  {
    A a;
    CHECK(a.size() == 4 && !a.empty());
    for (std::size_t i = 0; i < 4; ++i) CHECK(a[i] == 0);
    A b {};
    for (std::size_t i = 0; i < 4; ++i) CHECK(b[i] == 0);
  }

  // uninitialized tag: skip construction, then assign
  {
    A a(stdex::uninitialized);
    for (std::size_t i = 0; i < 4; ++i) a[i] = static_cast<int>(i) * 10;
    CHECK(a[0] == 0 && a[3] == 30);
  }

  // in_place single value / multi argument
  {
    A a(std::in_place, 7);
    for (std::size_t i = 0; i < 4; ++i) CHECK(a[i] == 7);
    stdex::inplace_array<TwoInts, 3> t(std::in_place, 1, 2);
    for (std::size_t i = 0; i < 3; ++i) CHECK(t[i].a == 1 && t[i].b == 2);
  }

  // element access
  {
    A a { 1, 2, 3, 4 };
    CHECK(&a[0] == a.data() && a.data() + 2 == &a[2]);
    CHECK(a.front() == 1 && a.back() == 4);
    CHECK(a.at(2) == 3);
    bool threw = false;
    try {
      (void) a.at(4);
    } catch (std::out_of_range const &) {
      threw = true;
    }
    CHECK(threw);
  }

  // iterators (std::array's checked iterators)
  {
    A a { 1, 2, 3, 4 };
    CHECK(a.end() - a.begin() == 4);
    int sum = 0;
    for (auto it = a.begin(); it != a.end(); ++it) sum += *it;
    CHECK(sum == 10);
    int rsum = 0;
    for (auto it = a.rbegin(); it != a.rend(); ++it) rsum += *it;
    CHECK(rsum == 10);
    CHECK(a.cbegin() == a.begin() && a.cend() == a.end());
    CHECK(a.crbegin() == a.rbegin() && a.crend() == a.rend());
    *a.begin() = 100;
    CHECK(a[0] == 100);
    A const &ca = a;
    CHECK(ca.begin() == a.begin() && ca[0] == 100);
  }

  // fill assigns over live elements
  {
    A a { 1, 2, 3, 4 };
    a.fill(9);
    for (std::size_t i = 0; i < 4; ++i) CHECK(a[i] == 9);
  }

  // swap (distinct objects) + free swap
  {
    A a { 1, 2, 3, 4 };
    A b { 9, 9, 9, 9 };
    a.swap(b);
    CHECK(a[0] == 9 && b[0] == 1 && b[3] == 4);
    swap(a, b);
    CHECK(a[0] == 1 && b[0] == 9);
  }

  // comparison
  {
    A a { 1, 2, 3, 4 };
    A b { 1, 2, 3, 4 };
    A c { 1, 2, 3, 5 };
    CHECK(a == b);
    CHECK(!(a == c));
    CHECK(a != c);
    CHECK(a < c && c > a);
    CHECK(a <= b && b >= a);
    CHECK((a <=> b) == 0);
    CHECK((a <=> c) < 0);
  }

  // copy / move / assignment
  {
    A a { 1, 2, 3, 4 };
    A c = a;
    CHECK(c == a);
    A m = std::move(c);
    CHECK(m == a);
    A d { 0, 0, 0, 0 };
    d = a;
    CHECK(d == a);
    A e { 0, 0, 0, 0 };
    e = std::move(d);
    CHECK(e == a);
  }

  // comparison via a user type whose operator<=> is defaulted
  {
    using AT = stdex::inplace_array<Trivial, 3>;
    AT x { Trivial { 1 }, Trivial { 2 }, Trivial { 3 } };
    AT y { Trivial { 1 }, Trivial { 2 }, Trivial { 4 } };
    CHECK(x < y);
    CHECK((x <=> y) < 0);
  }
}

// ===========================================================================
// 2. initializer_list
// ===========================================================================
void test_initializer_list()
{
  using A = stdex::inplace_array<int, 4>;
  {
    A a { 1, 2, 3, 4 };
    CHECK(a[0] == 1 && a[3] == 4);
  }
  {
    bool threw = false;
    try {
      A a { 1, 2 };
      (void) a;
    } catch (std::length_error const &) {
      threw = true;
    }
    CHECK(threw);
  }
  {
    bool threw = false;
    try {
      A a { 1, 2, 3, 4, 5 };
      (void) a;
    } catch (std::length_error const &) {
      threw = true;
    }
    CHECK(threw);
  }
}

// ===========================================================================
// 3. iterator-pair and from_range constructors (exact N, else length_error)
// ===========================================================================
void test_range_ctors()
{
  using A = stdex::inplace_array<int, 4>;

  // exactly N (forward/contiguous iterators)
  {
    std::vector<int> v { 1, 2, 3, 4 };
    A a(v.begin(), v.end());
    CHECK(a[0] == 1 && a[3] == 4);
  }
  // raw pointers
  {
    int arr[4] { 5, 6, 7, 8 };
    A a(arr, arr + 4);
    CHECK(a[0] == 5 && a[3] == 8);
  }
  // too few / too many -> length_error
  {
    std::vector<int> few { 1, 2 };
    std::vector<int> many { 1, 2, 3, 4, 5 };
    bool threw_few = false, threw_many = false;
    try {
      A a(few.begin(), few.end());
      (void) a;
    } catch (std::length_error const &) {
      threw_few = true;
    }
    try {
      A a(many.begin(), many.end());
      (void) a;
    } catch (std::length_error const &) {
      threw_many = true;
    }
    CHECK(threw_few && threw_many);
  }
  // single-pass input iterators (uncounted path)
  {
    int arr[4] { 9, 8, 7, 6 };
    A a(input_only_it { arr }, input_only_it { arr + 4 });
    CHECK(a[0] == 9 && a[3] == 6);
  }
  {
    int arr[2] { 1, 2 };
    bool threw = false;
    try {
      A a(input_only_it { arr }, input_only_it { arr + 2 });
      (void) a;
    } catch (std::length_error const &) {
      threw = true;
    }
    CHECK(threw);
  }

#ifdef __cpp_lib_containers_ranges
  {
    std::vector<int> v { 4, 3, 2, 1 };
    A a(std::from_range, v);
    CHECK(a[0] == 4 && a[3] == 1);
  }
  {
    std::vector<int> v { 1, 2 };
    bool threw = false;
    try {
      A a(std::from_range, v);
      (void) a;
    } catch (std::length_error const &) {
      threw = true;
    }
    CHECK(threw);
  }
#endif
}

// ===========================================================================
// 4. non-trivial elements (std::string): full five + comparison + fill
// ===========================================================================
void test_string()
{
  using A = stdex::inplace_array<std::string, 3>;

  A a { std::string("a"), std::string("b"), std::string("c") };
  CHECK(a[0] == "a" && a[2] == "c");

  // default: three empty strings
  {
    A d;
    CHECK(d[0].empty() && d[2].empty());
  }
  // in_place
  {
    A d(std::in_place, std::string("x"));
    CHECK(d[0] == "x" && d[2] == "x");
  }
  // fill / copy / move / assign
  {
    A d { std::string("a"), std::string("b"), std::string("c") };
    d.fill("q");
    CHECK(d[0] == "q" && d[2] == "q");
  }
  {
    A c = a;
    CHECK(c == a);
    A m = std::move(c);
    CHECK(m == a);
    A d { std::string("1"), std::string("2"), std::string("3") };
    d = a;
    CHECK(d == a);
    A e { std::string("1"), std::string("2"), std::string("3") };
    e = std::move(d);
    CHECK(e == a);
  }
  // comparison
  {
    A b { std::string("a"), std::string("b"), std::string("d") };
    CHECK(a < b);
  }
}

// ===========================================================================
// 5. move-only element type
// ===========================================================================
void test_move_only()
{
  using P = std::unique_ptr<int>;
  using A = stdex::inplace_array<P, 3>;

  A a; // three null pointers
  CHECK(!a[0] && !a[1] && !a[2]);
  a[0] = std::make_unique<int>(1);
  a[2] = std::make_unique<int>(3);

  A b = std::move(a);
  CHECK(b[0] && *b[0] == 1 && *b[2] == 3);

  A c;
  c = std::move(b);
  CHECK(c[2] && *c[2] == 3);
}

// ===========================================================================
// 6. non-default-constructible element type
// ===========================================================================
void test_no_default()
{
  using A = stdex::inplace_array<NoDefault, 3>;
  static_assert(!std::is_default_constructible_v<A>);

  A a(std::in_place, 5);
  CHECK(a[0].value == 5 && a[2].value == 5);

  A b { NoDefault(1), NoDefault(2), NoDefault(3) };
  CHECK(b[0].value == 1 && b[2].value == 3);

  NoDefault src[3] { NoDefault(7), NoDefault(8), NoDefault(9) };
  A c(src, src + 3); // iterator-pair
  CHECK(c[1].value == 8);

  A d = c;
  CHECK(d[2].value == 9);
  A e(std::in_place, 0);
  e = std::move(d);
  CHECK(e[0].value == 7);
}

// ===========================================================================
// 7. N == 0
// ===========================================================================
template <class T>
void test_zero()
{
  stdex::inplace_array<T, 0> a;
  CHECK(a.empty());
  CHECK(a.size() == 0);
  CHECK(a.begin() == a.end() && a.rbegin() == a.rend());
  CHECK(a.data() == nullptr);
  a.fill(T {});
  stdex::inplace_array<T, 0> b;
  a.swap(b); // both empty: no aliasing element ranges
  CHECK(a == b);
  CHECK(!(a < b));
  stdex::inplace_array<T, 0> c = a;
  CHECK(c == a);
  stdex::inplace_array<T, 0> d = std::move(c);
  CHECK(d == a);
}

// ===========================================================================
// 7b. boundary tests on tiny capacities: exact-N constructors and access
// ===========================================================================
template <std::size_t N>
void test_small_capacity()
{
  using A = stdex::inplace_array<int, N>;

  CHECK(A::max_size() == N);

  // default-initialises exactly N zeros
  {
    A a;
    CHECK(a.size() == N && !a.empty());
    for (std::size_t i = 0; i < N; ++i) CHECK(a[i] == 0);
  }

  // initializer_list must match N exactly
  {
    A a;
    if constexpr (N == 1) {
      a = A { 7 };
    } else if constexpr (N == 2) {
      a = A { 7, 8 };
    } else {
      a = A { 7, 8, 9 };
    }
    CHECK(a[0] == 7 && a[N - 1] == static_cast<int>(N) + 6);

    bool threw = false;
    try {
      A bad { 1, 2, 3, 4, 5, 6, 7, 8 }; // always longer than N here (N <= 3)
      (void) bad;
    } catch (std::length_error const &) {
      threw = true;
    }
    CHECK(threw);
  }

  // iterator-pair / from_range must match N exactly
  {
    std::vector<int> exact(static_cast<std::size_t>(N), 3);
    A a(exact.begin(), exact.end());
    CHECK(a.size() == N && a[N - 1] == 3);

    std::vector<int> too_few; // empty, always fewer than N (N >= 1)
    bool threw = false;
    try {
      A bad(too_few.begin(), too_few.end());
      (void) bad;
    } catch (std::length_error const &) {
      threw = true;
    }
    CHECK(threw);

#ifdef __cpp_lib_containers_ranges
    A fr(std::from_range, exact);
    CHECK(fr.size() == N && fr[0] == 3);
#endif
  }

  // element access boundaries
  {
    std::vector<int> src(static_cast<std::size_t>(N), 0);
    for (std::size_t i = 0; i < N; ++i) src[i] = static_cast<int>(i) + 1;
    A a(src.begin(), src.end());
    CHECK(a.front() == 1 && a.back() == static_cast<int>(N));
    CHECK(a.at(0) == 1 && a.at(N - 1) == static_cast<int>(N));
    bool oor = false;
    try {
      (void) a.at(N);
    } catch (std::out_of_range const &) {
      oor = true;
    }
    CHECK(oor);
  }

  // fill / swap / compare / copy / move
  {
    std::vector<int> ones(static_cast<std::size_t>(N), 1);
    std::vector<int> twos(static_cast<std::size_t>(N), 2);
    A a(ones.begin(), ones.end());
    A b(twos.begin(), twos.end());
    a.fill(5);
    for (std::size_t i = 0; i < N; ++i) CHECK(a[i] == 5);
    a.swap(b);
    CHECK(a[0] == 2 && b[0] == 5);
    swap(a, b);
    CHECK(a[0] == 5 && b[0] == 2);
    CHECK(!(a == b) && b < a);
    A c = b;
    CHECK(c == b);
    A d = std::move(c);
    CHECK(d == b);
  }
}

// ===========================================================================
// 7c. fast-path / value-type conversion coverage
// ===========================================================================
void test_fast_path_and_conversions()
{
  // POD contiguous source (int -> int): every constructor that copies a
  // contiguous range exercises the memcpy fast path.
  {
    int src[4] = { 1, 2, 3, 4 };
    stdex::inplace_array<int, 4> a(src, src + 4);
    CHECK(a[0] == 1 && a[3] == 4);

    stdex::inplace_array<int, 4> const ca(src, src + 4);
    stdex::inplace_array<int, 4> b = ca; // copy via uninitialized_copy fast path
    CHECK(b[2] == 3);

#ifdef __cpp_lib_containers_ranges
    std::vector<int> v { 5, 6, 7, 8 };
    stdex::inplace_array<int, 4> c(std::from_range, v);
    CHECK(c[0] == 5 && c[3] == 8);
#endif
  }

  // Cross value-type range must convert element-wise, not raw-memcpy a
  // different type (int -> long / unsigned).
  {
    int src[3] = { 1, 2, 3 };
    stdex::inplace_array<long, 3> l(src, src + 3);
    CHECK(l[0] == 1L && l[2] == 3L);

    int neg[2] = { -1, 300 };
    stdex::inplace_array<unsigned, 2> u(neg, neg + 2);
    CHECK(u[0] == static_cast<unsigned>(-1) && u[1] == 300u);
  }

  // Relocatable element type: exercises the "bitcopy-constructible but not
  // trivially copyable" branch on copy / move construction.
  {
    stdex::inplace_array<Relocatable, 3> a { Relocatable(1), Relocatable(2), Relocatable(3) };
    stdex::inplace_array<Relocatable, 3> b = a;             // copy (non-trivial copy ctor)
    CHECK(b[0].value == 1 && b[2].value == 3);
    stdex::inplace_array<Relocatable, 3> c = std::move(a);  // move (trivial move ctor)
    CHECK(c[1].value == 2);
    stdex::inplace_array<Relocatable, 3> d { Relocatable(0), Relocatable(0), Relocatable(0) };
    d = b;
    CHECK(d[2].value == 3);
  }

  // NonTrivialCopyAssign element type: trivially copy-constructible but not
  // trivially copy-assignable.
  {
    stdex::inplace_array<NonTrivialCopyAssign, 3> a { NonTrivialCopyAssign(1), NonTrivialCopyAssign(2), NonTrivialCopyAssign(3) };
    stdex::inplace_array<NonTrivialCopyAssign, 3> b = a;
    CHECK(b[1].value == 2);
    a = b; // copy-assign path
    CHECK(a[2].value == 3);
  }
}

// ===========================================================================
// 7d. randomized fuzz against a std::vector reference model
// ===========================================================================
template <class T, std::size_t N>
void fuzz(std::uint32_t seed, int iters)
{
  std::mt19937 rng(seed);
  auto V = [](int x) { return make_value<T>(x); };
  stdex::inplace_array<T, N> arr;
  std::vector<T> ref;

  // Seed both from the same source so they start equal.
  {
    std::vector<T> init;
    for (std::size_t i = 0; i < N; ++i) init.push_back(V(static_cast<int>(i)));
    arr = stdex::inplace_array<T, N>(init.begin(), init.end());
    ref = init;
  }

  auto verify = [&](char const *where) {
    if (!matches(arr, ref)) {
      std::printf("FUZZ MISMATCH seed=%u at %s: arr=%zu ref=%zu\n", seed, where, arr.size(), ref.size());
      ++g_fail;
      return false;
    }
    return true;
  };

  for (int step = 0; step < iters; ++step) {
    unsigned const op = static_cast<unsigned>(rng() % 6u);
    int const val     = static_cast<int>(rng() % 100);
    switch (op) {
      case 0: { // fill
        arr.fill(V(val));
        for (auto &e : ref) e = V(val);
        break;
      }
      case 1: { // copy construct + swap
        stdex::inplace_array<T, N> tmp = arr;
        arr.swap(tmp);
        break;
      }
      case 2: { // copy assign
        stdex::inplace_array<T, N> tmp = arr;
        arr = tmp;
        break;
      }
      case 3: { // move construct + move assign
        stdex::inplace_array<T, N> tmp = std::move(arr);
        arr = std::move(tmp);
        break;
      }
      case 4: { // rebuild from a fresh range
        std::vector<T> tmp;
        for (std::size_t i = 0; i < N; ++i) tmp.push_back(V(val + static_cast<int>(i)));
        arr = stdex::inplace_array<T, N>(tmp.begin(), tmp.end());
        ref = tmp;
        break;
      }
      case 5: { // element mutation via iterators
        std::size_t const p = rng() % N;
        *std::next(arr.begin(), static_cast<std::ptrdiff_t>(p)) = V(val);
        ref[p] = V(val);
        break;
      }
    }
    if (!verify("step")) break;
  }
}

// ===========================================================================
// 8. exception safety
// ===========================================================================
void test_exception_safety()
{
  using A = stdex::inplace_array<Counted, 4>;

  // default (value-init) throws mid-way: no leaks, no double destroy
  {
    Counted::live   = 0;
    Counted::budget = 2;
    bool threw      = false;
    try {
      A a;
      (void) a;
    } catch (std::runtime_error const &) {
      threw = true;
    }
    Counted::budget = -1;
    CHECK(threw);
    CHECK(Counted::live == 0);
  }

  // copy ctor throws mid-way: source intact, destination rolled back
  {
    Counted::live   = 0;
    Counted::budget = -1;
    A a(std::in_place, 5);
    int const live0 = Counted::live;
    Counted::budget = 1;
    bool threw      = false;
    try {
      A b = a;
      (void) b;
    } catch (std::runtime_error const &) {
      threw = true;
    }
    Counted::budget = -1;
    CHECK(threw);
    CHECK(Counted::live == live0);
    CHECK(a[0].v == 5 && a[3].v == 5);
    Counted::live = 0;
  }

  // iterator-pair ctor with a throwing element: rolled back, no leak
  {
    Counted::live   = 0;
    Counted::budget = -1;
    Counted src[4] { Counted(1), Counted(2), Counted(3), Counted(4) };
    Counted::budget = 2; // the 3rd copy during array construction throws
    bool threw      = false;
    try {
      using AC = stdex::inplace_array<Counted, 4>;
      AC a(src, src + 4);
      (void) a;
    } catch (std::runtime_error const &) {
      threw = true;
    }
    Counted::budget = -1;
    CHECK(threw);
    CHECK(Counted::live == 4); // only the four source objects remain
    Counted::live = 0;
  }
}

// ===========================================================================
// 9. fill assigns, not constructs
// ===========================================================================
void test_fill_assigns()
{
  stdex::inplace_array<AssignTrack, 3> a;
  AssignTrack::ctors   = 0;
  AssignTrack::assigns = 0;
  a.fill(AssignTrack(9)); // 1 temporary ctor, 3 assignments, 0 constructions
  CHECK(AssignTrack::ctors == 1);
  CHECK(AssignTrack::assigns == 3);
  CHECK(a[0].v == 9 && a[2].v == 9);
}

// ===========================================================================
// 10. std::array-like non-member interface: get<I>() and to_inplace_array()
// ===========================================================================
void test_get_to_inplace_array()
{
  using A = stdex::inplace_array<int, 4>;
  A a { 1, 2, 3, 4 };

  // reference categories
  static_assert(std::is_same_v<decltype(stdex::get<0>(a)), int &>);
  static_assert(std::is_same_v<decltype(stdex::get<0>(std::declval<A const &>())), int const &>);
  static_assert(std::is_same_v<decltype(stdex::get<0>(std::declval<A &&>())), int &&>);
  static_assert(std::is_same_v<decltype(stdex::get<0>(std::declval<A const &&>())), int const &&>);

  CHECK(stdex::get<0>(a) == 1 && stdex::get<3>(a) == 4);
  stdex::get<1>(a) = 20;
  CHECK(a[1] == 20);
  A const &ca = a;
  CHECK(stdex::get<1>(ca) == 20);
  {
    A b { 1, 2, 3, 4 };
    int &&rr = stdex::get<2>(std::move(b));
    CHECK(rr == 3);
  }

  // to_inplace_array (lvalue): copies out of a C array
  int src[4] { 5, 6, 7, 8 };
  auto arr = stdex::to_inplace_array(src);
  static_assert(std::is_same_v<decltype(arr), stdex::inplace_array<int, 4>>);
  CHECK(arr.size() == 4 && arr[0] == 5 && arr[3] == 8);
  src[0] = 99;
  CHECK(arr[0] == 5); // independent copy

  // to_inplace_array (const lvalue): cv-qualifiers stripped from the element type
  int const csrc[3] { 1, 2, 3 };
  auto carr = stdex::to_inplace_array(csrc);
  static_assert(std::is_same_v<decltype(carr), stdex::inplace_array<int, 3>>);
  CHECK(carr[2] == 3);

  // to_inplace_array (rvalue): moves elements
  std::string strs[2] { std::string("aa"), std::string("bb") };
  auto sarr = stdex::to_inplace_array(std::move(strs));
  static_assert(std::is_same_v<decltype(sarr), stdex::inplace_array<std::string, 2>>);
  CHECK(sarr[0] == "aa" && sarr[1] == "bb");

  // distinct name: coexists with std::to_array (no shared overload set)
  {
    int src2[3] { 1, 2, 3 };
    auto mine = stdex::to_inplace_array(src2);
    auto stdv = std::to_array(src2);
    static_assert(std::is_same_v<decltype(mine), stdex::inplace_array<int, 3>>);
    static_assert(std::is_same_v<decltype(stdv), std::array<int, 3>>);
    CHECK(mine[0] == 1 && stdv[2] == 3);
  }
}

// ===========================================================================
// 11. structured bindings (std::tuple_size / std::tuple_element + ADL get)
// ===========================================================================
void test_structured_binding()
{
  using A = stdex::inplace_array<int, 3>;
  static_assert(std::tuple_size_v<A> == 3);
  static_assert(std::is_same_v<std::tuple_element_t<0, A>, int>);
  static_assert(std::is_same_v<std::tuple_element_t<2, A>, int>);
  static_assert(std::tuple_size_v<stdex::inplace_array<int, 0>> == 0);

  A a { 1, 2, 3 };

  // by value: independent copy
  {
    auto [x, y, z] = a;
    CHECK(x == 1 && y == 2 && z == 3);
    x = 100;
    CHECK(a[0] == 1);
  }
  // by reference: mutates the array
  {
    auto &[rx, ry, rz] = a;
    ry = 20;
    (void) rx;
    (void) rz;
    CHECK(a[1] == 20);
  }
  // const reference
  {
    A const &ca = a;
    auto const &[cx, cy, cz] = ca;
    CHECK(cx == 1 && cy == 20 && cz == 3);
  }
  // non-trivial element type
  {
    stdex::inplace_array<std::string, 2> s { std::string("aa"), std::string("bb") };
    auto &[s0, s1] = s;
    CHECK(s0 == "aa" && s1 == "bb");
    s1 = "cc";
    CHECK(s[1] == "cc");
  }
}

// ===========================================================================
// 12. triviality / type-trait guarantees
// ===========================================================================
static_assert(std::is_trivially_copyable_v<stdex::inplace_array<int, 4>>);
static_assert(std::is_trivially_destructible_v<stdex::inplace_array<int, 4>>);
static_assert(std::is_trivially_copyable_v<stdex::inplace_array<Trivial, 3>>);
static_assert(!std::is_trivially_copyable_v<stdex::inplace_array<std::string, 3>>);
static_assert(!std::is_trivially_destructible_v<stdex::inplace_array<std::string, 3>>);
static_assert(std::is_copy_assignable_v<stdex::inplace_array<std::string, 3>>);
static_assert(std::is_move_assignable_v<stdex::inplace_array<std::string, 3>>);
static_assert(sizeof(stdex::inplace_array<std::string, 0>) <= sizeof(void *));
// iterators are borrowed from std::array (debug-checked in debug builds)
static_assert(std::is_same_v<stdex::inplace_array<int, 4>::iterator, std::array<int, 4>::iterator>);

// Trivially move-constructible + trivially destructible but non-trivially
// copyable (Relocatable): the container must expose a trivial move constructor
// and a trivial destructor (mirrors the inplace_vector guarantee). The storage
// layer declares its copy/move SMFs explicitly so the implicit move constructor
// is not suppressed by the user-declared destructor.
static_assert(std::is_trivially_destructible_v<stdex::inplace_array<Relocatable, 4>>);
static_assert(std::is_trivially_move_constructible_v<stdex::inplace_array<Relocatable, 4>>);
static_assert(std::is_trivially_copy_constructible_v<stdex::inplace_array<NonTrivialCopyAssign, 4>>);
static_assert(!std::is_trivially_copy_assignable_v<stdex::inplace_array<NonTrivialCopyAssign, 4>>);

// N == 0: empty and trivially constructible/copyable.
static_assert(std::is_empty_v<stdex::inplace_array<std::string, 0>>);
static_assert(std::is_trivially_default_constructible_v<stdex::inplace_array<int, 0>>);
static_assert(std::is_trivially_copyable_v<stdex::inplace_array<std::string, 0>>);

// Non-default-constructible element type: no default constructor.
static_assert(!std::is_default_constructible_v<stdex::inplace_array<NoDefault, 3>>);

// ===========================================================================
// 13. constexpr usability for a trivial element type
// ===========================================================================
namespace constexpr_checks
{
[[maybe_unused]] constexpr bool drive()
{
  stdex::inplace_array<int, 4> a;
  if (a.size() != 4 || a[0] != 0 || a[3] != 0) return false;

  stdex::inplace_array<int, 4> b { 1, 2, 3, 4 };
  if (b[0] != 1 || b[2] != 3) return false;

  b.fill(7);
  if (b[1] != 7) return false;

  // defaulted trivial copy / move must also be constant-evaluable
  stdex::inplace_array<int, 4> e = b;
  if (e[0] != 7 || e[3] != 7) return false;
  stdex::inplace_array<int, 4> f = std::move(e);
  if (f[0] != 7 || f[3] != 7) return false;

  stdex::inplace_array<int, 4> c(std::in_place, 5);
  if (c[0] != 5 || c[3] != 5) return false;

  int src[4] { 4, 3, 2, 1 };
  stdex::inplace_array<int, 4> d(src, src + 4);
  if (d[0] != 4 || d[3] != 1) return false;

  if (stdex::get<0>(b) != 7 || stdex::get<3>(d) != 1) return false;
  int ta[3] { 1, 2, 3 };
  auto tarr = stdex::to_inplace_array(ta);
  if (tarr[0] != 1 || tarr[2] != 3) return false;

  if (!(c < b)) return false;

  // multi-argument in_place construction of a non-trivial literal type.
  // Before C++26 the union storage cannot be constant-evaluated for non-trivial
  // types (no std::start_lifetime), so this is only a compile-time check then.
#if defined(__cpp_lib_start_lifetime) && __cpp_lib_start_lifetime >= 202603L
  {
    stdex::inplace_array<TwoInts, 3> t(std::in_place, 1, 2);
    if (t[0].a != 1 || t[2].b != 2) return false;
  }
#endif

  // comparison surface at compile time
  {
    stdex::inplace_array<int, 3> x { 1, 2, 3 };
    stdex::inplace_array<int, 3> y { 1, 2, 3 };
    stdex::inplace_array<int, 3> z { 1, 2, 4 };
    if (!(x == y) || (x == z)) return false;
    if (!(x < z) || (z <= x)) return false;
    if (!((x <=> y) == 0) || !((x <=> z) < 0)) return false;
  }

  // swap at compile time
  {
    stdex::inplace_array<int, 2> p { 1, 2 };
    stdex::inplace_array<int, 2> q { 8, 9 };
    p.swap(q);
    if (p[0] != 8 || q[1] != 2) return false;
  }

  // N == 1 boundary
  {
    stdex::inplace_array<int, 1> one { 5 };
    if (one.size() != 1 || one.front() != 5 || one.back() != 5) return false;
    one.fill(0);
    if (one[0] != 0) return false;
  }

  stdex::inplace_array<int, 0> z;
  if (!z.empty() || z.begin() != z.end()) return false;
  return true;
}
} // namespace constexpr_checks

// Trivial element types use a plain array member before C++26, so constant
// evaluation works there on every conforming compiler; non-trivial elements
// (the TwoInts case) are only constant-evaluable from C++26 via start_lifetime.
static_assert(constexpr_checks::drive(), "inplace_array<int,4> must be usable in constant evaluation");

} // namespace

int main()
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  std::printf("\n=== inplace_array: trivial ===\n");
  test_trivial();

  std::printf("\n=== inplace_array: initializer_list ===\n");
  test_initializer_list();

  std::printf("\n=== inplace_array: range / from_range ===\n");
  test_range_ctors();

  std::printf("\n=== inplace_array: non-trivial ===\n");
  test_string();
  test_move_only();
  test_no_default();

  std::printf("\n=== inplace_array: boundaries ===\n");
  test_zero<int>();
  test_zero<std::string>();
  test_small_capacity<1>();
  test_small_capacity<2>();
  test_small_capacity<3>();

  std::printf("\n=== inplace_array: exception safety ===\n");
  test_exception_safety();
  test_fill_assigns();

  std::printf("\n=== inplace_array: fast paths / conversions ===\n");
  test_fast_path_and_conversions();

  std::printf("\n=== inplace_array: get / to_inplace_array ===\n");
  test_get_to_inplace_array();

  std::printf("\n=== inplace_array: structured bindings ===\n");
  test_structured_binding();

  std::printf("\n=== inplace_array: fuzz ===\n");
  fuzz<int, 8>(0xC0FFEEu, 3000);
  fuzz<int, 1>(0x1234u, 2000);
  fuzz<std::string, 6>(0xBEEFu, 3000);
  fuzz<Trivial, 8>(0xABCDu, 2000);

  std::printf("\n=== inplace_array summary: %d passed, %d failed ===\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
