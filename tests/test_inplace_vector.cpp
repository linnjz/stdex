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

// test_inplace_vector.cpp
//
// Functional / boundary / fuzz / constexpr tests for stdex::inplace_vector
// (include/inplace_vector.hpp). Self-contained (no GoogleTest): every check
// goes through the CHECK macro and main() returns non-zero on failure.
//
// Coverage: every public member of the primary template and the
// N == 0 specialization, across POD / NPOD / relocatable-but-not-copyable /
// non-default-constructible / move-only element types; boundary conditions;
// compile-time (constexpr) usage; randomized "fuzz" comparison against a
// std::vector reference model; and the fast-path (bit-copy) element types.

#include <algorithm>
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
#include <version>
#include <vector>

#include "inplace_vector.hpp"

namespace
{
int g_pass = 0;
int g_fail = 0;

#define CHECK(...)                                                          \
  do {                                                                      \
    if (static_cast<bool>(__VA_ARGS__)) {                                   \
      ++g_pass;                                                             \
    } else {                                                                \
      ++g_fail;                                                             \
      if (g_fail <= 60) std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__); \
    }                                                                       \
  } while (0)

// try_* returns T* before C++26 and std::optional<T&> from C++26 on.
template <class R>
constexpr bool try_ok(R const &r)
{
#if __cplusplus >= 202603L
  return r.has_value();
#else
  return r != nullptr;
#endif
}

// ---------------------------------------------------------------------------
// Element types spanning the implementation branches / triviality classes.
// Every "generic" type is: default-constructible from `int`, copyable, movable,
// equality + ordering comparable. It is usable by the shared generic tests.
// ---------------------------------------------------------------------------

// Fully trivial / trivially copyable POD.
struct Trivial
{
  int value;
  friend constexpr auto operator<=>(Trivial const &, Trivial const &) = default;
};

// Trivially move-constructible + trivially destructible, but NON-trivially copy
// constructible. Exercises the relocation ("iter_move_cat") fast path and the
// "bitcopy_constructible but not trivially_copyable" situation.
struct Relocatable
{
  int value = 0;
  constexpr Relocatable()                        = default;
  constexpr Relocatable(int v) : value { v } {}
  constexpr Relocatable(Relocatable const &o) : value { o.value } {} // non-trivial copy ctor
  constexpr Relocatable(Relocatable &&)             = default;        // trivial move ctor
  constexpr Relocatable &operator=(Relocatable const &) = default;
  constexpr Relocatable &operator=(Relocatable &&)      = default;
  constexpr ~Relocatable()                              = default;    // trivial dtor
};

// Trivially copy-constructible but NON-trivially copy-assignable. Exercises the
// "trivially copy-constructible but not trivially copyable" copy fast path.
struct NonTrivialCopyAssign
{
  int value = 0;
  constexpr NonTrivialCopyAssign()                               = default;
  constexpr NonTrivialCopyAssign(int v) : value { v } {}
  constexpr NonTrivialCopyAssign(NonTrivialCopyAssign const &)   = default;
  constexpr NonTrivialCopyAssign(NonTrivialCopyAssign &&)        = default;
  constexpr NonTrivialCopyAssign &operator=(NonTrivialCopyAssign const &o)
  {
    value = o.value;
    return *this;
  }
  constexpr NonTrivialCopyAssign &operator=(NonTrivialCopyAssign &&) = default;
  constexpr ~NonTrivialCopyAssign()                                  = default;
};

// Only constructible from int (not default-constructible).
struct NoDefault
{
  int value;
  explicit constexpr NoDefault(int v) : value { v } {}
  constexpr NoDefault(NoDefault const &)            = default;
  constexpr NoDefault &operator=(NoDefault const &) = default;
};

// Tracks live instances and can be told to throw after a number of successful
// constructions (default or copy), for strong-guarantee / leak checks.
struct Counted
{
  int v;
  inline static int live   = 0;
  inline static int budget = -1; // -1 = unlimited; 0 = next ctor throws

  static void step()
  {
    if (budget == 0) throw std::runtime_error("Counted");
    if (budget > 0) --budget;
  }

  explicit Counted(int x = 0) : v { x } { step(); ++live; }
  Counted(Counted const &o) : v { o.v } { step(); ++live; }
  Counted(Counted &&o) noexcept : v { o.v } { ++live; }
  Counted &operator=(Counted const &o) noexcept { v = o.v; return *this; }
  Counted &operator=(Counted &&o) noexcept { v = o.v; return *this; }
  ~Counted() { --live; }
};

template <class T>
T make_value(int v)
{
  if constexpr (std::is_same_v<std::remove_cv_t<T>, std::string>) {
    return std::to_string(v);
  } else {
    return T { v };
  }
}

template <class V, class T>
bool matches(V const &v, std::vector<T> const &ref)
{
  if (v.size() != ref.size()) return false;
  for (std::size_t i = 0; i < ref.size(); ++i) {
    if (!(v[i] == ref[i])) return false;
  }
  return true;
}

// ===========================================================================
// Generic behavioural tests: every public method, valid + boundary behaviour.
// Instantiated for several element types (POD, relocatable, non-trivial copy,
// std::string).
// ===========================================================================
template <class T, std::size_t N>
void test_generic()
{
  static_assert(N >= 12, "generic test needs some headroom");
  using IV = stdex::inplace_vector<T, N>;
  auto V  = [](int x) { return make_value<T>(x); };

  // ---- construction / capacity / iterators ------------------------------
  {
    IV v;
    CHECK(v.empty());
    CHECK(v.size() == 0);
    CHECK(IV::capacity() == N);
    CHECK(IV::max_size() == N);
    CHECK(v.begin() == v.end());
    CHECK(v.cbegin() == v.cend());
    CHECK(v.rbegin() == v.rend());
    CHECK(v.crbegin() == v.crend());
    CHECK(v.data() != nullptr);
    v.shrink_to_fit(); // no-op
    v.reserve(N);      // ok
    v.reserve(0);      // ok
    bool threw = false;
    try { v.reserve(N + 1); } catch (std::bad_alloc const &) { threw = true; }
    CHECK(threw);
  }

  // ---- push/emplace/back + element access -------------------------------
  {
    IV v;
    v.push_back(V(1));
    v.emplace_back(V(2));
    v.push_back(V(3));
    CHECK(v.size() == 3);
    CHECK(v[0] == V(1) && v[1] == V(2) && v[2] == V(3));
    CHECK(v.front() == V(1));
    CHECK(v.back() == V(3));
    CHECK(&v[0] == v.data());
    CHECK(&v[2] == v.data() + 2);
    CHECK(v.at(1) == V(2));
    bool threw = false;
    try { (void) v.at(N); } catch (std::out_of_range const &) { threw = true; }
    CHECK(threw);

    IV const &cv = v;
    CHECK(cv[0] == V(1));
    CHECK(cv.front() == V(1));
    CHECK(cv.back() == V(3));
    CHECK(cv.at(0) == V(1));
  }

  // ---- iterators (forward + reverse + mutable) --------------------------
  {
    IV v;
    for (int i = 1; i <= 5; ++i) v.push_back(V(i));
    std::size_t i = 0;
    for (auto it = v.begin(); it != v.end(); ++it, ++i) CHECK(*it == v[i]);
    i = v.size();
    for (auto it = v.rbegin(); it != v.rend(); ++it) { --i; CHECK(*it == v[i]); }
    for (auto &e : v) { (void) e; }
    *v.begin() = V(42);
    CHECK(v[0] == V(42));
  }

  // ---- unchecked_emplace_back -------------------
  {
    IV v;
    v.unchecked_emplace_back(V(7));
    v.unchecked_emplace_back(V(8));
    CHECK(v.size() == 2 && v[0] == V(7) && v[1] == V(8));
    T &last = v.unchecked_emplace_back(V(9));
    CHECK(&last == &v[2]);
    T a = V(10);
    v.unchecked_push_back(a);     // unchecked_push_back(const T&)
    v.unchecked_push_back(V(11)); // unchecked_push_back(T&&)
    CHECK(v.size() == 5 && v[3] == V(10) && v[4] == V(11));
  }

  // ---- try_push_back / try_emplace_back ---------------------------------
  {
    IV v;
    CHECK(try_ok(v.try_push_back(V(1))));
    CHECK(try_ok(v.try_emplace_back(V(2))));
    CHECK(v.size() == 2 && v[1] == V(2));
    while (v.size() < N) v.push_back(V(0));
    CHECK(!try_ok(v.try_push_back(V(3))));
    CHECK(!try_ok(v.try_emplace_back(V(3))));
    CHECK(v.size() == N);
  }

  // ---- insert single / emplace(pos) -------------------------------------
  {
    IV v { V(1), V(2), V(3) };
    auto it = v.insert(v.begin() + 1, V(9));
    CHECK(v.size() == 4 && v[0] == V(1) && v[1] == V(9) && v[2] == V(2) && v[3] == V(3));
    CHECK(it == v.begin() + 1);
    auto it2 = v.emplace(v.begin(), V(0));
    CHECK(v.size() == 5 && v[0] == V(0) && it2 == v.begin());
    auto it3 = v.insert(v.end(), V(8));
    CHECK(v.size() == 6 && v[5] == V(8) && it3 == v.end() - 1);
  }

  // ---- insert(count, value) ---------------------------------------------
  {
    IV v { V(1), V(2), V(3) };
    auto it = v.insert(v.begin() + 1, std::size_t(2), V(7));
    CHECK(v.size() == 5);
    CHECK(v[0] == V(1) && v[1] == V(7) && v[2] == V(7) && v[3] == V(2) && v[4] == V(3));
    CHECK(it == v.begin() + 1);
    // insert 0 is a no-op returning position
    auto it0 = v.insert(v.begin() + 2, std::size_t(0), V(4));
    CHECK(v.size() == 5 && it0 == v.begin() + 2);
  }

  // ---- insert(count, value) aliasing the shifted element ----------------
  {
    IV v { V(1), V(2), V(3), V(4), V(5) };
    v.insert(v.begin() + 1, std::size_t(1), v[2]); // v[2] sits in the shifted overlap
    CHECK(v.size() == 6);
    CHECK(v[0] == V(1) && v[1] == V(3) && v[2] == V(2) && v[3] == V(3) && v[4] == V(4) && v[5] == V(5));
  }

  // ---- insert(range) ----------------------------------------------------
  {
    IV v { V(1), V(2), V(3) };
    std::vector<T> src { V(7), V(8) };
    auto it = v.insert(v.begin() + 1, src.begin(), src.end());
    CHECK(v.size() == 5 && it == v.begin() + 1);
    CHECK(v[0] == V(1) && v[1] == V(7) && v[2] == V(8) && v[3] == V(2) && v[4] == V(3));
    // empty range: no-op, returns position
    std::vector<T> empty;
    auto it2 = v.insert(v.end(), empty.begin(), empty.end());
    CHECK(v.size() == 5 && it2 == v.end());
  }

  // ---- insert(initializer_list) / insert_range --------------------------
  {
    IV v { V(1), V(2) };
    auto it = v.insert(v.begin() + 1, { V(5), V(6) });
    CHECK(v.size() == 4 && it == v.begin() + 1);
    CHECK(v[0] == V(1) && v[1] == V(5) && v[2] == V(6) && v[3] == V(2));

    std::vector<T> src { V(8), V(9) };
    v.insert_range(v.end(), src);
    CHECK(v.size() == 6 && v[4] == V(8) && v[5] == V(9));
  }

  // ---- append_range / assign_range -------------------------------------
  {
    IV v { V(1) };
    std::vector<T> src { V(2), V(3), V(4) };
    v.append_range(src);
    CHECK(v.size() == 4 && v[3] == V(4));

    v.assign_range(src);
    CHECK(v.size() == 3 && v[0] == V(2) && v[2] == V(4));
  }

  // ---- resize (grow / shrink) + resize(count, value) --------------------
  {
    IV v { V(1), V(2) };
    v.resize(5);
    CHECK(v.size() == 5);
    CHECK(v[0] == V(1) && v[1] == V(2)); // prefix preserved; appended are value-initialised
    v.resize(7, V(9));
    CHECK(v.size() == 7 && v[5] == V(9) && v[6] == V(9));
    v.resize(1);
    CHECK(v.size() == 1 && v[0] == V(1));
    // resize to same size: no-op
    v.resize(1);
    CHECK(v.size() == 1);
    bool threw = false;
    try { v.resize(N + 1); } catch (std::bad_alloc const &) { threw = true; }
    CHECK(threw);
  }

  // ---- assign(count,value) / assign(range) / assign(ilist) --------------
  {
    IV v { V(1), V(2), V(3) };
    v.assign(std::size_t(4), V(7));
    CHECK(v.size() == 4 && v[0] == V(7) && v[3] == V(7));
    std::vector<T> src { V(5), V(6) };
    v.assign(src.begin(), src.end());
    CHECK(v.size() == 2 && v[0] == V(5) && v[1] == V(6));
    v.assign({ V(8), V(9), V(0) });
    CHECK(v.size() == 3 && v[0] == V(8) && v[2] == V(0));
    bool threw = false;
    try { v.assign(std::size_t(N + 1), V(1)); } catch (std::bad_alloc const &) { threw = true; }
    CHECK(threw);
  }

  // ---- erase(position) / erase(first,last) / pop_back / clear -----------
  {
    IV v;
    for (int i = 1; i <= 6; ++i) v.push_back(V(i)); // 1..6
    auto it = v.erase(v.begin() + 1);               // remove 2
    CHECK(v.size() == 5 && v[1] == V(3) && it == v.begin() + 1);
    auto it2 = v.erase(v.begin() + 1, v.begin() + 3); // remove 3,4
    CHECK(v.size() == 3 && v[1] == V(5) && it2 == v.begin() + 1);
    v.pop_back();
    CHECK(v.size() == 2 && v[1] == V(5));
    // erase of an EMPTY range on a NON-empty container must be a no-op
    auto itE = v.erase(v.begin() + 1, v.begin() + 1);
    CHECK(v.size() == 2 && itE == v.begin() + 1 && v[0] == V(1) && v[1] == V(5));
    v.clear();
    CHECK(v.empty());
    // erase empty range is a no-op
    auto it3 = v.erase(v.begin(), v.begin());
    CHECK(v.empty() && it3 == v.begin());
  }

  // ---- swap -------------------------------------------------------------
  {
    IV a { V(1), V(2), V(3) };
    IV b { V(7) };
    a.swap(b);
    CHECK(a.size() == 1 && a[0] == V(7));
    CHECK(b.size() == 3 && b[0] == V(1) && b[2] == V(3));
    swap(a, b); // ADL friend
    CHECK(a.size() == 3 && a[0] == V(1));
    a.swap(a); // self-swap
    CHECK(a.size() == 3 && a[0] == V(1));
  }

  // ---- copy / move construction & assignment ----------------------------
  {
    IV v { V(1), V(2), V(3) };
    IV c = v;
    CHECK(c == v && c.size() == 3);
    IV m = std::move(c);
    CHECK(m == v);
    IV a;
    a = v;
    CHECK(a == v);
    IV b;
    b = std::move(a);
    CHECK(b == v);
  }

  // ---- comparison operators --------------------------------------------
  {
    IV a { V(1), V(2), V(3) };
    IV b { V(1), V(2), V(3) };
    IV c { V(1), V(2), V(4) };
    IV d { V(1), V(2) };
    CHECK(a == b);
    CHECK(!(a == c));
    CHECK(a != c);
    CHECK(a < c);
    CHECK(c > a);
    CHECK(a <= b);
    CHECK(b >= a);
    CHECK(d < a);              // prefix is smaller
    CHECK((a <=> b) == 0);
    CHECK((a <=> c) < 0);
  }

  // ---- lvalue overloads, ilist assignment, const iterators, from_range ---
  {
    IV v;
    T x = V(4);
    v.push_back(x); // push_back(const_reference)
    T y = V(5);
    v.insert(v.begin(), y); // insert(const_iterator, const_reference)
    CHECK(v.size() == 2 && v[0] == V(5) && v[1] == V(4));
    v = { V(1), V(2), V(3) }; // operator=(initializer_list)
    CHECK(v.size() == 3 && v[2] == V(3));
    CHECK(v.cbegin() == v.begin() && v.cend() == v.end());
    CHECK(v.crbegin() == v.rbegin() && v.crend() == v.rend());
#ifdef __cpp_lib_containers_ranges
    std::vector<T> src { V(7), V(8) };
    IV fr { std::from_range, src };
    CHECK(fr.size() == 2 && fr[1] == V(8));
#endif
  }

  // ---- free erase / erase_if are covered in test_free_erasure() ---------
}

template <class V>
std::vector<int> ints(V const &v)
{
  std::vector<int> r;
  for (auto const &e : v) r.push_back(e.value);
  return r;
}

// Dedicated coverage for element types that are trivially movable but not
// trivially copyable (Relocatable) or trivially copy-constructible but not
// trivially copyable (NonTrivialCopyAssign). These exercise the relocation /
// copy fast paths. Comparisons go through .value so we avoid the standard
// library's vectorised equal/find paths, which are ill-suited to 4-byte
// non-trivially-copyable types.
template <class T>
void test_value_type_fastpaths()
{
  using IV = stdex::inplace_vector<T, 16>;

  IV v;
  v.emplace_back(1);
  v.push_back(T { 2 });
  v.emplace_back(3);
  CHECK(ints(v) == (std::vector<int> { 1, 2, 3 }));

  v.insert(v.begin() + 1, T { 9 });
  CHECK(ints(v) == (std::vector<int> { 1, 9, 2, 3 }));

  v.insert(v.begin(), std::size_t(2), T { 7 }); // relocation fast path + fill
  CHECK(ints(v) == (std::vector<int> { 7, 7, 1, 9, 2, 3 }));

  v.emplace(v.end(), 8);
  CHECK(ints(v).back() == 8);

  std::vector<T> src { T { 5 }, T { 6 } };
  v.insert(v.begin() + 1, src.begin(), src.end());
  CHECK(ints(v)[0] == 7 && ints(v)[1] == 5 && ints(v)[2] == 6);

  v.erase(v.begin()); // member erase (std::move based)
  CHECK(ints(v)[0] == 5 && ints(v)[1] == 6);

  v.resize(2);
  CHECK(v.size() == 2);
  v.resize(4, T { 0 });
  CHECK(v.size() == 4 && ints(v)[3] == 0);

  v.assign(std::size_t(3), T { 4 });
  CHECK(ints(v) == (std::vector<int> { 4, 4, 4 }));

  v.pop_back();
  CHECK(v.size() == 2);

  IV a { T { 1 }, T { 2 } };
  IV b { T { 9 } };
  a.swap(b);
  CHECK(ints(a) == (std::vector<int> { 9 }));
  CHECK(ints(b) == (std::vector<int> { 1, 2 }));

  IV c = b;
  CHECK(ints(c) == ints(b));
  IV d = std::move(c);
  CHECK(ints(d) == (std::vector<int> { 1, 2 }));

  v.clear();
  CHECK(v.empty());
}

void test_free_erasure()
{
  {
    stdex::inplace_vector<int, 16> v;
    for (int i : { 1, 2, 2, 3, 2 }) v.push_back(i);
    CHECK(stdex::erase(v, 2) == 3);
    CHECK(v.size() == 2 && v[0] == 1 && v[1] == 3);
    for (int i : { 5, 6, 7, 8 }) v.push_back(i);
    CHECK(stdex::erase_if(v, [](int e) { return e == 6 || e == 8; }) == 2);
    CHECK(v.size() == 4);
  }
  {
    stdex::inplace_vector<std::string, 16> v;
    for (auto const *s : { "a", "b", "b", "c" }) v.push_back(s);
    CHECK(stdex::erase(v, std::string("b")) == 2);
    CHECK(v.size() == 2 && v[0] == "a" && v[1] == "c");
    v.push_back("dd");
    CHECK(stdex::erase_if(v, [](std::string const &e) { return e.size() == 2; }) == 1);
  }
}

// ===========================================================================
// N == 0 specialization
// ===========================================================================
template <class T>
void test_zero_capacity()
{
  using IV = stdex::inplace_vector<T, 0>;
  IV v;
  CHECK(v.empty());
  CHECK(v.size() == 0);
  CHECK(IV::capacity() == 0);
  CHECK(IV::max_size() == 0);
  CHECK(v.data() == nullptr);
  CHECK(v.begin() == v.end());
  CHECK(v.rbegin() == v.rend());
  CHECK(!try_ok(v.try_push_back(T {})));
  v.clear();
  v.shrink_to_fit();
  v.swap(v);

  auto expect_throw = [](auto &&fn) {
    try { fn(); } catch (std::bad_alloc const &) { return true; } catch (...) { return false; }
    return false;
  };
  CHECK(expect_throw([&] { v.push_back(T {}); }));
  CHECK(expect_throw([&] { v.emplace_back(T {}); }));
  CHECK(expect_throw([&] { v.reserve(1); }));
  CHECK(expect_throw([&] { v.resize(1); }));
  CHECK(expect_throw([&] { v.assign(std::size_t(1), T {}); }));
  CHECK(expect_throw([&] { (void) v.insert(v.begin(), T {}); }));
  CHECK(expect_throw([&] { (void) v.insert(v.begin(), std::size_t(1), T {}); }));
  // empty-range ops are no-ops
  std::vector<T> empty;
  v.assign(empty.begin(), empty.end());
  v.append_range(empty);
  CHECK(v.insert(v.begin(), empty.begin(), empty.end()) == v.begin());

  IV c = v;
  CHECK(c == v);
  IV m = std::move(c);
  CHECK(m == v);
  CHECK(stdex::erase(v, T {}) == 0);
  CHECK(stdex::erase_if(v, [](T const &) { return true; }) == 0);
}

// ===========================================================================
// Boundary tests on tiny capacities (int): full, one-in/one-out.
// ===========================================================================
template <std::size_t N>
void test_small_capacity()
{
  using IV = stdex::inplace_vector<int, N>;
  IV v;
  for (int i = 0; i < static_cast<int>(N); ++i) {
    CHECK(try_ok(v.try_push_back(i)));
  }
  CHECK(v.size() == N);
  CHECK(!try_ok(v.try_push_back(99)));
  auto threw = [](auto &&fn) {
    try { fn(); } catch (std::bad_alloc const &) { return true; } catch (...) { return false; }
    return false;
  };
  CHECK(threw([&] { v.push_back(99); }));
  CHECK(threw([&] { v.emplace_back(99); }));
  CHECK(threw([&] { (void) v.insert(v.begin(), 99); }));
  CHECK(threw([&] { (void) v.insert(v.begin(), std::size_t(2), 99); }));
  CHECK(threw([&] { v.resize(N + 1); }));
  CHECK(threw([&] { v.assign(std::size_t(N + 1), 1); }));
  CHECK(threw([&] { v.reserve(N + 1); }));
  CHECK(v.size() == N); // unchanged after all failures

  bool oor = false;
  try { (void) v.at(N); } catch (std::out_of_range const &) { oor = true; }
  CHECK(oor);
  CHECK(v.at(N - 1) == static_cast<int>(N) - 1);

  // shrink to empty via erase range
  v.erase(v.begin(), v.end());
  CHECK(v.empty());
}

// ===========================================================================
// Non-default-constructible and move-only element types.
// ===========================================================================
void test_no_default()
{
  stdex::inplace_vector<NoDefault, 4> v;
  v.emplace_back(5);
  v.emplace_back(6);
  v.emplace_back(7);
  CHECK(v.size() == 3 && v[0].value == 5 && v[2].value == 7);
  v.emplace(v.begin() + 1, 9);
  CHECK(v.size() == 4 && v[1].value == 9);
  bool threw = false;
  try { v.insert(v.begin(), NoDefault { 1 }); } catch (std::bad_alloc const &) { threw = true; }
  CHECK(threw); // full
  v.erase(v.begin());
  CHECK(v.size() == 3 && v[0].value == 9);
  v.pop_back();
  CHECK(v.size() == 2);
  v.clear();
  CHECK(v.empty());
}

void test_move_only()
{
  using P = std::unique_ptr<int>;
  stdex::inplace_vector<P, 5> v;
  v.emplace_back(new int(1));
  v.push_back(std::make_unique<int>(2));
  v.emplace(v.begin() + 1, new int(9));
  CHECK(v.size() == 3);
  CHECK(*v[0] == 1 && *v[1] == 9 && *v[2] == 2);
  v.insert(v.begin(), std::make_unique<int>(0));
  CHECK(v.size() == 4 && *v[0] == 0);
  v.erase(v.begin());
  CHECK(v.size() == 3 && *v[0] == 1);
  v.pop_back();
  CHECK(v.size() == 2);

  stdex::inplace_vector<P, 5> w;
  w.emplace_back(new int(7));
  v.swap(w);
  CHECK(v.size() == 1 && *v[0] == 7);
  CHECK(w.size() == 2 && *w[0] == 1);
  v.clear();
  w.clear();
}

// ===========================================================================
// Exception safety / no-leak checks (strong guarantee on resize; basic on
// element-throwing insert etc.).
// ===========================================================================
void test_exception_safety()
{
  using V = stdex::inplace_vector<Counted, 8>;

  // resize(count): strong guarantee — size and live count restored on throw.
  {
    Counted::live = 0; Counted::budget = -1;
    V v; v.resize(2);
    int live0 = Counted::live, size0 = static_cast<int>(v.size());
    Counted::budget = 1; // one more success, then throw
    bool threw = false;
    try { v.resize(5); } catch (std::runtime_error const &) { threw = true; }
    Counted::budget = -1;
    CHECK(threw);
    CHECK(v.size() == static_cast<std::size_t>(size0));
    CHECK(Counted::live == live0); // no leaked partial elements
  }
  CHECK(Counted::live == 0);

  // resize(count, value): strong guarantee.
  {
    Counted::live = 0; Counted::budget = -1;
    V v; v.resize(2);
    Counted val;
    int live0 = Counted::live, size0 = static_cast<int>(v.size());
    Counted::budget = 1;
    bool threw = false;
    try { v.resize(5, val); } catch (std::runtime_error const &) { threw = true; }
    Counted::budget = -1;
    CHECK(threw);
    CHECK(v.size() == static_cast<std::size_t>(size0));
    CHECK(Counted::live == live0);
  }
  CHECK(Counted::live == 0);

  // push_back/emplace_back on a full vector: throws, no effect.
  {
    Counted::live = 0; Counted::budget = -1;
    V v; for (int i = 0; i < 8; ++i) v.emplace_back(i);
    int live0 = Counted::live;
    bool threw = false;
    try { v.push_back(Counted(9)); } catch (std::bad_alloc const &) { threw = true; }
    CHECK(threw);
    CHECK(v.size() == 8 && Counted::live == live0);
  }
  CHECK(Counted::live == 0);

  // copy-construction that throws must not leak.
  {
    Counted::live = 0; Counted::budget = -1;
    V v; v.emplace_back(1); v.emplace_back(2);
    Counted::budget = 1;
    bool threw = false;
    try { V copy { v }; (void) copy; } catch (std::runtime_error const &) { threw = true; }
    Counted::budget = -1;
    CHECK(threw);
    CHECK(Counted::live == 2); // the tail of the failed copy was destroyed
  }
  CHECK(Counted::live == 0);
}

// ===========================================================================
// Fast-path / type-conversion checks: contiguous POD ranges (int/long/unsigned)
// and cross value-type ranges must convert, not blindly bit-copy.
// ===========================================================================
void test_fast_path_and_conversions()
{
  // POD contiguous range (int -> int): every method that copies a contiguous
  // range exercises the memcpy fast path.
  {
    int src[5] = { 1, 2, 3, 4, 5 };
    stdex::inplace_vector<int, 8> a(src, src + 5);
    CHECK(a.size() == 5 && a[4] == 5);
    stdex::inplace_vector<int, 8> b;
    b.append_range(std::ranges::subrange(src, src + 5));
    CHECK(b.size() == 5 && b[2] == 3);
    stdex::inplace_vector<int, 8> c { 9 };
    c.insert(c.begin(), src, src + 5);
    CHECK(c.size() == 6 && c[0] == 1 && c[5] == 9);
    c.assign(src, src + 5);
    CHECK(c.size() == 5 && c[0] == 1 && c[4] == 5);
  }

  // Cross value-type range must convert element-wise (not raw memcpy of a
  // different type). long/unsigned from int.
  {
    std::vector<int> const src { 1, 2, 3 };
    stdex::inplace_vector<long, 8> v;
    v.assign(src.begin(), src.end());
    CHECK(v.size() == 3 && v[0] == 1L && v[2] == 3L);
    v.clear();
    v.append_range(src);
    CHECK(v.size() == 3 && v[1] == 2L);
    v.clear();
    v.insert(v.begin(), src.begin(), src.end());
    CHECK(v.size() == 3 && v[2] == 3L);

    std::vector<int> const neg { -1, 300 };
    stdex::inplace_vector<unsigned, 8> u;
    u.assign(neg.begin(), neg.end());
    CHECK(u.size() == 2 && u[0] == static_cast<unsigned>(-1) && u[1] == 300u);
  }

  // A relocatable-but-not-copyable element type exercised by insert/erase/swap:
  // backs onto the "iter_move_cat" relocation fast path.
  {
    stdex::inplace_vector<Relocatable, 8> v { Relocatable(1), Relocatable(2), Relocatable(3) };
    v.insert(v.begin() + 1, Relocatable(9));
    CHECK(v.size() == 4 && v[1].value == 9 && v[3].value == 3);
    stdex::inplace_vector<Relocatable, 8> w { Relocatable(7), Relocatable(8) };
    v.swap(w);
    CHECK(v.size() == 2 && v[0].value == 7 && w.size() == 4 && w[1].value == 9);
  }
}

// ===========================================================================
// Randomized fuzz: drive inplace_vector with a random op stream and compare to a
// std::vector reference model after every step. Ops stay within capacity;
// overflow is exercised separately (must throw + leave container unchanged).
// ===========================================================================
template <class T, std::size_t N>
void fuzz(std::uint32_t seed, int iters)
{
  std::mt19937 rng(seed);
  auto V = [](int x) { return make_value<T>(x); };
  stdex::inplace_vector<T, N> vec;
  std::vector<T> ref;

  auto verify = [&](char const *where) {
    if (!matches(vec, ref)) {
      std::printf("FUZZ MISMATCH seed=%u at %s: vec=%zu ref=%zu\n", seed, where, vec.size(), ref.size());
      ++g_fail;
      return false;
    }
    if (vec.size() > N) { ++g_fail; return false; }
    return true;
  };

  for (int step = 0; step < iters; ++step) {
    unsigned const op = static_cast<unsigned>(rng() % 14u);
    int const val     = static_cast<int>(rng() % 100);
    switch (op) {
      case 0: // push_back
        if (vec.size() < N) { vec.push_back(V(val)); ref.push_back(V(val)); }
        break;
      case 1: // emplace_back
        if (vec.size() < N) { vec.emplace_back(V(val)); ref.push_back(V(val)); }
        break;
      case 2: // pop_back
        if (!ref.empty()) { vec.pop_back(); ref.pop_back(); }
        break;
      case 3: { // insert single
        std::size_t p = ref.empty() ? 0 : rng() % (ref.size() + 1);
        if (vec.size() < N) {
          vec.insert(vec.begin() + static_cast<std::ptrdiff_t>(p), V(val));
          ref.insert(ref.begin() + static_cast<std::ptrdiff_t>(p), V(val));
        }
        break;
      }
      case 4: { // insert(count, value)
        std::size_t room = N - vec.size();
        std::size_t n    = room == 0 ? 0 : rng() % (room + 1);
        std::size_t p    = ref.empty() ? 0 : rng() % (ref.size() + 1);
        vec.insert(vec.begin() + static_cast<std::ptrdiff_t>(p), n, V(val));
        ref.insert(ref.begin() + static_cast<std::ptrdiff_t>(p), n, V(val));
        break;
      }
      case 5: { // erase single
        if (!ref.empty()) {
          std::size_t p = rng() % ref.size();
          vec.erase(vec.begin() + static_cast<std::ptrdiff_t>(p));
          ref.erase(ref.begin() + static_cast<std::ptrdiff_t>(p));
        }
        break;
      }
      case 6: { // erase range
        if (!ref.empty()) {
          std::size_t a = rng() % ref.size();
          std::size_t b = a + (rng() % (ref.size() - a + 1));
          vec.erase(vec.begin() + static_cast<std::ptrdiff_t>(a), vec.begin() + static_cast<std::ptrdiff_t>(b));
          ref.erase(ref.begin() + static_cast<std::ptrdiff_t>(a), ref.begin() + static_cast<std::ptrdiff_t>(b));
        }
        break;
      }
      case 7: { // resize(n)
        std::size_t n = rng() % (N + 1);
        vec.resize(n);
        ref.resize(n);
        break;
      }
      case 8: { // resize(n, val)
        std::size_t n = rng() % (N + 1);
        vec.resize(n, V(val));
        ref.resize(n, V(val));
        break;
      }
      case 9: { // assign(n, val)
        std::size_t n = rng() % (N + 1);
        vec.assign(n, V(val));
        ref.assign(n, V(val));
        break;
      }
      case 10: { // assign(range)
        std::size_t n = rng() % (N + 1);
        std::vector<T> tmp;
        for (std::size_t i = 0; i < n; ++i) tmp.push_back(V(static_cast<int>(i)));
        vec.assign(tmp.begin(), tmp.end());
        ref.assign(tmp.begin(), tmp.end());
        break;
      }
      case 11: // clear
        vec.clear();
        ref.clear();
        break;
      case 12: { // append_range
        std::size_t room = N - vec.size();
        std::size_t n    = room == 0 ? 0 : rng() % (room + 1);
        std::vector<T> tmp;
        for (std::size_t i = 0; i < n; ++i) tmp.push_back(V(val));
        vec.append_range(tmp);
        ref.insert(ref.end(), tmp.begin(), tmp.end());
        break;
      }
      case 13: { // try_push_back (bounded by capacity)
        bool const fits = ref.size() < N;
        auto r = vec.try_push_back(V(val));
        if (fits) {
          CHECK(try_ok(r));
          ref.push_back(V(val));
        } else {
          CHECK(!try_ok(r));
        }
        break;
      }
    }
    if (!verify("step")) break;
  }

  // Overflow boundary: when full, push_back / try_push_back must throw / report
  // nullopt and leave the container untouched.
  vec.clear(); ref.clear();
  while (vec.size() < N) { vec.push_back(V(0)); ref.push_back(V(0)); }
  bool threw = false;
  try { vec.push_back(V(1)); } catch (std::bad_alloc const &) { threw = true; }
  CHECK(threw);
  CHECK(!try_ok(vec.try_push_back(V(1))));
  verify("overflow");
}

// ===========================================================================
// Compile-time (constexpr) coverage for a trivially-copyable element type.
// ===========================================================================
namespace constexpr_checks
{
[[maybe_unused]] constexpr bool drive()
{
  using IV = stdex::inplace_vector<int, 16>;
  IV v;
  if (!v.empty() || v.size() != 0) return false;
  if (IV::capacity() != 16 || IV::max_size() != 16) return false;

  v.push_back(1);
  v.emplace_back(2);
  v.push_back(3);
  if (v.size() != 3 || v[0] != 1 || v[2] != 3) return false;
  if (v.front() != 1 || v.back() != 3 || v.data()[1] != 2) return false;
  if (v.at(1) != 2) return false;

  { int s = 0; for (int x : v) s += x; if (s != 6) return false; }
  { int s = 0; for (auto it = v.rbegin(); it != v.rend(); ++it) s += *it; if (s != 6) return false; }

  v.insert(v.begin() + 1, 9);    // 1 9 2 3
  v.emplace(v.begin(), 0);       // 0 1 9 2 3
  v.insert(v.begin() + 2, std::size_t(2), 7); // 0 1 7 7 9 2 3
  if (!(v.size() == 7 && v[0] == 0 && v[2] == 7 && v[3] == 7)) return false;
  int arr[2] = { 8, 8 };
  v.insert(v.begin(), arr, arr + 2); // 8 8 0 1 7 7 9 2 3
  if (!(v.size() == 9 && v[0] == 8 && v[1] == 8)) return false;
  v.insert(v.end(), { 4, 5 }); // 8 8 0 1 7 7 9 2 3 4 5
  if (!(v.size() == 11 && v[9] == 4 && v[10] == 5)) return false;

  IV w;
  int a2[3] = { 1, 2, 3 };
  w.append_range(std::ranges::subrange(a2, a2 + 3));
  if (w.size() != 3 || w[2] != 3) return false;

  w.resize(5);
  if (w.size() != 5 || w[3] != 0 || w[4] != 0) return false;
  w.resize(7, 4);
  if (w.size() != 7 || w[6] != 4) return false;
  w.resize(2);
  if (w.size() != 2 || w[1] != 2) return false;

  w.assign(std::size_t(3), 6);
  if (w.size() != 3 || w[0] != 6 || w[2] != 6) return false;
  int a3[2] = { 11, 12 };
  w.assign(a3, a3 + 2);
  if (w.size() != 2 || w[1] != 12) return false;
  w.assign({ 1, 2, 3, 4 });
  if (w.size() != 4 || w[3] != 4) return false;

  w.erase(w.begin());
  if (w.size() != 3 || w[0] != 2) return false;
  w.erase(w.begin(), w.begin() + 2);
  if (w.size() != 1) return false;
  w.push_back(9);
  w.pop_back();
  if (w.size() != 1) return false;
  w.clear();
  if (!w.empty()) return false;

  IV x { 1, 2, 3 };
  IV y { 7 };
  x.swap(y);
  if (x.size() != 1 || x[0] != 7 || y.size() != 3 || y[2] != 3) return false;
  swap(x, y);
  if (x.size() != 3 || x[0] != 1) return false;

  IV z;
  if (!try_ok(z.try_push_back(5)) || z.size() != 1) return false;

  IV p { 1, 2, 3 };
  IV q { 1, 2, 4 };
  if (!(p < q) || (p == q) || (q <= p)) return false;
  if (!((p <=> q) < 0)) return false;

  return true;
}
} // namespace constexpr_checks

// Constant evaluation of the union-backed inplace_storage requires either a
// compiler that starts the union member's lifetime implicitly in constant
// expressions (MSVC) or C++26 std::start_lifetime; strict compilers reject it
// without the latter.
#if (defined(_MSC_VER) && !defined(__clang__)) || (defined(__cpp_lib_start_lifetime) && __cpp_lib_start_lifetime >= 202603L)
static_assert(constexpr_checks::drive(), "inplace_vector<int,16> must be fully usable in constant evaluation");
#endif

// ---------------------------------------------------------------------------
// P1 regression: every container path that ends element lifetimes must stay
// usable in constant evaluation for a trivially destructible T.
//
// The elements of inplace_vector<int, N> are sub-objects of a std::array; the
// array's own destructor already "destroys" them, so the container must NOT call
// std::destroy/std::destroy_at on them. MSVC's std::destroy(&[first,last)) ends
// the sub-object lifetimes even for trivial T while constant-evaluating, and the
// later std::array destruction then trips "destroying an object that has already
// been destroyed" (C2131). Routing every destroy site through
// `if constexpr (!is_trivially_destructible_v<T>)` fixed it; these asserts fail
// to compile if any site regresses to an unconditional std::destroy.
// ---------------------------------------------------------------------------
#if (defined(_MSC_VER) && !defined(__clang__)) || (defined(__cpp_lib_start_lifetime) && __cpp_lib_start_lifetime >= 202603L)
namespace constexpr_destroy_checks
{
constexpr bool drive()
{
  using IV = stdex::inplace_vector<int, 8>;

  { // clear() -> destroy(data(), data()+size())
    IV v;
    for (int i = 0; i < 5; ++i) v.push_back(i);
    v.clear();
    if (!v.empty()) return false;
  }
  { // pop_back() -> destroy_at(last)
    IV v;
    for (int i = 0; i < 5; ++i) v.emplace_back(i);
    v.pop_back();
    if (v.size() != 4 || v[3] != 3) return false;
  }
  { // erase(pos) / erase(first,last) -> destroy(tail)
    IV v;
    for (int i = 0; i < 6; ++i) v.push_back(i);
    v.erase(v.begin() + 1);
    if (v.size() != 5 || v[1] != 2) return false;
    v.erase(v.begin() + 1, v.begin() + 3);
    if (v.size() != 3 || v[1] != 4) return false;
  }
  { // resize(smaller) -> destroy(tail)
    IV v;
    for (int i = 0; i < 6; ++i) v.push_back(i);
    v.resize(2);
    if (v.size() != 2) return false;
  }
  { // assign(count, value) shrinking -> destroy(tail)
    IV v;
    for (int i = 0; i < 6; ++i) v.push_back(i);
    v.assign(std::size_t(2), 9);
    if (v.size() != 2 || v[1] != 9) return false;
  }
  { // assign(first, last) shrinking (forward) -> destroy(tail)
    IV v;
    for (int i = 0; i < 6; ++i) v.push_back(i);
    int src[3] = { 7, 8, 9 };
    v.assign(src, src + 3);
    if (v.size() != 3 || v[2] != 9) return false;
  }
  { // swap of unequal sizes -> destroy(tail of the larger)
    IV a;
    for (int i = 0; i < 4; ++i) a.push_back(i);
    IV b;
    b.push_back(9);
    a.swap(b);
    if (a.size() != 1 || a[0] != 9 || b.size() != 4 || b[3] != 3) return false;
  }
  return true;
}
} // namespace constexpr_destroy_checks
static_assert(constexpr_destroy_checks::drive(), "destroy paths must be usable in constant evaluation for trivial T (P1)");
#endif


// ---------------------------------------------------------------------------
// Reserved: non-POD constexpr usage (C++26).
//
// Prior to C++26 the specification only guarantees constant-expression use of
// inplace_vector for trivially-copyable, trivially-default-constructible T.
// Under C++26 (constexpr allocation / non-trivial literal types) a
// non-trivially-copyable element type should also be usable in constant
// expressions; keep this block behind `#if __cplusplus >= 202603L` so it only
// compiles where that is supported.
// ---------------------------------------------------------------------------
#if __cplusplus >= 202603L
namespace constexpr_nontrivial
{
// Non-trivially-copyable (user-provided copy ctor + dtor) but a literal type.
struct NP
{
  int v;
  constexpr NP() : v { 0 } {}
  constexpr NP(int x) : v { x } {}
  constexpr NP(NP const &o) : v { o.v } {}
  constexpr NP &operator=(NP const &o) { v = o.v; return *this; }
  constexpr ~NP() {}
  friend constexpr auto operator<=>(NP const &, NP const &) = default;
};

constexpr bool drive()
{
  stdex::inplace_vector<NP, 8> v;
  v.emplace_back(1);
  v.push_back(NP { 2 });
  v.insert(v.begin() + 1, NP { 9 }); // 1 9 2
  if (!(v.size() == 3 && v[1].v == 9)) return false;
  v.erase(v.begin());
  if (!(v.size() == 2 && v[0].v == 9)) return false;
  v.resize(4, NP { 7 });
  if (!(v.size() == 4 && v[3].v == 7)) return false;
  v.clear();
  return v.empty();
}
} // namespace constexpr_nontrivial
static_assert(constexpr_nontrivial::drive(), "non-POD constexpr use requires C++26");
#endif // __cplusplus >= 202603L

// ---- triviality / type-trait guarantees that the implementation provides ---
static_assert(std::is_trivially_copyable_v<stdex::inplace_vector<int, 8>>);
static_assert(std::is_trivially_move_constructible_v<stdex::inplace_vector<int, 8>>);
static_assert(std::is_trivially_destructible_v<stdex::inplace_vector<int, 8>>);
static_assert(std::is_trivially_copyable_v<stdex::inplace_vector<Trivial, 4>>);

// N == 0: trivially copyable, empty, trivially default constructible.
static_assert(std::is_trivially_copyable_v<stdex::inplace_vector<std::string, 0>>);
static_assert(std::is_empty_v<stdex::inplace_vector<std::string, 0>>);
static_assert(std::is_trivially_default_constructible_v<stdex::inplace_vector<int, 0>>);

// Non-trivial element type: container SMFs are non-trivial.
static_assert(!std::is_trivially_copyable_v<stdex::inplace_vector<std::string, 4>>);
static_assert(!std::is_trivially_destructible_v<stdex::inplace_vector<std::string, 4>>);

// noexcept specs mirror MSVC std::vector's strengthened members.
using IVint = stdex::inplace_vector<int, 8>;
static_assert(noexcept(std::declval<IVint &>().erase(std::declval<IVint::const_iterator>())));
static_assert(noexcept(std::declval<IVint &>().erase(std::declval<IVint::const_iterator>(),
                                                     std::declval<IVint::const_iterator>())));
static_assert(noexcept(std::declval<IVint &>().pop_back()));
static_assert(noexcept(std::declval<IVint &>().clear()));

// NOTE(spec [inplace.vector.overview]/5.2, /5.3): for a T that is trivially
// move constructible and trivially destructible (but not trivially copyable),
// such as Relocatable, IV must expose a trivial move constructor and a trivial
// destructor. The storage layer declares its copy/move SMFs explicitly so the
// implicit move constructor is not suppressed by the destructor declation.
static_assert(std::is_trivially_destructible_v<stdex::inplace_vector<Relocatable, 4>>);
static_assert(std::is_trivially_move_constructible_v<stdex::inplace_vector<Relocatable, 4>>);

// Runtime counterpart of the P1 constexpr checks above: prove that the
// `if constexpr (!is_trivially_destructible_v<T>)` guards do NOT skip real
// destruction for a non-trivially-destructible element type (Counted tracks
// live objects). After each shrink operation `Counted::live` must equal the
// number of elements still owned.
void test_destroy_paths()
{
  using V = stdex::inplace_vector<Counted, 16>;
  Counted::live   = 0;
  Counted::budget = -1;

  { // clear()
    V v;
    for (int i = 0; i < 6; ++i) v.emplace_back(i);
    CHECK(Counted::live == 6);
    v.clear();
    CHECK(v.empty() && Counted::live == 0);
  }
  CHECK(Counted::live == 0);

  { // pop_back()
    V v;
    for (int i = 0; i < 5; ++i) v.emplace_back(i);
    v.pop_back();
    CHECK(v.size() == 4 && Counted::live == 4);
  }
  CHECK(Counted::live == 0);

  { // erase(pos) / erase(first,last)
    V v;
    for (int i = 0; i < 6; ++i) v.emplace_back(i);
    v.erase(v.begin() + 1);
    CHECK(v.size() == 5 && Counted::live == 5 && v[1].v == 2);
    v.erase(v.begin() + 1, v.begin() + 3);
    CHECK(v.size() == 3 && Counted::live == 3 && v[1].v == 4);
  }
  CHECK(Counted::live == 0);

  { // resize(smaller)
    V v;
    for (int i = 0; i < 6; ++i) v.emplace_back(i);
    v.resize(2);
    CHECK(v.size() == 2 && Counted::live == 2);
  }
  CHECK(Counted::live == 0);

  { // assign(count, value) shrinking
    V v;
    for (int i = 0; i < 6; ++i) v.emplace_back(i);
    Counted c(99);
    v.assign(std::size_t(3), c);
    CHECK(v.size() == 3 && Counted::live == 4); // 3 in v + c
    CHECK(v[0].v == 99 && v[2].v == 99);
  }
  CHECK(Counted::live == 0);

  { // assign(first, last) shrinking (forward iterators -> assign_forward_)
    V v;
    for (int i = 0; i < 6; ++i) v.emplace_back(i);
    Counted src[3] = { Counted(7), Counted(8), Counted(9) };
    CHECK(Counted::live == 9);
    v.assign(src, src + 3);
    CHECK(v.size() == 3 && Counted::live == 6); // 3 in v + 3 in src
    CHECK(v[0].v == 7 && v[2].v == 9);
  }
  CHECK(Counted::live == 0);

  { // copy assignment shrinking
    V a;
    for (int i = 0; i < 6; ++i) a.emplace_back(i);
    V b;
    b.emplace_back(1);
    b.emplace_back(2);
    CHECK(Counted::live == 8);
    a = b;
    CHECK(a.size() == 2 && Counted::live == 4); // 2 in a + 2 in b
  }
  CHECK(Counted::live == 0);

  { // move assignment shrinking
    V a;
    for (int i = 0; i < 6; ++i) a.emplace_back(i);
    V b;
    b.emplace_back(5);
    CHECK(Counted::live == 7);
    a = std::move(b);
    CHECK(a.size() == 1 && Counted::live == 2); // 1 in a + 1 moved-from in b
  }
  CHECK(Counted::live == 0);

  { // swap of unequal sizes (non-bitcopy path: construct + destroy)
    V a;
    for (int i = 0; i < 4; ++i) a.emplace_back(i);
    V b;
    b.emplace_back(9);
    CHECK(Counted::live == 5);
    a.swap(b);
    CHECK(a.size() == 1 && b.size() == 4 && Counted::live == 5 && a[0].v == 9);
  }
  CHECK(Counted::live == 0);
}

} // namespace

int main()
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("\n=== inplace_vector: generic types ===\n");
  test_generic<int, 16>();
  test_generic<Trivial, 16>();
  test_generic<std::string, 16>();

  std::printf("\n=== inplace_vector: fast-path value types ===\n");
  test_value_type_fastpaths<Relocatable>();
  test_value_type_fastpaths<NonTrivialCopyAssign>();
  test_free_erasure();

  std::printf("\n=== inplace_vector: boundary capacities ===\n");
  test_small_capacity<1>();
  test_small_capacity<2>();
  test_small_capacity<3>();

  std::printf("\n=== inplace_vector: zero capacity ===\n");
  test_zero_capacity<int>();
  test_zero_capacity<std::string>();

  std::printf("\n=== inplace_vector: special element types ===\n");
  test_no_default();
  test_move_only();
  test_exception_safety();
  test_fast_path_and_conversions();

  std::printf("\n=== inplace_vector: destroy paths (P1) ===\n");
  test_destroy_paths();

  std::printf("\n=== inplace_vector: fuzz ===\n");
  fuzz<int, 8>(0xC0FFEEu, 3000);
  fuzz<int, 1>(0x1234u, 2000);
  fuzz<std::string, 6>(0xBEEFu, 3000);
  fuzz<Trivial, 8>(0xABCDu, 2000);

  std::printf("\n=== inplace_vector summary: %d passed, %d failed ===\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
