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

#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include "field_variant.hpp"

// ============================================================================
// Comprehensive test: covers matrix C01-C13 (incl. C09b class-element array, C13 private dtor)
// x storage path (plain / ref-niche / bool-niche)
// + Rule-of-five combos (S4, >=14 classes) + generic fuzz run_smoke (S5, >=15 configs)
// + boundary scenarios (S6, 8 items) + compare/swap/hash (S7) + visit Cartesian dispatch (r05).
// ============================================================================

// ---------------- Rule-of-five combo classes ----------------
struct TrivialAll { int a; double b; };
struct Widget {
  int tag = 0;
  ~Widget() { tag = -1; }
};
struct NC {
  NC() = default;
  NC(const NC&) = delete;
  NC(NC&&) = default;
};
struct BothCC {
  int v = 0;
  BothCC() = default;
  BothCC(const BothCC& o) : v(o.v + 1) {}
  BothCC(BothCC&& o) noexcept : v(o.v + 1) {}
  BothCC& operator=(const BothCC& o) { v = o.v + 1; return *this; }
  BothCC& operator=(BothCC&& o) noexcept { v = o.v + 1; return *this; }
};
struct CopyOnly {
  int v = 0;
  CopyOnly() = default;
  CopyOnly(const CopyOnly& o) : v(o.v) {}
  CopyOnly& operator=(const CopyOnly& o) { v = o.v; return *this; }
  CopyOnly(CopyOnly&&) = delete;
  CopyOnly& operator=(CopyOnly&&) = delete;
};
struct MoveOnly {
  std::string s;
  MoveOnly() = default;
  MoveOnly(const char* cs) : s(cs) {}
  MoveOnly(const MoveOnly&) = delete;
  MoveOnly& operator=(const MoveOnly&) = delete;
  MoveOnly(MoveOnly&&) noexcept = default;
  MoveOnly& operator=(MoveOnly&&) noexcept = default;
};
struct NoThrowCopy {
  int v = 0;
  NoThrowCopy() = default;
  NoThrowCopy(const NoThrowCopy&) noexcept = default;
  NoThrowCopy& operator=(const NoThrowCopy&) noexcept = default;
  NoThrowCopy(NoThrowCopy&&) noexcept = default;
  NoThrowCopy& operator=(NoThrowCopy&&) noexcept = default;
};
struct ThrowCopy {
  int v = 0;
  ThrowCopy() = default;
  ThrowCopy(const ThrowCopy&) {}
  ThrowCopy& operator=(const ThrowCopy&) { return *this; }
  ThrowCopy(ThrowCopy&&) noexcept = default;
  ThrowCopy& operator=(ThrowCopy&&) noexcept = default;
};

// ---------------- Member type matrix (C01-C12) ----------------
struct AB { int a; std::string b; };
struct Ints { int a, b; };
struct ConstM { const int m = 0; };
struct VolM { volatile int m; };
struct CVVolM { const volatile int m; };
struct RefL { int& r; };
struct RefC { const int& r; };
struct RefR { int&& r; };
struct PtrM { int* p; };
struct ArrM { int a[3]; int b; };
struct StrArr { std::string s[2]; int n; };
struct Empty {
  bool operator==(const Empty&) const = default;
};
struct EmptyM { Empty e; int x; };
struct StrM { std::string s; int n; };
struct WidgetM { Widget w; };
struct MixNT { int& r; std::string s; };

// ---------------- Rule-of-five x member combos ----------------
struct WithNC { int a; NC n; };
struct WithBoth { BothCC b; int a; };
struct WithCopyOnly { CopyOnly c; int b; };
struct WithMoveOnly { MoveOnly m; int b; };
struct NoThrowStr { NoThrowCopy n; int b; };
struct ThrowStr { ThrowCopy t; int b; };
struct Big { std::string a; std::string b; };

// ---------------- Niche types (is_niche explicit switch) ----------------
struct RefNiche2 { int& a; std::string& b; using is_niche = void; };
struct RefNiche1 { int& a; using is_niche = void; };
struct RefNiche3 { int& a; std::string& b; double& c; using is_niche = void; };
struct RefNicheFailAlign { int& a; char& b; using is_niche = void; };
struct BoolNiche { bool flag; Empty e; using is_niche = void; };
struct BoolNiche1 { bool a; using is_niche = void; };
struct BoolNiche2E { bool a; Empty e1; Empty e2; using is_niche = void; };
struct BoolNicheMid { Empty e0; bool b; Empty e1; using is_niche = void; };
struct BoolNicheLast { Empty e; bool b; using is_niche = void; };
struct BoolFail { bool a; bool b; using is_niche = void; };
struct BoolPlain { bool a; Empty e; };
struct Ref2NoOpt { int& a; std::string& b; };

// Private dtor access (C13)
struct SecTDtor { int v; SecTDtor() = default; SecTDtor(int x) : v(x) {} private: ~SecTDtor() = default; };

// ============================================================================
// Metadata predicate assertions
// ============================================================================
static_assert(stdex::field_variant_detail::storage_meta<Ints>::niche_ref_capable == false);
static_assert(stdex::field_variant_detail::storage_meta<Ints>::niche_bool_capable == false);
static_assert(stdex::field_variant_detail::storage_meta<RefNiche2>::niche_ref_capable == true);
static_assert(stdex::field_variant_detail::storage_meta<RefNiche1>::niche_ref_capable == true);
static_assert(stdex::field_variant_detail::storage_meta<RefNiche3>::niche_ref_capable == true);
static_assert(stdex::field_variant_detail::storage_meta<RefNicheFailAlign>::niche_ref_capable == false);
static_assert(stdex::field_variant_detail::storage_meta<Ref2NoOpt>::niche_ref_capable == false);
static_assert(stdex::field_variant_detail::storage_meta<BoolNiche>::niche_bool_capable == true);
static_assert(stdex::field_variant_detail::storage_meta<BoolNiche1>::niche_bool_capable == true);
static_assert(stdex::field_variant_detail::storage_meta<BoolNiche2E>::niche_bool_capable == true);
static_assert(stdex::field_variant_detail::storage_meta<BoolNicheMid>::niche_bool_capable == true);
static_assert(stdex::field_variant_detail::storage_meta<BoolNicheLast>::niche_bool_capable == true);
static_assert(stdex::field_variant_detail::storage_meta<BoolNicheMid>::metadata.bool_type_index == 1);
static_assert(stdex::field_variant_detail::storage_meta<BoolNicheLast>::metadata.bool_type_index == 1);
static_assert(stdex::field_variant_detail::storage_meta<BoolFail>::niche_bool_capable == false);
static_assert(stdex::field_variant_detail::storage_meta<BoolPlain>::niche_bool_capable == false);
static_assert(stdex::field_variant_detail::storage_meta<EmptyM>::niche_bool_capable == false);

// Index by type (exact match, incl. cvref)
static_assert(stdex::field_variant_detail::storage_meta<RefNiche2>::index_of<int&> == 0);
static_assert(stdex::field_variant_detail::storage_meta<RefNiche2>::index_of<std::string&> == 1);
static_assert(stdex::field_variant_detail::storage_meta<RefNiche2>::index_of<int> == 2);
static_assert(stdex::field_variant_detail::storage_meta<Ints>::index_of<int> == 2);
static_assert(stdex::field_variant_detail::storage_meta<RefL>::index_of<int&> == 0);
static_assert(stdex::field_variant_detail::storage_meta<RefC>::index_of<const int&> == 0);
static_assert(stdex::field_variant_detail::storage_meta<RefC>::index_of<int&> == 1);

// ============================================================================
// Rule-of-five x member type combos: usability/triviality/noexcept assertions (S4)
// ============================================================================
static_assert(std::is_trivially_copyable_v<stdex::field_variant<TrivialAll>>);
static_assert(std::is_trivially_destructible_v<stdex::field_variant<TrivialAll>>);
static_assert(std::is_copy_constructible_v<stdex::field_variant<WidgetM>>);
static_assert(!std::is_trivially_copy_constructible_v<stdex::field_variant<WidgetM>>);
static_assert(!std::is_trivially_copy_assignable_v<stdex::field_variant<WidgetM>>);
static_assert(!std::is_trivially_copyable_v<stdex::field_variant<WidgetM>>);
static_assert(!std::is_trivially_destructible_v<stdex::field_variant<WidgetM>>);
static_assert(std::is_destructible_v<stdex::field_variant<WidgetM>>);
static_assert(!std::is_copy_constructible_v<stdex::field_variant<WithNC>>);
static_assert(std::is_move_constructible_v<stdex::field_variant<WithNC>>);
static_assert(std::is_trivially_move_constructible_v<stdex::field_variant<WithNC>>);
static_assert(!std::is_copy_assignable_v<stdex::field_variant<WithNC>>);
static_assert(!std::is_move_assignable_v<stdex::field_variant<WithNC>>);
static_assert(std::is_copy_constructible_v<stdex::field_variant<WithBoth>>);
static_assert(std::is_move_constructible_v<stdex::field_variant<WithBoth>>);
static_assert(!std::is_trivially_copyable_v<stdex::field_variant<WithBoth>>);
static_assert(!std::is_trivially_copy_constructible_v<stdex::field_variant<WithBoth>>);
static_assert(std::is_trivially_destructible_v<stdex::field_variant<WithBoth>>);
static_assert(std::is_copy_constructible_v<stdex::field_variant<WithCopyOnly>>);
static_assert(!std::is_move_constructible_v<stdex::field_variant<WithCopyOnly>>);
static_assert(std::is_copy_assignable_v<stdex::field_variant<WithCopyOnly>>);
static_assert(!std::is_move_assignable_v<stdex::field_variant<WithCopyOnly>>);
static_assert(!std::is_copy_constructible_v<stdex::field_variant<WithMoveOnly>>);
static_assert(!std::is_copy_assignable_v<stdex::field_variant<WithMoveOnly>>);
static_assert(std::is_move_constructible_v<stdex::field_variant<WithMoveOnly>>);
static_assert(std::is_move_assignable_v<stdex::field_variant<WithMoveOnly>>);
static_assert(std::is_copy_constructible_v<stdex::field_variant<ConstM>>);
static_assert(!std::is_copy_assignable_v<stdex::field_variant<ConstM>>);
static_assert(std::is_move_constructible_v<stdex::field_variant<ConstM>>);
static_assert(!std::is_move_assignable_v<stdex::field_variant<ConstM>>);
static_assert(std::is_trivially_copyable_v<stdex::field_variant<VolM>>);
static_assert(std::is_trivially_copyable_v<stdex::field_variant<CVVolM>>);
static_assert(!std::is_copy_constructible_v<stdex::field_variant<ArrM>>);
static_assert(!std::is_move_constructible_v<stdex::field_variant<ArrM>>);
static_assert(std::is_trivially_destructible_v<stdex::field_variant<ArrM>>);
static_assert(std::is_destructible_v<stdex::field_variant<StrArr>>);
static_assert(!std::is_trivially_destructible_v<stdex::field_variant<StrArr>>);
static_assert(!std::is_copy_constructible_v<stdex::field_variant<StrArr>>);
static_assert(std::is_trivially_copyable_v<stdex::field_variant<EmptyM>>);
static_assert(std::is_trivially_copyable_v<stdex::field_variant<RefL>>);
static_assert(std::is_trivially_copyable_v<stdex::field_variant<RefC>>);
static_assert(!std::is_copy_constructible_v<stdex::field_variant<RefR>>);
static_assert(std::is_move_constructible_v<stdex::field_variant<RefR>>);
static_assert(std::is_trivially_move_constructible_v<stdex::field_variant<RefR>>);
static_assert(std::is_trivially_move_assignable_v<stdex::field_variant<RefR>>);
static_assert(std::is_trivially_copyable_v<stdex::field_variant<PtrM>>);
static_assert(std::is_copy_constructible_v<stdex::field_variant<StrM>>);
static_assert(!std::is_trivially_copyable_v<stdex::field_variant<StrM>>);
static_assert(std::is_nothrow_copy_constructible_v<stdex::field_variant<NoThrowStr>>);
static_assert(std::is_nothrow_move_constructible_v<stdex::field_variant<NoThrowStr>>);
static_assert(!std::is_nothrow_copy_constructible_v<stdex::field_variant<ThrowStr>>);
static_assert(std::is_nothrow_move_constructible_v<stdex::field_variant<ThrowStr>>);
static_assert(!std::is_trivially_copyable_v<stdex::field_variant<RefNiche2>>);
static_assert(std::is_trivially_copy_constructible_v<stdex::field_variant<RefNiche2>>);
static_assert(std::is_trivially_copyable_v<stdex::field_variant<RefNiche1>>);
static_assert(!std::is_trivially_copyable_v<stdex::field_variant<RefNiche3>>);
static_assert(std::is_trivially_copy_constructible_v<stdex::field_variant<RefNiche3>>);
static_assert(std::is_trivially_copyable_v<stdex::field_variant<BoolNiche>>);
static_assert(std::is_trivially_copyable_v<stdex::field_variant<BoolNiche1>>);
static_assert(std::is_trivially_copyable_v<stdex::field_variant<BoolNiche2E>>);
static_assert(std::is_nothrow_copy_constructible_v<stdex::field_variant<Ints>>);
static_assert(std::is_nothrow_move_assignable_v<stdex::field_variant<Ints>>);

// ============================================================================
// const propagation assertions (S6-7)
// ============================================================================
static_assert(std::is_same_v<decltype(std::declval<stdex::field_variant<Ints>&>().get<0>()), int&>);
static_assert(std::is_same_v<decltype(std::declval<const stdex::field_variant<Ints>&>().get<0>()), const int&>);
static_assert(std::is_same_v<decltype(std::declval<stdex::field_variant<Ints>&&>().get<0>()), int&&>);
static_assert(std::is_same_v<decltype(std::declval<stdex::field_variant<ConstM>&>().get<0>()), const int&>);
static_assert(std::is_same_v<decltype(std::declval<stdex::field_variant<VolM>&>().get<0>()), volatile int&>);
static_assert(std::is_same_v<decltype(std::declval<stdex::field_variant<RefL>&>().get<0>()), int&>);
static_assert(std::is_same_v<decltype(std::declval<const stdex::field_variant<RefL>&>().get<0>()), int&>);
static_assert(std::is_same_v<decltype(std::declval<stdex::field_variant<RefC>&>().get<0>()), const int&>);
static_assert(std::is_same_v<decltype(std::declval<const stdex::field_variant<RefC>&>().get<0>()), const int&>);
static_assert(std::is_same_v<decltype(std::declval<stdex::field_variant<RefR>&>().get<0>()), int&>);
static_assert(std::is_same_v<decltype(std::declval<const stdex::field_variant<RefNiche2>&>().get<0>()), int&>);
static_assert(std::is_same_v<decltype(std::declval<const stdex::field_variant<RefNiche2>&>().get<1>()), std::string&>);
static_assert(std::is_same_v<decltype(std::declval<stdex::field_variant<BoolNiche>&>().get<0>()), bool&>);
static_assert(std::is_same_v<decltype(std::declval<const stdex::field_variant<BoolNiche>&>().get<0>()), const bool&>);
static_assert(std::is_same_v<decltype(std::declval<stdex::field_variant<BoolNiche>&>().get<1>()), Empty>);
static_assert(std::is_same_v<decltype(std::declval<const stdex::field_variant<BoolNiche>&>().get<1>()), Empty>);
static_assert(std::is_same_v<decltype(std::declval<stdex::field_variant<EmptyM>&>().get<0>()), Empty&>);
static_assert(std::is_same_v<decltype(std::declval<stdex::field_variant<PtrM>&>().get<0>()), int*&>);
static_assert(std::is_same_v<decltype(std::declval<const stdex::field_variant<PtrM>&>().get<0>()), int* const&>);

// ============================================================================
// sizeof/alignof assertions (S6-8)
// ============================================================================
static_assert(sizeof(stdex::field_variant<RefNiche2>) == 8);
static_assert(sizeof(stdex::field_variant<RefNiche1>) == 8);
static_assert(sizeof(stdex::field_variant<RefNiche3>) == 8);
static_assert(sizeof(stdex::field_variant<BoolNiche>) == 1);
static_assert(sizeof(stdex::field_variant<BoolNiche1>) == 1);
static_assert(sizeof(stdex::field_variant<BoolNiche2E>) == 1);
static_assert(sizeof(stdex::field_variant<BoolNicheMid>) == 1);
static_assert(sizeof(stdex::field_variant<BoolNicheLast>) == 1);
static_assert(sizeof(stdex::field_variant<Ints>) == 8);
static_assert(sizeof(stdex::field_variant<RefL>) == 16);
static_assert(sizeof(stdex::field_variant<BoolPlain>) == 2);
static_assert(alignof(stdex::field_variant<RefNiche2>) == 8);

// ============================================================================
// compare / swap / hash availability, constraints, and result types (S7)
// ============================================================================
template <class T, class = void>
struct has_spaceship : std::false_type {};
template <class T>
struct has_spaceship<T, std::void_t<decltype(std::declval<const T&>() <=> std::declval<const T&>())>> : std::true_type {};

template <class T, class = void>
struct has_swap : std::false_type {};
template <class T>
struct has_swap<T, std::void_t<decltype(std::declval<T&>().swap(std::declval<T&>()))>> : std::true_type {};

template <class T, class = void>
struct has_hash : std::false_type {};
template <class T>
struct has_hash<T, std::void_t<decltype(std::hash<T>{}(std::declval<const T&>()))>> : std::true_type {};

static_assert(requires (const stdex::field_variant<Ints>& a, const stdex::field_variant<Ints>& b) { a <=> b; });
static_assert(requires (const stdex::field_variant<AB>& a, const stdex::field_variant<AB>& b) { a <=> b; });
static_assert(requires (const stdex::field_variant<EmptyM>& a, const stdex::field_variant<EmptyM>& b) { a <=> b; });
static_assert(requires (const stdex::field_variant<BoolNiche>& a, const stdex::field_variant<BoolNiche>& b) { a <=> b; });
static_assert(requires (const stdex::field_variant<RefNiche2>& a, const stdex::field_variant<RefNiche2>& b) { a <=> b; });
static_assert(!has_spaceship<stdex::field_variant<WidgetM>>::value);
static_assert(!has_spaceship<stdex::field_variant<NoThrowStr>>::value);
static_assert(std::is_same_v<decltype(std::declval<const stdex::field_variant<Ints>&>() <=> std::declval<const stdex::field_variant<Ints>&>()), std::strong_ordering>);
static_assert(std::is_same_v<decltype(std::declval<const stdex::field_variant<AB>&>() <=> std::declval<const stdex::field_variant<AB>&>()), std::strong_ordering>);
static_assert(std::is_same_v<decltype(std::declval<const stdex::field_variant<BoolNiche>&>() <=> std::declval<const stdex::field_variant<BoolNiche>&>()), std::strong_ordering>);
static_assert(std::is_same_v<decltype(std::declval<const stdex::field_variant<RefNiche2>&>() <=> std::declval<const stdex::field_variant<RefNiche2>&>()), std::strong_ordering>);
static_assert(requires (const stdex::field_variant<Ints>& a, const stdex::field_variant<Ints>& b) { a == b; });
static_assert(requires (const stdex::field_variant<AB>& a, const stdex::field_variant<AB>& b) { a != b; });
static_assert(requires (stdex::field_variant<Ints>& a, stdex::field_variant<Ints>& b) { a.swap(b); });
static_assert(requires (stdex::field_variant<BoolNiche>& a, stdex::field_variant<BoolNiche>& b) { a.swap(b); });
static_assert(requires (stdex::field_variant<RefNiche2>& a, stdex::field_variant<RefNiche2>& b) { a.swap(b); });
static_assert(requires (stdex::field_variant<AB>& a, stdex::field_variant<AB>& b) { a.swap(b); });
static_assert(requires (stdex::field_variant<Ints>& a, stdex::field_variant<Ints>& b) { swap(a, b); });
static_assert(requires (stdex::field_variant<RefNiche2>& a, stdex::field_variant<RefNiche2>& b) { swap(a, b); });
static_assert(!has_swap<stdex::field_variant<WithCopyOnly>>::value);
static_assert(requires (const stdex::field_variant<AB>& a) { std::hash<stdex::field_variant<AB>>{}(a); });
static_assert(requires (const stdex::field_variant<BoolNiche>& a) { std::hash<stdex::field_variant<BoolNiche>>{}(a); });
static_assert(requires (const stdex::field_variant<RefNiche2>& a) { std::hash<stdex::field_variant<RefNiche2>>{}(a); });
static_assert(requires (const stdex::field_variant<Ints>& a) { std::hash<stdex::field_variant<Ints>>{}(a); });
static_assert(!has_hash<stdex::field_variant<ArrM>>::value);

// ============================================================================
// Generic fuzz: run_smoke (S5) -- fixed 8-step operation sequence
// ============================================================================
template <class T, class Arg0, class Arg1>
void run_smoke(Arg0&& a0, Arg1&& a1) {
  constexpr std::size_t N = stdex::field_variant_detail::storage_meta<T>::member_count;
  stdex::field_variant<T> v;
  assert(!v.has_value());
  assert(v.index() == N);
  v.template emplace<0>(std::forward<Arg0>(a0));
  assert(v.has_value() && v.index() == 0);
  auto c = v;
  assert(c.index() == 0);
  if constexpr (std::is_copy_assignable_v<stdex::field_variant<T>>) {
    c = v;
    assert(c.index() == 0);
  }
  auto m = std::move(c);
  assert(m.index() == 0);
  if constexpr (N >= 2) {
    v.template emplace<1>(std::forward<Arg1>(a1));
    assert(v.has_value() && v.index() == 1);
    if constexpr (std::is_copy_assignable_v<stdex::field_variant<T>>) {
      v = m;
      assert(v.index() == 0);
    }
  }
  if constexpr (std::is_move_assignable_v<stdex::field_variant<T>>) {
    v = std::move(m);
    assert(v.index() == 0);
  }
}

template <class T, class Arg0, class Arg1>
void run_smoke_moveonly(Arg0&& a0, Arg1&& a1) {
  constexpr std::size_t N = stdex::field_variant_detail::storage_meta<T>::member_count;
  stdex::field_variant<T> v;
  assert(!v.has_value() && v.index() == N);
  v.template emplace<0>(std::forward<Arg0>(a0));
  assert(v.index() == 0);
  auto m = std::move(v);
  assert(m.index() == 0);
  if constexpr (N >= 2) {
    v.template emplace<1>(std::forward<Arg1>(a1));
    assert(v.index() == 1);
    if constexpr (std::is_move_assignable_v<stdex::field_variant<T>>) {
      v = std::move(m);
      assert(v.index() == 0);
    }
  }
  stdex::field_variant<T> w;
  if constexpr (std::is_move_assignable_v<stdex::field_variant<T>>) {
    w = std::move(m);
    assert(w.index() == 0);
  }
}

// ============================================================================
// Target boundary tests (S6-1~8)
// ============================================================================
void test_boundary() {
  {
    stdex::field_variant<AB> v;
    v.emplace<0>(1);
    v.emplace<1>("x");
    assert(v.index() == 1 && v.get<1>() == "x");
    v.emplace<0>(9);
    assert(v.index() == 0 && v.get<0>() == 9);
  }
  {
    stdex::field_variant<AB> a, b;
    a.emplace<1>("aaa");
    b.emplace<1>("bbb");
    a = b;
    assert(a.get<1>() == "bbb");
    a = std::move(b);
    assert(a.get<1>() == "bbb");
  }
  {
    int x = 1, y = 2;
    stdex::field_variant<RefL> a, b;
    a.emplace<0>(x);
    b.emplace<0>(y);
    a = b;
    assert(x == 1 && a.get<0>() == 2);
  }
  {
    int x = 1, y = 2;
    stdex::field_variant<MixNT> a, b;
    a.emplace<0>(x);
    b.emplace<0>(y);
    a = b;
    assert(x == 2 && a.get<0>() == 2);
  }
  {
    stdex::field_variant<AB> a, b;
    a.emplace<0>(42);
    b.emplace<1>("hi");
    a = b;
    assert(a.index() == 1 && a.get<1>() == "hi");
    b.emplace<0>(7);
    a = std::move(b);
    assert(a.index() == 0 && a.get<0>() == 7);
  }
  {
    int x = 1;
    stdex::field_variant<RefL> r;
    r.emplace<0>(x);
    auto c = r;
    x = 2;
    assert(c.get<0>() == 2);
  }
  {
    int x = 1;
    std::string s = "a";
    stdex::field_variant<RefNiche2> r;
    r.emplace<0>(x);
    auto c = r;
    assert(c.index() == 0 && c.get<0>() == 1);
    x = 5;
    assert(c.get<0>() == 5);
    r.emplace<1>(s);
    auto d = r;
    assert(d.index() == 1 && d.get<1>() == "a");
  }
  {
    stdex::field_variant<BoolNiche> b;
    b.emplace<0>(true);
    auto c = b;
    c.get<0>() = false;
    assert(c.index() == 0 && c.get<0>() == false);
    assert(b.get<0>() == true);
    c.emplace<0>(true);
    assert(c.index() == 0 && c.get<0>() == true);
    c.emplace<1>();
    assert(c.index() == 1 && c.has_value());
    assert(c.get<1>().operator==(Empty{}));
    c.emplace<0>(false);
    assert(c.index() == 0 && c.get<0>() == false);
  }
  {
    stdex::field_variant<BoolNiche1> b;
    assert(!b.has_value() && b.index() == 1);
    b.emplace<0>(false);
    assert(b.index() == 0 && b.get<0>() == false);
    b.emplace<0>(true);
    assert(b.get<0>() == true);
  }
  {
    stdex::field_variant<BoolNiche> b;
    b.emplace<0>(true);
    b.emplace<1>();
    assert(b.index() == 1);
    b.emplace<0>(true);
    assert(b.index() == 0 && b.get<0>());
  }
  {
    stdex::field_variant<BoolNicheMid> v;
    v.emplace<2>();
    assert(v.index() == 2);
    v.emplace<1>(true);
    assert(v.index() == 1 && v.get<1>() == true);
    v.emplace<0>();
    assert(v.index() == 0);
    stdex::field_variant<BoolNicheLast> w;
    w.emplace<0>();
    assert(w.index() == 0);
    w.emplace<1>(true);
    assert(w.index() == 1 && w.get<1>() == true);
  }
  {
    stdex::field_variant<VolM> v;
    v.emplace<0>(7);
    assert(v.get<0>() == 7);
    auto c = v;
    assert(c.get<0>() == 7);
    c.get<0>() = 8;
    assert(c.get<0>() == 8);
    stdex::field_variant<CVVolM> cv;
    cv.emplace<0>(3);
    assert(cv.get<0>() == 3);
  }
  {
    stdex::field_variant<ConstM> v;
    v.emplace<0>(42);
    assert(v.get<0>() == 42);
    auto c = v;
    assert(c.get<0>() == 42);
  }
  {
    int x = 1;
    const int z = 3;
    stdex::field_variant<RefC> rc;
    rc.emplace<0>(z);
    assert(rc.get<0>() == 3);
    stdex::field_variant<RefR> rr;
    rr.emplace<0>(x);
    assert(rr.get<0>() == 1);
  }
  {
    int x = 1;
    stdex::field_variant<PtrM> p;
    p.emplace<0>(&x);
    assert(*p.get<0>() == 1);
    *p.get<0>() = 9;
    assert(x == 9);
  }
  {
    stdex::field_variant<EmptyM> e;
    e.emplace<0>();
    assert(e.index() == 0);
    [[maybe_unused]] Empty& er = e.get<0>();
    e.emplace<1>(5);
    assert(e.index() == 1 && e.get<1>() == 5);
  }
  {
    stdex::field_variant<ArrM> v;
    assert(!v.has_value() && v.index() == 2);
    v.emplace<0>(1, 2, 3);
    assert(v.index() == 0);
    assert(v.get<0>()[0] == 1 && v.get<0>()[1] == 2 && v.get<0>()[2] == 3);
    auto& arr = v.get<0>();
    arr[1] = 42;
    assert(v.get<0>()[1] == 42);
    v.emplace<1>(5);
    assert(v.index() == 1 && v.get<1>() == 5);
    v.emplace<1>(9);
    assert(v.index() == 1 && v.get<1>() == 9);
    v.emplace<0>();
    assert(v.index() == 0 && v.get<0>()[0] == 0 && v.get<0>()[1] == 0 && v.get<0>()[2] == 0);
  }
  {
    stdex::field_variant<StrArr> v;
    v.emplace<0>("alpha", "beta");
    assert(v.index() == 0 && v.get<0>()[0] == "alpha" && v.get<0>()[1] == "beta");
    v.emplace<1>(7);
    assert(v.index() == 1 && v.get<1>() == 7);
    v.emplace<0>("x", "y");
    assert(v.get<0>()[0] == "x" && v.get<0>()[1] == "y");
  }
  {
    stdex::field_variant<WidgetM> w;
    w.emplace<0>(Widget{});
    assert(w.get<0>().tag == 0);
    auto c = w;
    assert(c.get<0>().tag == 0);
    c.get<0>().tag = 3;
    assert(c.get<0>().tag == 3);
  }
  {
    stdex::field_variant<AB> v;
    v.emplace<0>(1);
    assert(v.get<int>() == 1);
    stdex::field_variant<RefNiche2> r;
    int x = 4;
    r.emplace<0>(x);
    assert(r.get<int&>() == 4);
    stdex::field_variant<StrM> s;
    s.emplace<1>(5);
    assert(s.get<int>() == 5);
    stdex::field_variant<BoolNiche> b;
    b.emplace<0>(true);
    assert(b.get<bool>() == true);
  }
  {
    char c = 'z';
    stdex::field_variant<RefNicheFailAlign> r;
    r.emplace<1>(c);
    assert(r.index() == 1 && r.get<1>() == 'z');
    assert(sizeof(stdex::field_variant<RefNicheFailAlign>) == 16);
  }
  {
    int x = 7;
    stdex::field_variant<Ref2NoOpt> r;
    r.emplace<0>(x);
    assert(r.index() == 0 && r.get<0>() == 7);
    assert(sizeof(stdex::field_variant<Ref2NoOpt>) == 16);
  }
}

// ============================================================================
// Rule-of-five custom class runtime (WithBoth / WithCopyOnly / WithMoveOnly)
// ============================================================================
void test_five_law_runtime() {
  {
    stdex::field_variant<WithBoth> v;
    v.emplace<0>(BothCC{});
    assert(v.index() == 0);
    auto c = v;
    assert(c.index() == 0);
    c = v;
    assert(c.index() == 0);
    auto m = std::move(c);
    assert(m.index() == 0);
  }
  {
    CopyOnly co;
    co.v = 5;
    stdex::field_variant<WithCopyOnly> v;
    v.emplace<0>(co);
    assert(v.get<0>().v == 5);
    auto c = v;
    assert(c.get<0>().v == 5);
    c = v;
    assert(c.get<0>().v == 5);
  }
  {
    stdex::field_variant<WithMoveOnly> v;
    v.emplace<0>(MoveOnly{"mv"});
    assert(v.get<0>().s == "mv");
    auto m = std::move(v);
    assert(m.get<0>().s == "mv");
    m.emplace<1>(42);
    assert(m.index() == 1 && m.get<1>() == 42);
  }
}

// ============================================================================
// Generic fuzz instantiation (S5: >=15 configs)
// ============================================================================
void test_fuzz() {
  run_smoke<Ints>(1, 2);
  run_smoke<AB>(1, std::string("hi"));
  {
    int x = 1;
    run_smoke<RefL>(x, x);
    run_smoke<RefC>(x, x);
    run_smoke<PtrM>(&x, &x);
  }
  run_smoke<StrM>(std::string("s"), 5);
  run_smoke<WidgetM>(Widget{}, Widget{});
  run_smoke<WithBoth>(BothCC{}, 0);
  run_smoke<ThrowStr>(ThrowCopy{}, 0);
  run_smoke<NoThrowStr>(NoThrowCopy{}, 0);
  run_smoke<VolM>(7, 7);
  run_smoke<Big>(std::string("a"), std::string("b"));
  {
    int x = 1;
    std::string s = "s";
    run_smoke<RefNiche2>(x, s);
    run_smoke<RefNiche1>(x, x);
    run_smoke<RefNiche3>(x, s);
  }
  run_smoke<BoolNiche1>(true, true);
  run_smoke<TrivialAll>(1, 2.5);
  run_smoke_moveonly<WithMoveOnly>(MoveOnly{"m"}, 42);
  run_smoke_moveonly<WithNC>(1, NC{});
  {
    int x = 1;
    run_smoke_moveonly<RefR>(x, x);
  }
}

// ============================================================================
// Private dtor access (C13)
// ============================================================================
void test_private_dtor() {
  static_assert(std::is_destructible_v<stdex::field_variant<SecTDtor>>);
  static_assert(std::is_trivially_destructible_v<stdex::field_variant<SecTDtor>>);
  static_assert(std::is_copy_constructible_v<stdex::field_variant<SecTDtor>>);
  static_assert(std::is_move_constructible_v<stdex::field_variant<SecTDtor>>);
  {
    stdex::field_variant<SecTDtor> v;
    assert(!v.has_value() && v.index() == 1);
    v.template emplace<0>(42);
    assert(v.has_value() && v.index() == 0 && v.get<0>() == 42);
    auto c = v;
    assert(c.index() == 0 && c.get<0>() == 42);
    auto m = std::move(c);
    assert(m.index() == 0 && m.get<0>() == 42);
  }
}

// ============================================================================
// Runtime: operator<=> and rewritten comparisons (S7)
// ============================================================================
void test_compare() {
  stdex::field_variant<Ints> a, b;
  a.emplace<0>(1);
  b.emplace<0>(2);
  assert((a <=> b) == std::strong_ordering::less);
  assert(a < b && a != b);
  stdex::field_variant<Ints> c;
  c.emplace<0>(2);
  assert((b <=> c) == std::strong_ordering::equal);
  assert(b == c);
  stdex::field_variant<AB> d, e;
  d.emplace<0>(1000);
  e.emplace<1>("zzz");
  assert(d < e);
  stdex::field_variant<Ints> dis;
  assert(!dis.has_value() && dis.index() == 2);
  stdex::field_variant<Ints> en;
  en.emplace<0>(1);
  assert(dis < en);
  assert(en > dis);
  assert(dis == stdex::field_variant<Ints>{});
  stdex::field_variant<AB> s1, s2;
  s1.emplace<1>("apple");
  s2.emplace<1>("banana");
  assert((s1 <=> s2) == std::strong_ordering::less);
  stdex::field_variant<BoolNiche> bn1, bn2;
  bn1.emplace<0>(true);
  bn2.emplace<0>(false);
  assert(bn1 > bn2);
  bn2.emplace<1>();
  assert(bn1 < bn2);
  int x = 1, y = 5;
  std::string p = "p", q = "q";
  stdex::field_variant<RefNiche2> r1, r2;
  r1.emplace<0>(x);
  r2.emplace<0>(y);
  assert(r1 < r2);
  r2.emplace<1>(q);
  assert(r1 < r2);
  r1.emplace<1>(p);
  assert(r1 < r2);
}

// ============================================================================
// Runtime: swap (member + non-member ADL, S7)
// ============================================================================
void test_swap() {
  stdex::field_variant<AB> a, b;
  a.emplace<1>("aaa");
  b.emplace<1>("bbb");
  a.swap(b);
  assert(a.get<1>() == "bbb" && b.get<1>() == "aaa");
  stdex::field_variant<AB> c, d;
  c.emplace<0>(42);
  d.emplace<1>("hi");
  c.swap(d);
  assert(c.index() == 1 && c.get<1>() == "hi");
  assert(d.index() == 0 && d.get<0>() == 42);
  stdex::field_variant<Ints> dis, en;
  en.emplace<0>(7);
  dis.swap(en);
  assert(dis.has_value() && dis.get<0>() == 7);
  assert(!en.has_value() && en.index() == 2);
  stdex::field_variant<Ints> g1, g2;
  g1.swap(g2);
  assert(!g1.has_value() && !g2.has_value());
  stdex::field_variant<BoolNiche> bn1, bn2;
  bn1.emplace<0>(true);
  bn2.emplace<0>(false);
  bn1.swap(bn2);
  assert(bn1.get<0>() == false && bn2.get<0>() == true);
  stdex::field_variant<BoolNiche> bn3, bn4;
  bn3.emplace<0>(true);
  bn4.emplace<1>();
  bn3.swap(bn4);
  assert(bn3.index() == 1 && bn4.index() == 0 && bn4.get<0>() == true);
  int x = 1, y = 2;
  std::string p = "p", q = "q";
  stdex::field_variant<RefNiche2> r1, r2;
  r1.emplace<0>(x);
  r2.emplace<0>(y);
  r1.swap(r2);
  assert(x == 2 && y == 1);
  stdex::field_variant<RefNiche2> r3, r4;
  r3.emplace<1>(p);
  r4.emplace<0>(x);
  r3.swap(r4);
  assert(r3.index() == 0 && r3.get<0>() == 2);
  assert(r4.index() == 1 && r4.get<1>() == "p");
  stdex::field_variant<Ints> n1, n2;
  n1.emplace<0>(11);
  n2.emplace<0>(22);
  {
    using std::swap;
    swap(n1, n2);
  }
  assert(n1.get<0>() == 22 && n2.get<0>() == 11);
  stdex::field_variant<AB> s1, s2;
  s1.emplace<1>("hello");
  s2.emplace<1>("world");
  s1.swap(s2);
  assert(s1.get<1>() == "world" && s2.get<1>() == "hello");
}

// ============================================================================
// Runtime: hash (S7)
// ============================================================================
void test_hash() {
  std::hash<stdex::field_variant<AB>> h;
  stdex::field_variant<AB> a, b, c;
  a.emplace<1>("same");
  b.emplace<1>("same");
  c.emplace<1>("other");
  assert(h(a) == h(b));
  assert(h(a) != h(c));
  stdex::field_variant<AB> d, e;
  d.emplace<0>(42);
  e.emplace<0>(42);
  assert(h(d) == h(e));
  stdex::field_variant<AB> g1, g2;
  assert(h(g1) == h(g2));
  std::hash<stdex::field_variant<BoolNiche>> hb;
  stdex::field_variant<BoolNiche> b1, b2;
  b1.emplace<0>(true);
  b2.emplace<0>(true);
  assert(hb(b1) == hb(b2));
  b2.emplace<1>();
  assert(hb(b1) != hb(b2));
  std::string s = "s";
  std::hash<stdex::field_variant<RefNiche2>> hr;
  stdex::field_variant<RefNiche2> r1, r2;
  r1.emplace<1>(s);
  r2.emplace<1>(s);
  assert(hr(r1) == hr(r2));
  std::unordered_map<stdex::field_variant<AB>, int> m;
  stdex::field_variant<AB> k;
  k.emplace<1>("key");
  m[k] = 42;
  assert(m[k] == 42);
  stdex::field_variant<AB> k2;
  k2.emplace<1>("key");
  assert(m.find(k2) != m.end());
}

// ============================================================================
// visit Cartesian dispatch (r05)
// ============================================================================
struct V4  { int a, b, c, d; };
struct V16 { int a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15; };
struct V17 { int a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,a16; };
struct SMix { int i; double d; };

void test_visit() {
  using MV4  = stdex::field_variant<V4>;
  using MV16 = stdex::field_variant<V16>;
  using MV17 = stdex::field_variant<V17>;
  auto add = [](auto... vs) { return (static_cast<int>(vs) + ... + 0); };

  { MV4 v; v.emplace<2>(5); assert(v.visit([](auto x) { return x + 1; }) == 6); }
  { MV4 v; v.emplace<3>(9); assert(v.visit<long>([](auto x) { return x + 1; }) == 10L); }
  { MV4 v; v.emplace<0>(7); v.visit<void>([](auto) {}); }

  { MV4 v; v.emplace<1>(4); assert(std::visit([](auto x) { return x; }, v) == 4); }
  { MV4 v; v.emplace<0>(0);
    assert(std::visit([](auto&& x) -> decltype(auto) { return std::is_lvalue_reference_v<decltype(x)>; }, v)); }
  { MV4 v; v.emplace<0>(0);
    assert(std::visit([](auto&& x) -> decltype(auto) { return std::is_rvalue_reference_v<decltype(x)>; }, std::move(v))); }

  { MV4 x; x.emplace<3>(10); MV4 y; y.emplace<2>(20); assert(std::visit(add, x, y) == 30); }
  { MV16 x; x.emplace<15>(1); MV16 y; y.emplace<0>(2); assert(std::visit(add, x, y) == 3); }
  { MV4 x; x.emplace<0>(5); MV4 y; y.emplace<1>(6); assert(std::visit<long>(add, x, y) == 11L); }

  { MV17 a; a.emplace<3>(7); MV17 b; b.emplace<9>(8); assert(std::visit(add, a, b) == 15); }
  { MV17 a; a.emplace<1>(7); MV17 b; b.emplace<2>(8); assert(std::visit<long>(add, a, b) == 15L); }

  { stdex::field_variant<Ints> x, y, z; x.emplace<0>(1); y.emplace<1>(2); z.emplace<0>(3);
    assert(std::visit(add, x, y, z) == 6); }

  { stdex::field_variant<SMix> x, y; x.emplace<1>(2.5); y.emplace<0>(3);
    assert(std::visit([](auto a, auto b) -> decltype(auto) { return a + b; }, x, y) == 5.5); }

  { MV4 c; c.emplace<2>(0); auto& r = std::visit([](auto&& v) -> decltype(auto) { return (v); }, c);
    r = 42; assert(c.template get<2>() == 42); }
}

// ============================================================================
// bool-niche constexpr full flow (S6-9)
// ============================================================================
constexpr bool ce_bool_niche() {
  stdex::field_variant<BoolNiche1> a;
  if (a.has_value() || a.index() != 1) return false;
  a.emplace<0>();
  if (!a.has_value() || a.index() != 0 || a.get<0>() != false) return false;
  a.emplace<0>(42);
  if (a.get<0>() != true) return false;
  {
    stdex::field_variant<BoolNiche1> x;
    if (x.has_value() || x.index() != 1) return false;
  }

  stdex::field_variant<BoolNiche> b;
  b.emplace<0>(true);
  auto c = b;
  auto d = std::move(c);
  if (d.get<0>() != true) return false;
  d.emplace<1>();
  if (d.index() != 1 || !d.has_value() || !(d.get<1>() == Empty{})) return false;
  stdex::field_variant<BoolNiche> e;
  stdex::field_variant<BoolNiche> f = e;
  if (f.has_value()) return false;
  f = d;
  if (f.index() != 1 || !(f.get<1>() == Empty{})) return false;
  f = b;
  if (f.index() != 0 || f.get<0>() != true) return false;
  f.get<0>() = false;
  if (f.index() != 0 || f.get<0>() != false) return false;
  f.get<0>() = true;

  stdex::field_variant<BoolNiche2E> g;
  g.emplace<2>();
  if (g.index() != 2 || !g.has_value()) return false;
  if (!(g.get<2>() == Empty{})) return false;
  g.emplace<0>(false);
  if (g.get<0>() != false) return false;
  {
    stdex::field_variant<BoolNiche2E> y;
    if (y.has_value() || y.index() != 3) return false;
  }

  stdex::field_variant<BoolNicheMid> h;
  h.emplace<0>();
  if (h.index() != 0 || !(h.get<0>() == Empty{})) return false;
  h.emplace<1>(true);
  if (h.index() != 1 || h.get<1>() != true) return false;
  h.emplace<2>();
  if (h.index() != 2 || !(h.get<2>() == Empty{})) return false;
  h.emplace<1>(false);
  if (h.index() != 1 || h.get<1>() != false) return false;
  stdex::field_variant<BoolNicheLast> i;
  i.emplace<0>();
  if (i.index() != 0 || !(i.get<0>() == Empty{})) return false;
  i.emplace<1>(false);
  if (i.index() != 1 || i.get<1>() != false) return false;
  i.emplace<0>();
  if (i.index() != 0 || !(i.get<0>() == Empty{})) return false;
  return true;
}

// ============================================================================
// at_index dispatch test (merged from test_at_index_dispatch.cpp, 2026-08-17):
// macro-stamped switch (4..256) + N>256 function table.
// Covers: N at threshold sides (1/3->switch<4>, 17->switch<32>, 255/256 boundary,
// 257/300/512->table), constexpr evaluation, runtime per-I scan, void return path
// (destruction/construction).
// Table path (N>256) uses mock self to directly call at_index<N>, no 300-field real struct needed.
// disengaged (index()==N) not tested: callers always enter under has_value() guard, per unreachable contract.
// ============================================================================

namespace {
struct MockSelf {
  std::size_t i;
  constexpr std::size_t index() const noexcept { return i; }
};

template <std::size_t N>
constexpr int at(MockSelf s, int base) {
  return stdex::field_variant_detail::at_index<N>(
      s, [&]<std::size_t I>(std::integral_constant<std::size_t, I>) { return base + static_cast<int>(I); });
}

template <std::size_t N>
constexpr bool all_engaged_ok() {
  for (std::size_t i = 0; i < N; ++i) {
    if (at<N>(MockSelf{i}, 10) != 10 + static_cast<int>(i)) return false;
  }
  return true;
}

template <std::size_t N>
constexpr int at_void(std::size_t idx) {
  int counter = 0;
  stdex::field_variant_detail::at_index<N>(
      MockSelf{idx}, [&]<std::size_t I>(std::integral_constant<std::size_t, I>) -> void { ++counter; });
  return counter;
}
} // namespace

// ---- constexpr: switch path (ceiling bucket) ----
static_assert(at<1>(MockSelf{0}, 10) == 10);
static_assert(at<3>(MockSelf{0}, 10) == 10);
static_assert(at<3>(MockSelf{2}, 10) == 12);
static_assert(at<17>(MockSelf{16}, 10) == 26);
static_assert(at<64>(MockSelf{63}, 10) == 73);
static_assert(at<255>(MockSelf{254}, 10) == 264);
static_assert(at<256>(MockSelf{255}, 10) == 265);

// ---- constexpr: table path (N>256) ----
static_assert(at<257>(MockSelf{256}, 10) == 266);
static_assert(at<300>(MockSelf{299}, 10) == 309);
static_assert(at<300>(MockSelf{0}, 10) == 10);
static_assert(at<512>(MockSelf{511}, 10) == 521);

// ---- constexpr: per-I full coverage (incl. boundary N every index) ----
static_assert(all_engaged_ok<1>());
static_assert(all_engaged_ok<3>());
static_assert(all_engaged_ok<17>());
static_assert(all_engaged_ok<64>());
static_assert(all_engaged_ok<255>());
static_assert(all_engaged_ok<256>());
static_assert(all_engaged_ok<257>());
static_assert(all_engaged_ok<300>());
static_assert(all_engaged_ok<512>());

// ---- constexpr: void return path (R == void) ----
static_assert(at_void<3>(1) == 1);
static_assert(at_void<17>(16) == 1);
static_assert(at_void<300>(299) == 1);
static_assert(at_void<300>(0) == 1);

// ---- runtime: per-I scan + void path (bool return: explicit signal, unlike assert-based siblings) ----
bool test_at_index() {
  if (!all_engaged_ok<1>() || !all_engaged_ok<3>() || !all_engaged_ok<17>() ||
      !all_engaged_ok<64>() || !all_engaged_ok<255>() || !all_engaged_ok<256>() ||
      !all_engaged_ok<257>() || !all_engaged_ok<300>() || !all_engaged_ok<512>()) {
    std::puts("FAIL: engaged scan");
    return false;
  }
  if (at_void<3>(2) != 1 || at_void<257>(256) != 1 || at_void<512>(0) != 1) {
    std::puts("FAIL: void path");
    return false;
  }
  return true;
}

int main() {
  test_boundary();
  test_five_law_runtime();
  test_fuzz();
  test_private_dtor();
  ce_bool_niche();
  test_compare();
  test_swap();
  test_hash();
  test_visit();
  if (!test_at_index()) return 1;
  std::puts("ALL PASS");
  return 0;
}
