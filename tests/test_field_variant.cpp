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

#pragma once

#include <cassert>
#include <cstdio>
#include <string>

#include "field_variant.hpp"

// ============ Generic tests: tag storage, full coverage of value/reference/cv members ============

struct Widget {          // non-trivial destructor
  int tag = 0;
  ~Widget() { tag = -1; }
};

struct All {             // POD=int, non-POD=std::string
  int m0;
  volatile int m1;
  const int m2;
  volatile const int m3;
  int& m4;
  int&& m5;
  const int& m6;
  int* m7;
  std::string m8;
  volatile std::string m9;
  const std::string m10;
  volatile const std::string m11;
  std::string& m12;
  std::string&& m13;
  const std::string& m14;
  std::string* m15;
};

struct AllW {            // custom non-trivial type
  Widget w0;
  Widget& w1;
  Widget* w2;
};

template <class T>
struct R : stdex::storage_base<T> {};

// ============ niche tests: reference niche / bool niche / type index / get_if ============

// ---- niche switch (modeled after map::is_transparent): using is_niche = void; explicitly enables ----
// Types without the switch still use the tag version even when eligible (disabled by default)
struct Ref2   { int& a; std::string& b; using is_niche = void; };            // all references + switch: can use niche
struct Ref1   { int& a; using is_niche = void; };                            // single reference + switch: can use niche
struct Ref3   { int& a; std::string& b; double& c; using is_niche = void; }; // 3 references: 2 alignment bits suffice
struct NoNiche { int& a; char& b; using is_niche = void; };                  // char has alignment 1, no bits to borrow -> falls back to tag version
struct Mixed  { int x; std::string& s; };                                       // no switch + value member -> falls back to tag version
struct Ref2NoOpt { int& a; std::string& b; };                                   // eligible but no switch -> falls back to tag version

// ---- bool niche (Rust semantics): exactly 1 bool + remaining empty classes ----
struct Bool1  { bool a; using is_niche = void; };                              // single bool + switch: can use niche
struct Bool2  { bool a; struct None {} none; using is_niche = void; };         // bool + empty class + switch: can use niche
struct Bool3  { bool a; struct None {} none; struct Empty {} e; using is_niche = void; }; // bool + 2 empty classes
struct BoolFail { bool a; bool b; using is_niche = void; };                    // two bools: overlapping value ranges -> falls back to tag version
struct Bool1NoOpt { bool a; };                                                    // eligible but no switch -> falls back to tag version
struct Amb     { int a; int b; };                                                 // same-type members: ambiguous by type -> sentinel

// ---- lookup by type index (index_of, exact match including cvref) ----
static_assert(stdex::storage_meta<Ref2>::index_of<int&> == 0);          // int& member
static_assert(stdex::storage_meta<Ref2>::index_of<std::string&> == 1);  // string& member
static_assert(stdex::storage_meta<Ref2>::index_of<int> == 2);           // no exact match -> sentinel
static_assert(stdex::storage_meta<Ref2>::index_of<const int&> == 2);    // int& member != const int&
static_assert(stdex::storage_meta<Mixed>::index_of<int> == 0);
static_assert(stdex::storage_meta<Bool2>::index_of<bool> == 0);
static_assert(stdex::storage_meta<Bool2>::index_of<Bool2::None> == 1);
static_assert(stdex::storage_meta<Amb>::index_of<int> == 2);            // multiple matches -> sentinel

struct RefMix { int& a; int b; const int& c; };                            // user example: exact distinction among three types
static_assert(stdex::storage_meta<RefMix>::member_count == 3);
static_assert(std::is_same_v<stdex::storage_meta<RefMix>::member_type<0>, int&>);
static_assert(std::is_same_v<stdex::storage_meta<RefMix>::member_type<1>, int>);
static_assert(std::is_same_v<stdex::storage_meta<RefMix>::member_type<2>, const int&>);
static_assert(std::is_same_v<stdex::storage_meta<RefMix>::member_types, std::tuple<int&, int, const int&>>);
static_assert(stdex::storage_meta<RefMix>::index_of<int&> == 0);         // a
static_assert(stdex::storage_meta<RefMix>::index_of<int> == 1);          // b
static_assert(stdex::storage_meta<RefMix>::index_of<const int&> == 2);   // c
static_assert(stdex::storage_meta<RefMix>::index_of<const int> == 3);    // no exact match -> sentinel

// ---- has_member_type: whether the type list has an exact match ----
static_assert(stdex::storage_meta<Ref2>::has_member_type<int&>);
static_assert(stdex::storage_meta<Ref2>::has_member_type<std::string&>);
static_assert(!stdex::storage_meta<Ref2>::has_member_type<int>);       // exact match: int != int&
static_assert(!stdex::storage_meta<Ref2>::has_member_type<const int&>);
static_assert(stdex::storage_meta<RefMix>::has_member_type<int&>);
static_assert(stdex::storage_meta<RefMix>::has_member_type<int>);
static_assert(stdex::storage_meta<RefMix>::has_member_type<const int&>);
static_assert(!stdex::storage_meta<RefMix>::has_member_type<const int>);
static_assert(stdex::storage_meta<Amb>::has_member_type<int>);         // ambiguity still counts as present -> true
static_assert(stdex::storage_meta<Bool2>::has_member_type<bool>);
static_assert(stdex::storage_meta<Bool2>::has_member_type<Bool2::None>);

// ---- helper using: get the Ith member variable type of T (index first) ----
static_assert(std::is_same_v<stdex::member_type_t<0, Ref2>, int&>);
static_assert(std::is_same_v<stdex::member_type_t<1, Ref2>, std::string&>);
static_assert(std::is_same_v<stdex::member_type_t<0, RefMix>, int&>);
static_assert(std::is_same_v<stdex::member_type_t<1, RefMix>, int>);
static_assert(std::is_same_v<stdex::member_type_t<2, RefMix>, const int&>);
static_assert(std::is_same_v<stdex::member_type_t<0, Bool2>, bool>);
static_assert(std::is_same_v<stdex::member_type_t<1, Bool2>, Bool2::None>);

// ---- return types: get_if ----
static_assert(std::is_same_v<decltype(std::declval<stdex::storage_base<Ref2>&>().get_if<0>()), int*>);
static_assert(std::is_same_v<decltype(std::declval<stdex::storage_base<Ref2>&>().get_if<int&>()), int*>);
static_assert(std::is_same_v<decltype(std::declval<const stdex::storage_base<Ref2>&>().get_if<0>()), int*>);      // const does not propagate through reference members
static_assert(std::is_same_v<decltype(std::declval<stdex::storage_base<Mixed>&>().get_if<0>()), int*>);
static_assert(std::is_same_v<decltype(std::declval<const stdex::storage_base<Mixed>&>().get_if<0>()), const int*>); // const propagates for value members
static_assert(std::is_same_v<decltype(std::declval<stdex::storage_base<Bool2>&>().get_if<0>()), bool*>);

// ---- compile-time checks ----
static_assert(stdex::storage_meta<Ref2>::niche_capable);
static_assert(stdex::storage_meta<Ref1>::niche_capable);
static_assert(stdex::storage_meta<Ref3>::niche_capable);
static_assert(!stdex::storage_meta<NoNiche>::niche_capable);
static_assert(!stdex::storage_meta<Mixed>::niche_capable);
static_assert(!stdex::storage_meta<Ref2NoOpt>::niche_capable);   // no switch -> not enabled

static_assert(stdex::storage_meta<Bool1>::niche_bool_capable);
static_assert(stdex::storage_meta<Bool2>::niche_bool_capable);
static_assert(stdex::storage_meta<Bool3>::niche_bool_capable);
static_assert(!stdex::storage_meta<BoolFail>::niche_bool_capable);
static_assert(!stdex::storage_meta<BoolFail>::niche_capable);
static_assert(!stdex::storage_meta<Bool1NoOpt>::niche_bool_capable); // no switch -> not enabled

// ---- sizes: niche version is always 8 bytes (no tag field), tag version as before ----
static_assert(sizeof(stdex::storage_base<Ref2>) == 8);   // old scheme was 16 -> saves 8
static_assert(sizeof(stdex::storage_base<Ref1>) == 8);
static_assert(sizeof(stdex::storage_base<Ref3>) == 8);
static_assert(sizeof(stdex::storage_base<NoNiche>) == 16);
static_assert(sizeof(stdex::storage_base<Mixed>) == 16); // string& adapts to pointer 8 + tag 1 -> aligned to 8
static_assert(sizeof(stdex::storage_base<Ref2NoOpt>) == 16); // no switch -> tag version

// ---- sizes: bool niche is always 1 byte (tag version is 2 bytes) ----
static_assert(sizeof(stdex::storage_base<Bool1>) == 1);
static_assert(sizeof(stdex::storage_base<Bool2>) == 1);
static_assert(sizeof(stdex::storage_base<Bool3>) == 1);
static_assert(sizeof(stdex::storage_base<BoolFail>) == 2); // tag version: byte[1] + tag[1]
static_assert(sizeof(stdex::storage_base<Bool1NoOpt>) == 2); // no switch -> tag version

// ---- return types: const does not propagate through reference members ----
static_assert(std::is_same_v<decltype(std::declval<stdex::storage_base<Ref2>&>().get<0>()), int&>);
static_assert(std::is_same_v<decltype(std::declval<stdex::storage_base<Ref2>&>().get<1>()), std::string&>);
static_assert(std::is_same_v<decltype(std::declval<const stdex::storage_base<Ref2>&>().get<1>()), std::string&>);

// ---- return types: bool member -> real bool& (const object -> const bool&); empty-class member -> by value ----
static_assert(std::is_same_v<decltype(std::declval<stdex::storage_base<Bool2>&>().get<0>()), bool&>);
static_assert(std::is_same_v<decltype(std::declval<const stdex::storage_base<Bool2>&>().get<0>()), const bool&>);
static_assert(std::is_same_v<decltype(std::declval<stdex::storage_base<Bool2>&>().get<1>()), Bool2::None>);

int main() {
  // ============ generic version (tag): All covers all member types ============
  R<All> rs;
  int iv = 1;
  int iv2 = 2;
  std::string sv = "sv";
  std::string sv2 = "sv2";

  rs.construct<0>(42);            // int
  static_assert(std::is_same_v<decltype(rs.get<0>()), int&>);
  rs.get<0>() = 43;
  rs.destroy<0>();

  rs.construct<1>(7);             // volatile int
  static_assert(std::is_same_v<decltype(rs.get<1>()), volatile int&>);
  rs.destroy<1>();

  rs.construct<2>(8);             // const int
  static_assert(std::is_same_v<decltype(rs.get<2>()), const int&>);
  rs.destroy<2>();

  rs.construct<3>(9);             // volatile const int
  static_assert(std::is_same_v<decltype(rs.get<3>()), const volatile int&>);
  rs.destroy<3>();

  rs.construct<4>(iv);            // int&
  static_assert(std::is_same_v<decltype(rs.get<4>()), int&>);
  rs.get<4>() = 99;
  rs.destroy<4>();
  assert(iv == 99);

  rs.construct<5>(iv2);           // int&& binds as lvalue and stores a pointer
  static_assert(std::is_same_v<decltype(rs.get<5>()), int&>);
  rs.destroy<5>();

  rs.construct<6>(iv);            // const int&
  static_assert(std::is_same_v<decltype(rs.get<6>()), const int&>);
  rs.destroy<6>();

  rs.construct<7>(&iv);           // int*
  static_assert(std::is_same_v<decltype(rs.get<7>()), int*&>);
  *rs.get<7>() = 100;
  rs.destroy<7>();
  assert(iv == 100);

  rs.construct<8>(std::string("a"));   // string
  static_assert(std::is_same_v<decltype(rs.get<8>()), std::string&>);
  rs.get<8>() += "b";
  rs.destroy<8>();

  rs.construct<9>(std::string("v"));   // volatile string
  static_assert(std::is_same_v<decltype(rs.get<9>()), volatile std::string&>);
  rs.destroy<9>();

  rs.construct<10>(std::string("c"));  // const string
  static_assert(std::is_same_v<decltype(rs.get<10>()), const std::string&>);
  rs.destroy<10>();

  rs.construct<11>(std::string("vc")); // volatile const string
  static_assert(std::is_same_v<decltype(rs.get<11>()), const volatile std::string&>);
  rs.destroy<11>();

  rs.construct<12>(sv);            // string&
  static_assert(std::is_same_v<decltype(rs.get<12>()), std::string&>);
  rs.get<12>() += "x";
  rs.destroy<12>();
  assert(sv == "svx");

  rs.construct<13>(sv2);           // string&& binds as lvalue
  static_assert(std::is_same_v<decltype(rs.get<13>()), std::string&>);
  rs.destroy<13>();

  rs.construct<14>(sv);            // const string&
  static_assert(std::is_same_v<decltype(rs.get<14>()), const std::string&>);
  rs.destroy<14>();

  rs.construct<15>(&sv);           // string*
  static_assert(std::is_same_v<decltype(rs.get<15>()), std::string*&>);
  rs.get<15>()->append("y");
  rs.destroy<15>();
  assert(sv == "svxy");

  rs.construct<0>(1);              // const object access
  R<All> const& crs = rs;
  static_assert(std::is_same_v<decltype(crs.get<0>()), const int&>);
  static_assert(std::is_same_v<decltype(crs.get<1>()), const volatile int&>);
  static_assert(std::is_same_v<decltype(crs.get<4>()), int&>);        // reference members do not propagate const
  static_assert(std::is_same_v<decltype(crs.get<5>()), int&>);
  static_assert(std::is_same_v<decltype(crs.get<6>()), const int&>);
  static_assert(std::is_same_v<decltype(crs.get<7>()), int* const&>);
  static_assert(std::is_same_v<decltype(crs.get<8>()), const std::string&>);
  static_assert(std::is_same_v<decltype(crs.get<12>()), std::string&>);
  static_assert(std::is_same_v<decltype(crs.get<14>()), const std::string&>);
  rs.destroy<0>();

  static_assert(std::is_same_v<decltype(std::move(rs).get<0>()), int&&>);
  static_assert(std::is_same_v<decltype(std::move(rs).get<4>()), int&>);  // reference members do not forward_like
  static_assert(std::is_same_v<decltype(std::move(rs).get<8>()), std::string&&>);

  // custom non-trivially-destructible type
  R<AllW> rw;
  Widget wt;
  wt.tag = 7;
  rw.construct<0>(Widget{});
  rw.destroy<0>();                 // explicit destructor is called
  rw.construct<1>(wt);             // Widget&
  static_assert(std::is_same_v<decltype(rw.get<1>()), Widget&>);
  rw.get<1>().tag = 8;             // modify the original object through the reference
  rw.destroy<1>();
  assert(wt.tag == 8);
  rw.construct<2>(&wt);            // Widget*
  static_assert(std::is_same_v<decltype(rw.get<2>()), Widget*&>);
  rw.destroy<2>();
  assert(wt.tag == 8);             // destroying a reference member does not affect the referenced object

  // ============ niche version: reference niche ============
  int x = 1;
  std::string s = "hello";

  { // two-reference niche
    stdex::storage_base<Ref2> r;
    assert(!r.has_value());
    r.construct<0>(x);
    assert(r.has_value() && r.index() == 0);
    r.get<0>() = 2;
    assert(x == 2);                                // reference binding takes effect
    r.destroy<0>();
    assert(!r.has_value() && r.index() == 2);      // disengaged index == member_count

    r.construct<1>(s);
    assert(r.index() == 1);
    r.get<1>() += "!";
    assert(s == "hello!");                         // modification is visible through the reference
    const auto& cr = r;
    assert(cr.get<1>() == "hello!");               // get on const object: const does not propagate through
    r.destroy<1>();
    assert(!r.has_value());
  }

  { // single-reference niche (mask=0 degenerate path)
    stdex::storage_base<Ref1> r;
    r.construct<0>(x);
    assert(r.index() == 0 && r.get<0>() == 2);
    r.destroy<0>();
    assert(r.index() == 1);
  }

  { // three-reference niche
    double d = 3.14;
    stdex::storage_base<Ref3> r;
    r.construct<2>(d);
    assert(r.index() == 2 && r.get<2>() == 3.14);
    r.destroy<2>();
    assert(r.index() == 3);
  }

  { // insufficient alignment -> falls back to tag version (reference branch still works normally)
    char c = 'z';
    stdex::storage_base<NoNiche> r;
    r.construct<1>(c);
    assert(r.index() == 1 && r.get<1>() == 'z');
    r.destroy<1>();
    assert(r.index() == 2);
  }

  { // contains a value member -> falls back to tag version
    stdex::storage_base<Mixed> r;
    r.construct<0>(42);
    assert(r.index() == 0 && r.get<0>() == 42);
    r.destroy<0>();
    std::string s2 = "world";
    r.construct<1>(s2);
    assert(r.index() == 1 && r.get<1>() == "world");
    r.destroy<1>();
    assert(r.index() == 2);
  }

  { // bool niche: bool + empty class
    stdex::storage_base<Bool2> r;
    assert(!r.has_value() && r.index() == 2);
    r.construct<0>(true);
    assert(r.has_value() && r.index() == 0);
    assert(r.get<0>() == true);
    r.get<0>() = false;                        // bool& is writable without breaking the encoding
    assert(r.get<0>() == false);
    const auto& cr = r;
    assert(cr.get<0>() == false);              // const object -> const bool&
    r.destroy<0>();
    assert(!r.has_value() && r.index() == 2);

    r.construct<1>();                          // empty variant: no args (unit semantics)
    assert(r.has_value() && r.index() == 1);
    [[maybe_unused]] Bool2::None n = r.get<1>(); // returns by value
    r.destroy<1>();
    assert(!r.has_value() && r.index() == 2);
  }

  { // bool + 2 empty classes: empty variants encode as 2/3, index() reverse lookup
    stdex::storage_base<Bool3> r;
    r.construct<2>();
    assert(r.index() == 2);
    r.destroy<2>();
    r.construct<1>();
    assert(r.index() == 1);
    r.destroy<1>();
    r.construct<0>(true);
    assert(r.index() == 0 && r.get<0>());
    r.destroy<0>();
    assert(r.index() == 3);
  }

  { // single bool: optional<bool> semantics, 1 byte
    stdex::storage_base<Bool1> r;
    assert(!r.has_value() && r.index() == 1);
    r.construct<0>(false);
    assert(r.index() == 0 && r.get<0>() == false);
    r.destroy<0>();
    assert(!r.has_value() && r.index() == 1);
  }

  { // two bools: overlapping value ranges -> falls back to tag version
    stdex::storage_base<BoolFail> r;
    r.construct<1>(true);
    assert(r.index() == 1 && r.get<1>() == true);
    r.destroy<1>();
    assert(r.index() == 2);
  }

  { // get by type: reference-niche version (forwards to indexed get, exact match)
    stdex::storage_base<Ref2> r;
    int x = 5;
    std::string s = "abc";
    r.construct<0>(x);
    assert(r.get<int&>() == 5);
    r.get<int&>() = 6;                             // exact match on reference member, writable
    assert(x == 6);
    const auto& cr = r;
    assert(cr.get<int&>() == 6);                   // const does not propagate through reference members, still returns int&
    r.destroy<0>();
    r.construct<1>(s);
    assert(r.get<std::string&>() == "abc");
    r.destroy<1>();
  }

  { // get by type: tag version (value member)
    stdex::storage_base<Mixed> m;
    m.construct<0>(42);
    assert(m.get<int>() == 42);
    m.destroy<0>();

    stdex::storage_base<Bool2> b;
    b.construct<0>(true);
    assert(b.get<bool>() == true);
    b.destroy<0>();
    b.construct<1>();
    [[maybe_unused]] Bool2::None n2 = b.get<Bool2::None>(); // empty variant returns by value
    b.destroy<1>();
  }

  { // get_if: safe query path (returns nullptr on mismatch, no assert triggered)
    stdex::storage_base<Mixed> m;
    m.construct<0>(42);
    assert(m.get_if<0>() != nullptr && *m.get_if<0>() == 42);
    assert(m.get_if<1>() == nullptr);                // inactive variant -> nullptr
    m.destroy<0>();
    assert(m.get_if<0>() == nullptr);                // disengaged → nullptr
    m.construct<0>(7);
    assert(*m.get_if<int>() == 7);                   // get_if by type
    m.destroy<0>();

    stdex::storage_base<Ref2> r;
    int x = 9;
    r.construct<0>(x);
    assert(r.get_if<int&>() == &x);                  // reference member -> points to the referenced object
    r.destroy<0>();
    assert(r.get_if<0>() == nullptr);

    stdex::storage_base<Bool2> b;
    b.construct<0>(true);
    assert(b.get_if<bool>() != nullptr && *b.get_if<bool>());
    b.destroy<0>();
    assert(b.get_if<0>() == nullptr);
  }

  { // exact match distinguishes three types (user example: int&/int/const int&)
    stdex::storage_base<RefMix> r;
    int x = 1;
    const int z = 3;
    r.construct<0>(x);
    assert(r.get<int&>() == 1);
    r.destroy<0>();
    r.construct<1>(42);
    assert(r.get<int>() == 42);
    r.destroy<1>();
    r.construct<2>(z);
    assert(r.get<const int&>() == 3);
    r.destroy<2>();
  }

  { // no switch -> falls back to tag version (disabled by default)
    stdex::storage_base<Ref2NoOpt> r;
    int x = 7;
    r.construct<0>(x);
    assert(r.index() == 0 && r.get<0>() == 7);
    r.destroy<0>();
    assert(r.index() == 2);
  }

  std::puts("ALL PASS");
}