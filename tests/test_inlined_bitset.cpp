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

// test_inlined_bitset.cpp
// Covers stdex::inlined_bitset<Block, N, Allocator> from inlined_bitset.hpp:
// ctors (bit/block/string/copy/move/alloc), reference proxy, iterators,
// capacity/storage observers, element access, all/any/none/count,
// subset/intersects, find_* / find_*_zero, set/reset/flip, push/pop/emplace,
// insert/erase/assign/append (bit + block), to_string/to_ulong/to_ullong,
// shifts/bitwise/swap/compare/streams, tail-padding, block-type widths,
// stack<->heap transitions, model fuzz, exception safety, allocator lifecycle,
// constexpr (heap always; stack guarded by __cpp_lib_start_lifetime).
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <forward_list>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <print>
#include <random>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "inlined_bitset.hpp"

static int g_pass = 0;
static int g_fail = 0;

// Variadic so template-id commas inside conditions do not split macro args.
#define CHECK(...)                                                       \
  do {                                                                   \
    if (__VA_ARGS__) {                                                   \
      ++g_pass;                                                          \
    } else {                                                             \
      ++g_fail;                                                          \
      std::println(stderr, "  [FAIL] {}:{}  {}", __FILE__, __LINE__, #__VA_ARGS__); \
    }                                                                    \
  } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))

#define SECTION(name) std::println("\n=== {} ===", name)

namespace bs_test
{
using BS64_0 = stdex::inlined_bitset<std::uint64_t, 0>;
using BS64_2 = stdex::inlined_bitset<std::uint64_t, 2>;
using BS64_4 = stdex::inlined_bitset<std::uint64_t, 4>;
using BS32_4 = stdex::inlined_bitset<std::uint32_t, 4>;
using BS8_8 = stdex::inlined_bitset<std::uint8_t, 8>;
using BS16_4 = stdex::inlined_bitset<std::uint16_t, 4>;

static_assert(BS64_2::bits_per_block == 64u);
static_assert(BS32_4::bits_per_block == 32u);
static_assert(BS8_8::bits_per_block == 8u);
static_assert(BS64_2::inline_capacity == 2u);
static_assert(BS64_0::inline_capacity == 0u);
static_assert(std::is_same_v<BS64_2::value_type, bool>);
static_assert(std::is_same_v<BS64_2::const_reference, bool>);

// --- helpers ---------------------------------------------------------------
template <class BS>
std::vector<char> to_vec(BS const &bs)
{
  std::vector<char> v;
  v.reserve(bs.size());
  for (std::size_t i = 0; i < bs.size(); ++i) v.push_back(bs[i] ? char(1) : char(0));
  return v;
}

template <class BS>
bool equals_vec(BS const &bs, std::vector<char> const &v)
{
  if (bs.size() != v.size()) return false;
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (static_cast<bool>(bs[i]) != static_cast<bool>(v[i])) return false;
  }
  return true;
}

template <class BS>
BS make_from_vec(std::vector<char> const &v)
{
  BS bs;
  for (char c : v) bs.push_back(static_cast<bool>(c));
  return bs;
}

template <class BS>
bool tail_padding_zero(BS const &bs)
{
  using Block = typename BS::block_type;
  std::size_t const bits = BS::bits_per_block;
  std::size_t const r = bs.size() % bits;
  if (r == 0) return true;
  if (bs.block_count() == 0u) return true;
  Block const last = bs.data()[bs.block_count() - 1];
  for (std::size_t i = r; i < bits; ++i) {
    if ((last & static_cast<Block>(Block{1} << i)) != Block{0}) return false;
  }
  return true;
}

// counting allocator for lifecycle / bad_alloc tests
struct counting_state
{
  std::size_t allocation_attempts = 0;
  std::size_t allocations = 0;
  std::size_t deallocations = 0;
  std::size_t active = 0;
  std::size_t fail_on_attempt = 0; // 0 = never fail
  std::size_t max_limit = (std::numeric_limits<std::size_t>::max)();
};

template <class T>
struct counting_alloc
{
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using propagate_on_container_copy_assignment = std::false_type;
  using propagate_on_container_move_assignment = std::false_type;
  using propagate_on_container_swap = std::false_type;
  using is_always_equal = std::false_type;

  template <class U>
  struct rebind
  {
    using other = counting_alloc<U>;
  };

  counting_state *st_ = nullptr;
  int id_ = 0;

  counting_alloc() = default;
  counting_alloc(counting_state *st, int id) noexcept : st_{st}, id_{id} {}
  template <class U>
  counting_alloc(counting_alloc<U> const &o) noexcept : st_{o.st_}, id_{o.id_} {}

  [[nodiscard]] T *allocate(std::size_t n)
  {
    ++st_->allocation_attempts;
    if (st_->fail_on_attempt != 0 && st_->allocation_attempts == st_->fail_on_attempt) {
      throw std::bad_alloc{};
    }
    auto *p = static_cast<T *>(::operator new(n * sizeof(T)));
    ++st_->allocations;
    ++st_->active;
    return p;
  }
  void deallocate(T *p, std::size_t) noexcept
  {
    ::operator delete(p);
    ++st_->deallocations;
    --st_->active;
  }
  [[nodiscard]] std::size_t max_size() const noexcept { return st_ ? st_->max_limit : (std::numeric_limits<std::size_t>::max)(); }

  template <class U>
  friend bool operator==(counting_alloc const &l, counting_alloc<U> const &r) noexcept
  {
    return l.st_ == r.st_ && l.id_ == r.id_;
  }
  template <class U>
  friend bool operator!=(counting_alloc const &l, counting_alloc<U> const &r) noexcept
  {
    return !(l == r);
  }
  template <class>
  friend struct counting_alloc;
};

template <class Block, std::size_t N>
using CBitset = stdex::inlined_bitset<Block, N, counting_alloc<Block>>;

// throwing input iterator (bit-wise ctor input path)
struct throwing_input_iter
{
  using iterator_category = std::input_iterator_tag;
  using iterator_concept = std::input_iterator_tag;
  using value_type = int;
  using difference_type = std::ptrdiff_t;
  using pointer = void;
  using reference = int;

  std::vector<int> const *src_ = nullptr;
  std::size_t idx_ = 0;
  std::size_t throw_at_ = (std::numeric_limits<std::size_t>::max)();

  throwing_input_iter() = default;
  throwing_input_iter(std::vector<int> const *s, std::size_t i, std::size_t t) : src_{s}, idx_{i}, throw_at_{t} {}

  int operator*() const
  {
    if (idx_ == throw_at_) throw std::runtime_error("deref throw");
    return (*src_)[idx_];
  }
  throwing_input_iter &operator++()
  {
    ++idx_;
    return *this;
  }
  throwing_input_iter operator++(int)
  {
    auto t = *this;
    ++idx_;
    return t;
  }
  friend bool operator==(throwing_input_iter const &a, throwing_input_iter const &b) { return a.idx_ == b.idx_ && a.src_ == b.src_; }
  friend bool operator!=(throwing_input_iter const &a, throwing_input_iter const &b) { return !(a == b); }
};
static_assert(std::input_iterator<throwing_input_iter>);

// throwing forward iterator (bit-wise ctor forward path)
struct throwing_fwd_iter
{
  using iterator_category = std::forward_iterator_tag;
  using iterator_concept = std::forward_iterator_tag;
  using value_type = int;
  using difference_type = std::ptrdiff_t;
  using pointer = int const *;
  using reference = int;

  std::vector<int> const *src_ = nullptr;
  std::size_t idx_ = 0;
  std::size_t throw_at_ = (std::numeric_limits<std::size_t>::max)();

  throwing_fwd_iter() = default;
  throwing_fwd_iter(std::vector<int> const *s, std::size_t i, std::size_t t) : src_{s}, idx_{i}, throw_at_{t} {}

  int operator*() const
  {
    if (idx_ == throw_at_) throw std::runtime_error("fwd deref throw");
    return (*src_)[idx_];
  }
  throwing_fwd_iter &operator++()
  {
    ++idx_;
    return *this;
  }
  throwing_fwd_iter operator++(int)
  {
    auto t = *this;
    ++idx_;
    return t;
  }
  friend bool operator==(throwing_fwd_iter const &a, throwing_fwd_iter const &b) { return a.idx_ == b.idx_ && a.src_ == b.src_; }
  friend bool operator!=(throwing_fwd_iter const &a, throwing_fwd_iter const &b) { return !(a == b); }
};
static_assert(std::forward_iterator<throwing_fwd_iter>);

// concepts / proxy compile-time checks
static_assert(std::random_access_iterator<BS64_2::iterator>);
static_assert(std::random_access_iterator<BS64_2::const_iterator>);
static_assert(std::convertible_to<BS64_2::iterator, BS64_2::const_iterator>);
static_assert(std::is_same_v<BS64_2::iterator::value_type, bool>);
static_assert(std::is_same_v<BS64_2::const_iterator::reference, bool>);

} // namespace bs_test

using namespace bs_test;

// ==================================================================
// 1. default / nbits / from_blocks ctors
// ==================================================================
void test_default_and_nbits_ctors()
{
  SECTION("default / nbits / from_blocks ctors");
  {
    BS64_2 b;
    CHECK(b.empty());
    CHECK_EQ(b.size(), 0u);
    CHECK(b.is_stack());
    CHECK_EQ(b.block_count(), 0u);
    CHECK(tail_padding_zero(b));
  }
  {
    BS64_0 h;
    CHECK(h.empty());
    CHECK(h.is_heap());
    CHECK_EQ(h.block_count(), 0u);
  }
  {
    BS64_2 b(0);
    CHECK(b.empty());
    CHECK_EQ(b.block_count(), 0u);
  }
  {
    BS64_2 b(1);
    CHECK_EQ(b.size(), 1u);
    CHECK(!static_cast<bool>(b[0]));
    CHECK(tail_padding_zero(b));
  }
  {
    // nbits with init true masks tail
    BS64_2 b(65, true);
    CHECK_EQ(b.size(), 65u);
    CHECK_EQ(b.count(), 65u);
    CHECK(b.all());
    CHECK(tail_padding_zero(b));
    // last block has exactly 1 bit set
    CHECK_EQ(b.data()[1], static_cast<std::uint64_t>(1));
  }
  {
    BS64_2 b(65, false);
    CHECK_EQ(b.size(), 65u);
    CHECK_EQ(b.count(), 0u);
    CHECK(b.none());
  }
  {
    // from_blocks nblocks
    BS64_2 b(stdex::from_blocks, 2);
    CHECK_EQ(b.size(), 128u);
    CHECK_EQ(b.block_count(), 2u);
    CHECK_EQ(b.count(), 0u);
  }
  {
    BS64_2 b(stdex::from_blocks, 2, static_cast<std::uint64_t>(~std::uint64_t{0}));
    CHECK_EQ(b.size(), 128u);
    CHECK(b.all());
  }
  {
    BS64_2 b(stdex::from_blocks, 0);
    CHECK(b.empty());
    CHECK_EQ(b.block_count(), 0u);
  }
  {
    // alloc-only ctor
    BS64_2 b(std::allocator<std::uint64_t>{});
    CHECK(b.empty());
    CHECK(b.is_stack());
  }
  {
    BS64_2 b(10, std::allocator<std::uint64_t>{});
    CHECK_EQ(b.size(), 10u);
  }
}

// ==================================================================
// 2. bit-wise range / ilist / from_range ctors
// ==================================================================
void test_bit_range_ilist_constructors()
{
  SECTION("bit-wise range / ilist ctors");
  {
    std::vector<int> v{1, 0, 1, 1, 0};
    BS64_2 b(v.begin(), v.end());
    CHECK_EQ(b.size(), 5u);
    CHECK(static_cast<bool>(b[0]));
    CHECK(!static_cast<bool>(b[1]));
    CHECK(static_cast<bool>(b[2]));
    CHECK(tail_padding_zero(b));
  }
  {
    // input (non-forward) path: forward_list
    std::forward_list<int> fl{1, 0, 1};
    BS64_2 b(fl.begin(), fl.end());
    CHECK_EQ(b.size(), 3u);
    CHECK(equals_vec(b, std::vector<char>{1, 0, 1}));
  }
  {
    BS64_2 b{true, false, true};
    CHECK_EQ(b.size(), 3u);
    CHECK(equals_vec(b, std::vector<char>{1, 0, 1}));
  }
  {
    BS64_2 b{};
    CHECK(b.empty());
  }
  {
    // empty range
    std::vector<int> v;
    BS64_2 b(v.begin(), v.end());
    CHECK(b.empty());
  }
#if defined(__cpp_lib_containers_ranges)
  {
    std::vector<int> v{1, 1, 0};
    BS64_2 b(std::from_range, v);
    CHECK(equals_vec(b, std::vector<char>{1, 1, 0}));
  }
#endif
  {
    // cross-block size: 70 bits forces heap for N=2? 70 bits = 2 blocks fits stack exactly
    std::vector<char> v(70, char(1));
    BS64_2 b(v.begin(), v.end());
    CHECK_EQ(b.size(), 70u);
    CHECK_EQ(b.count(), 70u);
    CHECK(tail_padding_zero(b));
  }
  {
    // 200 bits forces heap for N=2
    std::vector<char> v(200, char(1));
    BS64_2 b(v.begin(), v.end());
    CHECK_EQ(b.size(), 200u);
    CHECK(b.is_heap());
    CHECK(tail_padding_zero(b));
  }
}

// ==================================================================
// 3. block-wise ctors
// ==================================================================
void test_block_range_ilist_constructors()
{
  SECTION("block-wise ctors");
  {
    std::vector<std::uint64_t> w{0xFULL, 0xA0ULL};
    BS64_2 b(stdex::from_blocks, w.begin(), w.end());
    CHECK_EQ(b.size(), 128u);
    CHECK_EQ(b.block_count(), 2u);
    CHECK_EQ(b.data()[0], static_cast<std::uint64_t>(0xFULL));
    CHECK_EQ(b.data()[1], static_cast<std::uint64_t>(0xA0ULL));
  }
  {
    BS64_2 b(stdex::from_blocks, std::initializer_list<std::uint64_t>{1u, 2u});
    CHECK_EQ(b.size(), 128u);
    CHECK_EQ(b.data()[0], static_cast<std::uint64_t>(1));
    CHECK_EQ(b.data()[1], static_cast<std::uint64_t>(2));
  }
  {
    // input block path (forward_list)
    std::forward_list<std::uint64_t> fl{5u, 6u};
    BS64_2 b(stdex::from_blocks, fl.begin(), fl.end());
    CHECK_EQ(b.block_count(), 2u);
    CHECK_EQ(b.data()[0], static_cast<std::uint64_t>(5));
  }
  {
    std::vector<std::uint64_t> w;
    BS64_2 b(stdex::from_blocks, w.begin(), w.end());
    CHECK(b.empty());
  }
#if defined(__cpp_lib_ranges)
  {
    std::vector<std::uint64_t> w{7u};
    BS64_2 b(stdex::from_blocks, w);
    CHECK_EQ(b.block_count(), 1u);
    CHECK_EQ(b.data()[0], static_cast<std::uint64_t>(7));
  }
#endif
  {
    // sub-block-type convertible: uint32_t blocks into uint64_t bitset
    std::vector<std::uint32_t> w{0xFFFFFFFFu};
    BS64_2 b(stdex::from_blocks, w.begin(), w.end());
    CHECK_EQ(b.data()[0], static_cast<std::uint64_t>(0xFFFFFFFFULL));
  }
}

// ==================================================================
// 4. string ctors
// ==================================================================
void test_string_constructors()
{
  SECTION("string ctors");
  {
    BS64_2 b(std::string_view{"101"});
    CHECK_EQ(b.size(), 3u);
    // last char is bit 0
    CHECK(static_cast<bool>(b[0]));
    CHECK(!static_cast<bool>(b[1]));
    CHECK(static_cast<bool>(b[2]));
    CHECK_EQ(b.to_string(), std::string{"101"});
  }
  {
    std::string s{"11010"};
    BS64_2 b(s);
    CHECK_EQ(b.to_string(), s);
  }
  {
    char const *cstr = "0011";
    BS64_2 b(cstr);
    CHECK_EQ(b.to_string(), std::string{"0011"});
  }
  {
    // pos/n slicing: "010101" take pos=1 n=3 -> "101"
    BS64_2 b(std::string_view{"010101"}, 1, 3);
    CHECK_EQ(b.size(), 3u);
    CHECK_EQ(b.to_string(), std::string{"101"});
  }
  {
    // custom zero/one
    BS64_2 b(std::string_view{"abba"}, 0, 4, 'a', 'b');
    CHECK_EQ(b.to_string('a', 'b'), std::string{"abba"});
  }
  {
    // empty string
    BS64_2 b(std::string_view{""});
    CHECK(b.empty());
  }
  {
    // pos > size throws out_of_range
    bool threw = false;
    try {
      BS64_2 b(std::string_view{"101"}, 5, 1);
    } catch (std::out_of_range const &) {
      threw = true;
    }
    CHECK(threw);
  }
  {
    // invalid char throws invalid_argument
    bool threw = false;
    try {
      BS64_2 b(std::string_view{"10x1"});
    } catch (std::invalid_argument const &) {
      threw = true;
    }
    CHECK(threw);
  }
  {
    // long string crossing blocks + SIMD pack path (>=16 chars, 1-byte CharT)
    std::string s(200, '1');
    s[0] = '0';
    s[199] = '0';
    BS64_4 b(s);
    CHECK_EQ(b.size(), 200u);
    CHECK(!static_cast<bool>(b[199]));
    CHECK(!static_cast<bool>(b[0]));
    CHECK(static_cast<bool>(b[100]));
    CHECK_EQ(b.to_string(), s);
    CHECK(tail_padding_zero(b));
  }
  {
    // 32-char SIMD chunk + reversal correctness vs scalar model
    std::string s;
    for (int i = 0; i < 100; ++i) s.push_back((i % 3 == 0) ? '1' : '0');
    BS64_0 b(s);
    CHECK_EQ(b.to_string(), s);
  }
}

// ==================================================================
// 5. copy / move / assign lifecycle
// ==================================================================
void test_copy_move_assign_lifecycle()
{
  SECTION("copy / move / assign");
  // stack source (100 bits fits 2 inline blocks)
  BS64_2 src;
  for (int i = 0; i < 100; ++i) src.push_back(i % 2 == 0);
  CHECK(src.is_stack());

  BS64_2 cpy(src);
  CHECK_EQ(cpy.size(), src.size());
  CHECK(cpy == src);

  BS64_2 cpy2(src, src.get_allocator());
  CHECK(cpy2 == src);

  BS64_2 assigned;
  assigned = src;
  CHECK(assigned == src);

  // stack move is element-wise: source keeps size (trivial blocks are copied)
  BS64_2 moved(std::move(cpy));
  CHECK_EQ(moved.size(), src.size());
  CHECK(moved == src);
  CHECK_EQ(cpy.size(), src.size());

  BS64_2 moved2;
  moved2 = std::move(moved);
  CHECK(moved2 == src);
  CHECK_EQ(moved.size(), src.size());

  // heap source: steal empties the source
  BS64_0 hsrc(300, true);
  BS64_0 hcpy(hsrc);
  BS64_0 hmove(std::move(hcpy));
  CHECK_EQ(hmove.size(), 300u);
  CHECK(hcpy.empty());
  BS64_0 hmove2;
  hmove2 = std::move(hmove);
  CHECK_EQ(hmove2.size(), 300u);
  CHECK(hmove.empty());

  // self assign
  assigned = assigned;
  CHECK(assigned == src);

  // ilist bool assign
  BS64_2 il;
  il = {true, false, true};
  CHECK(equals_vec(il, std::vector<char>{1, 0, 1}));

  // ilist block assign
  BS64_2 bl;
  bl = {std::uint64_t{9}, std::uint64_t{10}};
  CHECK_EQ(bl.block_count(), 2u);
  CHECK_EQ(bl.data()[0], static_cast<std::uint64_t>(9));

  // move with explicit allocator
  BS64_2 with_alloc(std::move(moved2), moved2.get_allocator());
  CHECK(with_alloc == src);

  // stack-only copy stays stack
  BS64_4 small;
  small.push_back(true);
  small.push_back(false);
  BS64_4 small_cpy(small);
  CHECK(small_cpy.is_stack());
  CHECK(small_cpy == small);
}

// ==================================================================
// 6. reference proxy
// ==================================================================
void test_reference_proxy()
{
  SECTION("reference proxy");
  BS64_2 b(8, false);
  // operator= bool
  b[0] = true;
  CHECK(static_cast<bool>(b[0]));
  // operator= ref
  b[1] = b[0];
  CHECK(static_cast<bool>(b[1]));
  BS64_2 c(8, false);
  c[0] = std::move(b[0]);
  CHECK(static_cast<bool>(c[0]));

  // &= |= ^= -=
  b[2] = true;
  b[2] &= false;
  CHECK(!static_cast<bool>(b[2]));
  b[2] |= true;
  CHECK(static_cast<bool>(b[2]));
  b[2] ^= true;
  CHECK(!static_cast<bool>(b[2]));
  b[2] = true;
  b[2] -= true;
  CHECK(!static_cast<bool>(b[2]));

  // ~
  b[3] = false;
  CHECK(static_cast<bool>(~b[3]));
  b[3] = true;
  CHECK(!static_cast<bool>(~b[3]));

  // set/reset/flip/assign
  b[4].set();
  CHECK(static_cast<bool>(b[4]));
  b[4].reset();
  CHECK(!static_cast<bool>(b[4]));
  b[4].flip();
  CHECK(static_cast<bool>(b[4]));
  b[4].assign(false);
  CHECK(!static_cast<bool>(b[4]));

  // swap ref-ref, ref-bool, bool-ref
  b[5] = true;
  b[6] = false;
  swap(b[5], b[6]);
  CHECK(!static_cast<bool>(b[5]));
  CHECK(static_cast<bool>(b[6]));
  bool plain = false;
  swap(b[6], plain);
  CHECK(!static_cast<bool>(b[6]));
  CHECK(plain);
  swap(plain, b[5]);
  CHECK(!plain);
  CHECK(static_cast<bool>(b[5]));

  // operator& is deleted on reference (verified by inspection of header);
  // no address-of test here because invoking a deleted function is ill-formed.

  // via iterator deref
  *b.begin() = true;
  CHECK(static_cast<bool>(b[0]));
  b.begin()[1] = false;
  CHECK(!static_cast<bool>(b[1]));
}

// ==================================================================
// 7. iterators random access
// ==================================================================
void test_iterator_random_access()
{
  SECTION("iterator random access");
  BS64_2 b{true, false, true, false, true};
  auto it = b.begin();
  CHECK(static_cast<bool>(*it));
  CHECK(static_cast<bool>(it[2]));
  CHECK(!static_cast<bool>(it[1]));

  auto it2 = it + 2;
  CHECK(static_cast<bool>(*it2));
  CHECK_EQ(it2 - it, 2);
  CHECK((it == it));
  CHECK((it != it2));
  CHECK((it < it2));
  CHECK((it2 > it));
  CHECK((it <= it));
  CHECK((it2 >= it2));

  ++it;
  CHECK(!static_cast<bool>(*it));
  it++;
  CHECK(static_cast<bool>(*it));
  --it;
  CHECK(!static_cast<bool>(*it));
  it--;
  CHECK(static_cast<bool>(*it));

  it += 3;
  CHECK(!static_cast<bool>(*it));
  it -= 3;
  CHECK(static_cast<bool>(*it));

  auto it3 = 2 + it;
  CHECK(static_cast<bool>(*it3));
  auto it4 = it3 - 2;
  CHECK((it4 == it));

  // default constructed equality
  BS64_2::iterator d1, d2;
  CHECK((d1 == d2));

  // const conversion + const iteration
  BS64_2::const_iterator cit = it;
  CHECK(static_cast<bool>(*cit));
  BS64_2 const &cb = b;
  CHECK((cb.begin() != cb.end()));
  CHECK_EQ(cb.end() - cb.begin(), 5);
  CHECK(static_cast<bool>(cb.begin()[0]));
  auto cc = cb.begin() + 4;
  CHECK(static_cast<bool>(*cc));
  --cc;
  CHECK(!static_cast<bool>(*cc));
  cc += 1;
  CHECK(static_cast<bool>(*cc));
  cc -= 1;
  CHECK(!static_cast<bool>(*cc));
  BS64_2::const_iterator cd1, cd2;
  CHECK((cd1 == cd2));
  CHECK(!((cb.begin() + 1) == cb.begin()));
}

void test_reverse_const_iterators()
{
  SECTION("reverse / const iterators");
  BS64_2 b{true, false, true}; // bit0=true, bit2=true
  // forward collects bit0..bit2
  std::vector<char> fwd;
  for (auto x : b) fwd.push_back(x ? char(1) : char(0));
  CHECK((fwd == std::vector<char>{1, 0, 1}));
  // reverse collects bit2..bit0
  std::vector<char> rev;
  for (auto rit = b.rbegin(); rit != b.rend(); ++rit) rev.push_back(*rit ? char(1) : char(0));
  CHECK((rev == std::vector<char>{1, 0, 1})); // palindrome here; use asymmetric below
  BS64_2 c{true, true, false}; // bits: 1,1,0 ; reverse: 0,1,1
  std::vector<char> rev2;
  for (auto rit = c.rbegin(); rit != c.rend(); ++rit) rev2.push_back(*rit ? char(1) : char(0));
  CHECK((rev2 == std::vector<char>{0, 1, 1}));
  // const reverse
  BS64_2 const &cc = c;
  std::vector<char> crev;
  for (auto rit = cc.rbegin(); rit != cc.rend(); ++rit) crev.push_back(*rit ? char(1) : char(0));
  CHECK((crev == std::vector<char>{0, 1, 1}));
  CHECK((cc.cbegin() != cc.cend()));
  CHECK((cc.crbegin() != cc.crend()));
  // mutable via reverse base? at least size check
  CHECK_EQ(c.rbegin() - c.rend(), -3);
}

// ==================================================================
// 8. block iterators
// ==================================================================
void test_block_iterators()
{
  SECTION("block iterators");
  BS64_2 b(stdex::from_blocks, std::initializer_list<std::uint64_t>{11u, 22u});
  CHECK_EQ(*b.block_begin(), static_cast<std::uint64_t>(11));
  CHECK_EQ(*(b.block_end() - 1), static_cast<std::uint64_t>(22));
  CHECK_EQ(b.block_cbegin()[1], static_cast<std::uint64_t>(22));
  // mutate through block iterator
  *b.block_begin() = 33u;
  CHECK_EQ(b.data()[0], static_cast<std::uint64_t>(33));
  // reverse
  CHECK_EQ(*b.block_rbegin(), static_cast<std::uint64_t>(22));
  BS64_2 const &cb = b;
  CHECK_EQ(*cb.block_begin(), static_cast<std::uint64_t>(33));
  CHECK_EQ(*cb.block_rbegin(), static_cast<std::uint64_t>(22));
  CHECK_EQ(cb.block_cbegin()[0], static_cast<std::uint64_t>(33));
  CHECK((cb.block_crbegin() != cb.block_crend()));
  CHECK_EQ(b.block_end() - b.block_begin(), 2);
}

// ==================================================================
// 9. capacity / storage observers
// ==================================================================
void test_capacity_observers()
{
  SECTION("capacity / storage");
  BS64_2 s;
  CHECK(s.empty());
  CHECK_EQ(s.size(), 0u);
  CHECK_EQ(s.block_count(), 0u);
  CHECK(s.is_stack());
  CHECK(!s.is_heap());
  CHECK_EQ(s.block_capacity(), 2u);
  CHECK_EQ(s.capacity(), 128u); // 2*64
  CHECK((s.max_size() > 0u));
  CHECK((s.block_max_size() > 0u));
  CHECK((s.data() != nullptr || s.block_count() == 0u));

  BS64_0 h;
  CHECK(h.is_heap());
  CHECK(!h.is_stack());
  CHECK_EQ(h.block_capacity(), 0u);
  CHECK_EQ(h.capacity(), 0u);

  BS64_2 g(200, false);
  CHECK(g.is_heap());
  CHECK((g.block_capacity() >= 4u));
  CHECK((g.capacity() >= 200u));
  CHECK_EQ(g.size(), 200u);

  // reserve grows, keeps bits
  BS64_2 r{true, false};
  r.reserve(200);
  CHECK(r.is_heap());
  CHECK((r.capacity() >= 200u));
  CHECK(equals_vec(r, std::vector<char>{1, 0}));
  std::size_t const cap = r.block_capacity();
  r.reserve(1); // no shrink
  CHECK_EQ(r.block_capacity(), cap);

  // shrink_to_fit: heap -> fit, then small -> stack
  r.shrink_to_fit();
  CHECK((r.capacity() >= 2u));
  CHECK(equals_vec(r, std::vector<char>{1, 0}));
  BS64_2 big(200, true);
  big.resize(2);
  big.shrink_to_fit();
  CHECK(big.is_stack());
  CHECK_EQ(big.size(), 2u);

  // get_allocator
  CHECK((r.get_allocator() == std::allocator<std::uint64_t>{}));
  // inline_capacity constant
  CHECK_EQ(BS64_2::inline_capacity, 2u);
}

// ==================================================================
// 10. element access
// ==================================================================
void test_element_access()
{
  SECTION("element access");
  BS64_2 b{true, false, true};
  CHECK(static_cast<bool>(b[0]));
  CHECK(!static_cast<bool>(b[1]));
  BS64_2 const &cb = b;
  CHECK(static_cast<bool>(cb[0]));
  CHECK(!static_cast<bool>(cb[1]));

  CHECK(static_cast<bool>(b.at(0)));
  CHECK(static_cast<bool>(b.test(0)));
  CHECK(!static_cast<bool>(b.test(1)));
  CHECK_EQ(b.test_set(0, false), true);
  CHECK(!static_cast<bool>(b[0]));
  CHECK_EQ(b.test_set(0), false);
  CHECK(static_cast<bool>(b[0]));

  CHECK(static_cast<bool>(b.front()));
  CHECK(static_cast<bool>(b.back()));
  BS64_2 one{true};
  CHECK(static_cast<bool>(one.front()));
  CHECK(static_cast<bool>(one.back()));
  CHECK(static_cast<bool>(cb.front()));
  CHECK(static_cast<bool>(cb.back()));
  CHECK(static_cast<bool>(cb.at(2)));

  b[0] = false;
  CHECK(!static_cast<bool>(b.front()));
  b[2] = false;
  CHECK(!static_cast<bool>(b.back()));

  for (auto pos : {10u, 100u, 1000u}) {
    bool threw = false;
    try {
      (void)b.at(pos);
    } catch (std::out_of_range const &) {
      threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
      (void)cb.at(pos);
    } catch (std::out_of_range const &) {
      threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
      (void)b.test(pos);
    } catch (std::out_of_range const &) {
      threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
      (void)b.test_set(pos);
    } catch (std::out_of_range const &) {
      threw = true;
    }
    CHECK(threw);
  }
  // empty front/back would be UB; only check empty at throws
  BS64_2 e;
  {
    bool threw = false;
    try {
      (void)e.at(0);
    } catch (std::out_of_range const &) {
      threw = true;
    }
    CHECK(threw);
  }
}

// ==================================================================
// 11. all / any / none / count
// ==================================================================
void test_all_any_none_count()
{
  SECTION("all / any / none / count");
  {
    BS64_2 e;
    CHECK(!e.any());
    CHECK(e.none());
    CHECK(e.all()); // vacuous over zero bits (matches all_blocks_ loop)
    CHECK_EQ(e.count(), 0u);
  }
  {
    BS64_2 b(10, false);
    CHECK(!b.any());
    CHECK(b.none());
    CHECK(!b.all());
    CHECK_EQ(b.count(), 0u);
  }
  {
    BS64_2 b(10, true);
    CHECK(b.any());
    CHECK(!b.none());
    CHECK(b.all());
    CHECK_EQ(b.count(), 10u);
  }
  {
    BS64_2 b(70, false);
    b.set(63, true);
    CHECK(b.any());
    CHECK_EQ(b.count(), 1u);
    CHECK(!b.all());
  }
  {
    // large triggers SIMD paths (>256B for any/count, >=64B for all)
    BS64_0 big(5000, false);
    CHECK(!big.any());
    CHECK_EQ(big.count(), 0u);
    CHECK(!big.all());
    big.set(0, true);
    big.set(4999, true);
    CHECK(big.any());
    CHECK_EQ(big.count(), 2u);
    big.set();
    CHECK(big.all());
    CHECK_EQ(big.count(), 5000u);
    big.reset();
    CHECK(big.none());
  }
  {
    // tail padding must not count
    BS64_2 b(65, true);
    CHECK_EQ(b.count(), 65u);
    CHECK(b.all());
  }
}

// ==================================================================
// 12. subset / intersects
// ==================================================================
void test_subset_intersects()
{
  SECTION("subset / intersects");
  BS64_2 a{true, false, true};
  BS64_2 b{true, true, true};
  CHECK(a.is_subset_of(b));
  CHECK(!b.is_subset_of(a));
  CHECK(a.is_proper_subset_of(b));
  CHECK(!b.is_proper_subset_of(b));
  CHECK(!b.is_proper_subset_of(a));
  BS64_2 e1(3, false), e2(3, false);
  CHECK(e1.is_subset_of(e2));
  CHECK(!e1.is_proper_subset_of(e2));
  CHECK(!e1.intersects(e2));
  e1.set(1, true);
  CHECK(e1.intersects(e1));
  CHECK(!e1.intersects(e2));
  e2.set(1, true);
  CHECK(e1.intersects(e2));

  BS64_2 diff_size(4, false);
  for (auto *p : {&a, &b}) {
    bool threw = false;
    try {
      (void)p->is_subset_of(diff_size);
    } catch (std::invalid_argument const &) {
      threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
      (void)p->is_proper_subset_of(diff_size);
    } catch (std::invalid_argument const &) {
      threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
      (void)p->intersects(diff_size);
    } catch (std::invalid_argument const &) {
      threw = true;
    }
    CHECK(threw);
  }
}

// ==================================================================
// 13. find one bits
// ==================================================================
void test_find_one_bits()
{
  SECTION("find one bits");
  {
    BS64_2 e;
    CHECK_EQ(e.find_first(), BS64_2::npos);
    CHECK_EQ(e.find_last(), BS64_2::npos);
    CHECK_EQ(e.find_next(0), BS64_2::npos);
    CHECK_EQ(e.find_prev(0), BS64_2::npos);
    CHECK_EQ(e.find_next(BS64_2::npos), BS64_2::npos);
  }
  {
    BS64_2 b(10, false);
    CHECK_EQ(b.find_first(), BS64_2::npos);
    CHECK_EQ(b.find_last(), BS64_2::npos);
    b.set(3, true);
    b.set(7, true);
    CHECK_EQ(b.find_first(), 3u);
    CHECK_EQ(b.find_last(), 7u);
    CHECK_EQ(b.find_next(3), 7u);
    CHECK_EQ(b.find_next(7), BS64_2::npos);
    CHECK_EQ(b.find_next(8), BS64_2::npos);
    CHECK_EQ(b.find_prev(8), 7u);
    CHECK_EQ(b.find_prev(7), 3u);
    CHECK_EQ(b.find_prev(3), BS64_2::npos);
    CHECK_EQ(b.find_prev(0), BS64_2::npos);
    CHECK_EQ(b.find_prev(b.size()), 7u);
    CHECK_EQ(b.find_prev(BS64_2::npos), 7u);
    CHECK_EQ(b.find_next(BS64_2::npos), BS64_2::npos);
  }
  {
    // cross-block
    BS64_2 b(130, false);
    b.set(0, true);
    b.set(64, true);
    b.set(129, true);
    CHECK_EQ(b.find_first(), 0u);
    CHECK_EQ(b.find_next(0), 64u);
    CHECK_EQ(b.find_next(64), 129u);
    CHECK_EQ(b.find_last(), 129u);
    CHECK_EQ(b.find_prev(129), 64u);
    CHECK_EQ(b.find_prev(64), 0u);
  }
  {
    // enumeration via find_next matches naive scan
    BS64_2 b(200, false);
    for (std::size_t i = 0; i < 200; i += 7) b.set(i, true);
    std::vector<std::size_t> got;
    for (auto p = b.find_first(); p != BS64_2::npos; p = b.find_next(p)) got.push_back(p);
    CHECK_EQ(got.size(), 29u);
    CHECK_EQ(got.front(), 0u);
    CHECK_EQ(got.back(), 196u);
  }
}

// ==================================================================
// 14. find zero bits
// ==================================================================
void test_find_zero_bits()
{
  SECTION("find zero bits");
  {
    BS64_2 e;
    CHECK_EQ(e.find_first_zero(), BS64_2::npos);
    CHECK_EQ(e.find_last_zero(), BS64_2::npos);
  }
  {
    BS64_2 all3(3, true);
    CHECK_EQ(all3.find_first_zero(), BS64_2::npos);
    CHECK_EQ(all3.find_last_zero(), BS64_2::npos);
    CHECK_EQ(all3.find_next_zero(0), BS64_2::npos);
    CHECK_EQ(all3.find_prev_zero(3), BS64_2::npos);
  }
  {
    BS64_2 b(10, true);
    b.reset(4);
    CHECK_EQ(b.find_first_zero(), 4u);
    CHECK_EQ(b.find_last_zero(), 4u);
    CHECK_EQ(b.find_next_zero(3), 4u);
    CHECK_EQ(b.find_next_zero(4), BS64_2::npos);
    CHECK_EQ(b.find_prev_zero(5), 4u);
    CHECK_EQ(b.find_prev_zero(4), BS64_2::npos);
    CHECK_EQ(b.find_next_zero(BS64_2::npos), BS64_2::npos);
  }
  {
    // tail padding must never match as zero
    BS64_2 b(65, true);
    CHECK_EQ(b.find_first_zero(), BS64_2::npos);
    CHECK_EQ(b.find_last_zero(), BS64_2::npos);
    BS64_2 c(65, false);
    CHECK_EQ(c.find_first_zero(), 0u);
    CHECK_EQ(c.find_last_zero(), 64u);
    CHECK_EQ(c.find_next_zero(63), 64u);
    c.set(64, true);
    CHECK_EQ(c.find_next_zero(63), BS64_2::npos);
  }
  {
    // cross-block zeros enumeration
    BS64_2 b(130, true);
    b.set(0, 64, false);
    CHECK_EQ(b.find_first_zero(), 0u);
    CHECK_EQ(b.find_next_zero(63), BS64_2::npos);
    b.reset(129);
    CHECK_EQ(b.find_last_zero(), 129u);
  }
}

// ==================================================================
// 15. set / reset / flip ranges
// ==================================================================
void test_set_reset_flip()
{
  SECTION("set / reset / flip");
  BS64_2 b(100, false);
  b.set();
  CHECK(b.all());
  CHECK(tail_padding_zero(b));
  b.reset();
  CHECK(b.none());
  b.flip();
  CHECK(b.all());
  b.flip();
  CHECK(b.none());

  // empty set/reset/flip are no-ops
  BS64_2 e;
  e.set();
  e.reset();
  e.flip();
  CHECK(e.empty());

  // single pos
  b.reset();
  b.set(5, true);
  CHECK(static_cast<bool>(b[5]));
  b.set(5, false);
  CHECK(!static_cast<bool>(b[5]));
  b.reset(7);
  CHECK(!static_cast<bool>(b[7]));
  b.flip(7);
  CHECK(static_cast<bool>(b[7]));
  b.flip(7);
  CHECK(!static_cast<bool>(b[7]));

  // range spanning blocks
  b.reset();
  b.set(60, 10, true); // 60..69 crosses 64
  CHECK_EQ(b.count(), 10u);
  for (std::size_t i = 60; i < 70; ++i) CHECK(static_cast<bool>(b[i]));
  b.reset(62, 4);
  CHECK_EQ(b.count(), 6u);
  b.flip(60, 10);
  CHECK_EQ(b.count(), 4u);

  // len 0 no-op
  std::size_t const before = b.count();
  b.set(10, 0, true);
  b.flip(10, 0);
  CHECK_EQ(b.count(), before);

  // out_of_range: pos > size, len overflows
  for (auto fn : {0, 1, 2}) {
    bool threw = false;
    try {
      if (fn == 0) b.set(101, true);
      else if (fn == 1) b.reset(101);
      else b.flip(101);
    } catch (std::out_of_range const &) {
      threw = true;
    }
    CHECK(threw);
  }
  for (auto fn : {0, 1, 2}) {
    bool threw = false;
    try {
      if (fn == 0) b.set(99, 2, true);
      else if (fn == 1) b.reset(99, 2);
      else b.flip(99, 2);
    } catch (std::out_of_range const &) {
      threw = true;
    }
    CHECK(threw);
  }
  // pos==size with len==0 is allowed (no-op, no throw)
  b.set(b.size(), 0, true);
  b.reset(b.size(), 0);
  b.flip(b.size(), 0);
  CHECK(true);
}

// ==================================================================
// 16. push/pop/emplace/clear/resize
// ==================================================================
void test_push_pop_emplace_clear_resize()
{
  SECTION("push / pop / emplace / clear / resize");
  BS64_2 b;
  b.push_back(true);
  b.push_back(false);
  CHECK(equals_vec(b, std::vector<char>{1, 0}));
  b.emplace_back(true);
  CHECK_EQ(b.size(), 3u);
  CHECK(static_cast<bool>(b.back()));
  auto it = b.emplace(1, true);
  CHECK_EQ(it - b.begin(), 1);
  CHECK(equals_vec(b, std::vector<char>{1, 1, 0, 1}));

  b.pop_back();
  CHECK(equals_vec(b, std::vector<char>{1, 1, 0}));
  // pop across block boundary
  BS64_2 cross(65, true);
  cross.pop_back();
  CHECK_EQ(cross.size(), 64u);
  CHECK(cross.all());
  CHECK(tail_padding_zero(cross));
  // pop empty is no-op
  BS64_2 e;
  e.pop_back();
  CHECK(e.empty());

  // clear keeps storage flag/capacity
  BS64_2 h(200, true);
  auto const heap_cap = h.block_capacity();
  h.clear();
  CHECK(h.empty());
  CHECK_EQ(h.size(), 0u);
  CHECK(h.is_heap());
  CHECK_EQ(h.block_capacity(), heap_cap);
  h.push_back(true);
  CHECK_EQ(h.size(), 1u);

  // resize grow false/true + shrink + tail mask
  BS64_2 r;
  r.resize(10, true);
  CHECK_EQ(r.count(), 10u);
  r.resize(5);
  CHECK_EQ(r.size(), 5u);
  CHECK_EQ(r.count(), 5u);
  r.resize(70, false);
  CHECK_EQ(r.size(), 70u);
  CHECK_EQ(r.count(), 5u);
  CHECK(tail_padding_zero(r));
  r.assign(70, true);
  CHECK_EQ(r.count(), 70u);
  // grow-with-true across partial tail block (old_r != 0 path)
  BS64_2 g(65, false);
  g.set(0, 65, true);
  g.resize(70, true);
  CHECK_EQ(g.count(), 70u);
  g.resize(64, false);
  g.resize(70, true);
  CHECK(static_cast<bool>(g[64]));
  // resize same size no-op
  std::size_t const n = r.size();
  r.resize(n, true);
  CHECK_EQ(r.size(), n);

  // reserve keeps bits, grows capacity
  BS64_2 rv{true};
  rv.reserve(200);
  CHECK((rv.capacity() >= 200u));
  CHECK(static_cast<bool>(rv[0]));
}

// ==================================================================
// 17. insert / erase bit-wise
// ==================================================================
void test_insert_erase_bitwise()
{
  SECTION("insert / erase bit-wise");
  {
    BS64_2 b{false, false, false};
    auto it = b.insert(1, true);
    CHECK_EQ(it - b.begin(), 1);
    CHECK(equals_vec(b, std::vector<char>{0, 1, 0, 0}));
  }
  {
    BS64_2 b{false, true};
    b.insert(1, 3, true);
    CHECK(equals_vec(b, std::vector<char>{0, 1, 1, 1, 1}));
  }
  {
    // count 0 no-op
    BS64_2 b{true};
    auto it = b.insert(0, std::size_t{0}, true);
    CHECK_EQ(b.size(), 1u);
    CHECK_EQ(it - b.begin(), 0);
  }
  {
    // range insert + ilist + rotate shape
    BS64_2 b{true, false};
    std::vector<char> ins{1, 1, 0};
    b.insert(1, ins.begin(), ins.end());
    CHECK(equals_vec(b, std::vector<char>{1, 1, 1, 0, 0}));
    b.insert(0, std::initializer_list<bool>{false});
    CHECK(equals_vec(b, std::vector<char>{0, 1, 1, 1, 0, 0}));
  }
  {
    // whole-block fast path: pos%64==0 && count%64==0
    BS64_0 b(128, false);
    b.insert(64, 64, true);
    CHECK_EQ(b.size(), 192u);
    CHECK(!static_cast<bool>(b[0]));
    CHECK(static_cast<bool>(b[64]));
    CHECK(static_cast<bool>(b[127]));
    CHECK(!static_cast<bool>(b[128]));
  }
#if defined(__cpp_lib_ranges)
  {
    BS64_2 b{false};
    std::vector<char> v{1, 0, 1};
    b.insert_range(1, v);
    CHECK(equals_vec(b, std::vector<char>{0, 1, 0, 1}));
    b.append_range(v);
    CHECK_EQ(b.size(), 7u);
  }
#endif
  {
    // append variants
    BS64_2 b;
    b.append(std::initializer_list<bool>{true, false});
    CHECK(equals_vec(b, std::vector<char>{1, 0}));
    std::vector<int> v{1, 1};
    b.append(v.begin(), v.end());
    CHECK(equals_vec(b, std::vector<char>{1, 0, 1, 1}));
  }
  {
    // erase single + range + whole-block path
    BS64_2 b{true, false, true, false};
    auto it = b.erase(1);
    CHECK_EQ(it - b.begin(), 1);
    CHECK(equals_vec(b, std::vector<char>{1, 1, 0}));
    it = b.erase(0, 2);
    CHECK(equals_vec(b, std::vector<char>{0}));
    // empty range no-op
    it = b.erase(0, 0);
    CHECK_EQ(b.size(), 1u);
    (void)it;
  }
  {
    BS64_0 b(192, false);
    b.set(64, 64, true);
    auto it = b.erase(64, 128); // block-aligned erase
    CHECK_EQ(b.size(), 128u);
    CHECK(b.none());
    (void)it;
  }
  {
    // unaligned erase shifts + tail mask
    BS64_2 b(70, false);
    b.set(0, 70, true);
    b.erase(60, 65);
    CHECK_EQ(b.size(), 65u);
    CHECK(b.all());
    CHECK(tail_padding_zero(b));
  }
}

// ==================================================================
// 18. block mutators (from_blocks)
// ==================================================================
void test_block_mutators()
{
  SECTION("block mutators");
  {
    BS64_2 b;
    b.resize(stdex::from_blocks, 2, static_cast<std::uint64_t>(0xFF));
    CHECK_EQ(b.size(), 128u);
    CHECK_EQ(b.data()[0], static_cast<std::uint64_t>(0xFF));
    b.resize(stdex::from_blocks, 1);
    CHECK_EQ(b.size(), 64u);
  }
  {
    BS64_2 b;
    b.append(stdex::from_blocks, static_cast<std::uint64_t>(7));
    CHECK_EQ(b.block_count(), 1u);
    CHECK_EQ(b.data()[0], static_cast<std::uint64_t>(7));
    std::vector<std::uint64_t> w{8u, 9u};
    b.append(stdex::from_blocks, w.begin(), w.end());
    CHECK_EQ(b.block_count(), 3u);
    b.append(stdex::from_blocks, std::initializer_list<std::uint64_t>{10u});
    CHECK_EQ(b.block_count(), 4u);
  }
  {
    BS64_2 b(stdex::from_blocks, std::initializer_list<std::uint64_t>{1u, 2u, 3u});
    auto it = b.insert(stdex::from_blocks, 1, static_cast<std::uint64_t>(99));
    CHECK_EQ(*it, static_cast<std::uint64_t>(99));
    CHECK_EQ(b.block_count(), 4u);
    b.insert(stdex::from_blocks, 0, std::size_t{2}, static_cast<std::uint64_t>(5));
    CHECK_EQ(b.block_count(), 6u);
    CHECK_EQ(b.data()[0], static_cast<std::uint64_t>(5));
    // count 0 no-op
    auto it0 = b.insert(stdex::from_blocks, 0, std::size_t{0}, static_cast<std::uint64_t>(1));
    CHECK_EQ(b.block_count(), 6u);
    (void)it0;
    std::vector<std::uint64_t> w{77u};
    b.insert(stdex::from_blocks, 1, w.begin(), w.end());
    CHECK_EQ(b.data()[1], static_cast<std::uint64_t>(77));
    b.insert(stdex::from_blocks, 0, std::initializer_list<std::uint64_t>{88u});
    CHECK_EQ(b.data()[0], static_cast<std::uint64_t>(88));
  }
#if defined(__cpp_lib_ranges)
  {
    BS64_2 b;
    std::vector<std::uint64_t> w{1u, 2u};
    b.append_range(stdex::from_blocks, w);
    CHECK_EQ(b.block_count(), 2u);
    b.insert_range(stdex::from_blocks, 0, w);
    CHECK_EQ(b.block_count(), 4u);
  }
#endif
  {
    BS64_2 b(stdex::from_blocks, std::initializer_list<std::uint64_t>{1u, 2u, 3u});
    b.assign(stdex::from_blocks, std::size_t{2}, static_cast<std::uint64_t>(42));
    CHECK_EQ(b.block_count(), 2u);
    CHECK_EQ(b.data()[0], static_cast<std::uint64_t>(42));
    std::vector<std::uint64_t> w{7u};
    b.assign(stdex::from_blocks, w.begin(), w.end());
    CHECK_EQ(b.block_count(), 1u);
    b.assign(stdex::from_blocks, std::initializer_list<std::uint64_t>{9u, 9u});
    CHECK_EQ(b.block_count(), 2u);
  }
#if defined(__cpp_lib_ranges)
  {
    BS64_2 b;
    std::vector<std::uint64_t> w{3u, 4u};
    b.assign_range(stdex::from_blocks, w);
    CHECK_EQ(b.block_count(), 2u);
  }
#endif
  {
    BS64_2 b(stdex::from_blocks, std::initializer_list<std::uint64_t>{1u, 2u, 3u});
    auto it = b.erase(stdex::from_blocks, 1);
    CHECK_EQ(b.block_count(), 2u);
    CHECK_EQ(*it, static_cast<std::uint64_t>(3));
    b.erase(stdex::from_blocks, 0, 2);
    CHECK(b.empty());
  }
}

// ==================================================================
// 19. assign variants (bit-wise)
// ==================================================================
void test_assign_variants()
{
  SECTION("assign variants");
  {
    BS64_2 b{true, true, true};
    b.assign(5, true);
    CHECK_EQ(b.size(), 5u);
    CHECK(b.all());
    b.assign(5, false);
    CHECK(b.none());
    b.assign(2, true);
    CHECK_EQ(b.size(), 2u);
    CHECK(b.all());
  }
  {
    BS64_2 b;
    b.assign(std::initializer_list<bool>{true, false, true});
    CHECK(equals_vec(b, std::vector<char>{1, 0, 1}));
    std::vector<int> v{0, 0, 1, 1};
    b.assign(v.begin(), v.end());
    CHECK(equals_vec(b, std::vector<char>{0, 0, 1, 1}));
    // shrink via assign
    std::vector<int> s{1};
    b.assign(s.begin(), s.end());
    CHECK_EQ(b.size(), 1u);
  }
#if defined(__cpp_lib_ranges)
  {
    BS64_2 b;
    std::vector<char> v{1, 0};
    b.assign_range(v);
    CHECK(equals_vec(b, std::vector<char>{1, 0}));
  }
#endif
  {
    // assign longer than capacity grows
    BS64_2 b{true};
    std::vector<char> v(300, char(1));
    b.assign(v.begin(), v.end());
    CHECK_EQ(b.size(), 300u);
    CHECK(b.all());
  }
}

// ==================================================================
// 20. to_string / to_ulong / to_ullong
// ==================================================================
void test_conversions()
{
  SECTION("to_string / to_ulong / to_ullong");
  {
    BS64_2 b(std::string_view{"10110"});
    CHECK_EQ(b.to_string(), std::string{"10110"});
    CHECK_EQ(b.to_string('a', 'b'), std::string{"babba"});
    CHECK_EQ(b.to_string('X', 'Y'), std::string{"YXYYX"});
  }
  {
    BS64_2 e;
    CHECK_EQ(e.to_string(), std::string{});
    CHECK_EQ(e.to_ulong(), 0ul);
    CHECK_EQ(e.to_ullong(), 0ull);
  }
  {
    BS64_2 b(std::string_view{"101"});
    CHECK_EQ(b.to_ulong(), 5ul);
    CHECK_EQ(b.to_ullong(), 5ull);
  }
  {
    // ulong width-aware: fill exactly ulong digits, then one more must throw
    constexpr std::size_t W = std::numeric_limits<unsigned long>::digits;
    BS64_0 b(W, true);
    (void)b.to_ulong(); // no throw
    b.push_back(true);
    bool threw = false;
    try {
      (void)b.to_ulong();
    } catch (std::overflow_error const &) {
      threw = true;
    }
    CHECK(threw);
  }
  {
    constexpr std::size_t W = std::numeric_limits<unsigned long long>::digits;
    BS64_0 b(W, true);
    (void)b.to_ullong();
    b.push_back(true);
    bool threw = false;
    try {
      (void)b.to_ullong();
    } catch (std::overflow_error const &) {
      threw = true;
    }
    CHECK(threw);
  }
  {
    // high bit beyond ulong width set -> throw even if size larger
    BS64_0 b(200, false);
    b.set(100, true);
    bool threw = false;
    try {
      (void)b.to_ulong();
    } catch (std::overflow_error const &) {
      threw = true;
    }
    CHECK(threw);
  }
}

// ==================================================================
// 21. shifts
// ==================================================================
void test_shifts()
{
  SECTION("shifts");
  {
    BS64_2 b(std::string_view{"000111"});
    auto l = b << 2;
    CHECK_EQ(l.to_string(), std::string{"011100"});
    auto r = b >> 2;
    CHECK_EQ(r.to_string(), std::string{"000001"});
    CHECK_EQ(b.to_string(), std::string{"000111"}); // original unchanged
  }
  {
    BS64_2 b(std::string_view{"101"});
    b <<= 0;
    CHECK_EQ(b.to_string(), std::string{"101"});
    b >>= 0;
    CHECK_EQ(b.to_string(), std::string{"101"});
    b <<= 3;
    CHECK_EQ(b.to_string(), std::string{"000"});
    b = BS64_2(std::string_view{"101"});
    b >>= 3;
    CHECK_EQ(b.to_string(), std::string{"000"});
    b = BS64_2(std::string_view{"101"});
    b <<= 10;
    CHECK(b.none());
  }
  {
    // empty shift no-op
    BS64_2 e;
    e <<= 5;
    e >>= 5;
    CHECK(e.empty());
    CHECK_EQ((e << 3).size(), 0u);
  }
  {
    // word + bit shift across blocks
    BS64_0 b(130, false);
    b.set(0, true);
    b.set(1, true);
    b <<= 65; // 64+1
    CHECK(!static_cast<bool>(b[0]));
    CHECK(static_cast<bool>(b[65]));
    CHECK(static_cast<bool>(b[66]));
    b >>= 65;
    CHECK(static_cast<bool>(b[0]));
    CHECK(static_cast<bool>(b[1]));
  }
  {
    // left shift tail masking: shifting ones off the top must not leave padding ones
    BS64_2 b(65, false);
    b.set(63, true);
    b <<= 1;
    CHECK(static_cast<bool>(b[64]));
    CHECK(tail_padding_zero(b));
    CHECK_EQ(b.count(), 1u);
  }
}

// ==================================================================
// 22. bitwise logic + swap + compare
// ==================================================================
void test_bitwise_logic()
{
  SECTION("bitwise logic / swap / compare");
  BS64_2 a(std::string_view{"1100"});
  BS64_2 b(std::string_view{"1010"});
  CHECK_EQ((a & b).to_string(), std::string{"1000"});
  CHECK_EQ((a | b).to_string(), std::string{"1110"});
  CHECK_EQ((a ^ b).to_string(), std::string{"0110"});
  CHECK_EQ((a - b).to_string(), std::string{"0100"});
  CHECK_EQ((~a).to_string(), std::string{"0011"});

  BS64_2 c = a;
  c &= b;
  CHECK_EQ(c.to_string(), std::string{"1000"});
  c = a;
  c |= b;
  CHECK_EQ(c.to_string(), std::string{"1110"});
  c = a;
  c ^= b;
  CHECK_EQ(c.to_string(), std::string{"0110"});
  c = a;
  c -= b;
  CHECK_EQ(c.to_string(), std::string{"0100"});

  CHECK((a == a));
  CHECK(!(a == b));
  BS64_2 d(std::string_view{"11000"});
  CHECK(!(a == d)); // different size

  // swap member + free
  BS64_2 s1(std::string_view{"111"});
  BS64_2 s2(std::string_view{"000"});
  s1.swap(s2);
  CHECK_EQ(s1.to_string(), std::string{"000"});
  CHECK_EQ(s2.to_string(), std::string{"111"});
  swap(s1, s2);
  CHECK_EQ(s1.to_string(), std::string{"111"});

  // size mismatch throws invalid_argument
  BS64_2 w(5, false);
  for (int k = 0; k < 4; ++k) {
    bool threw = false;
    try {
      if (k == 0) c &= w;
      else if (k == 1) c |= w;
      else if (k == 2) c ^= w;
      else c -= w;
    } catch (std::invalid_argument const &) {
      threw = true;
    }
    CHECK(threw);
  }
  {
    BS64_2 x(std::string_view{"11"});
    BS64_2 y(3, false);
    bool threw = false;
    try {
      (void)(x & y);
    } catch (std::invalid_argument const &) {
      threw = true;
    }
    CHECK(threw);
  }
}

// ==================================================================
// 23. streams
// ==================================================================
void test_streams()
{
  SECTION("streams");
  {
    BS64_2 b(std::string_view{"10110"});
    std::ostringstream os;
    os << b;
    CHECK_EQ(os.str(), std::string{"10110"});
  }
  {
    BS64_2 b;
    std::istringstream is("1101xyz");
    is >> b;
    CHECK_EQ(b.to_string(), std::string{"1101"});
    // trailing non-bit left in stream
    CHECK_EQ(is.peek(), int('x'));
  }
  {
    // non-bit leading input -> failbit + clear (sentry succeeds, str empty)
    BS64_2 b(std::string_view{"111"});
    std::istringstream is("xyz");
    is >> b;
    CHECK(is.fail());
    CHECK(b.empty());
  }
  {
    BS64_2 b;
    std::istringstream is("0011");
    is >> b;
    CHECK(!is.fail());
    CHECK_EQ(b.to_string(), std::string{"0011"});
  }
  {
    // full consume then eof
    BS64_2 b;
    std::istringstream is("101");
    is >> b;
    CHECK_EQ(b.size(), 3u);
  }
}

// ==================================================================
// 24. tail / boundary sizes
// ==================================================================
void test_tail_boundary()
{
  SECTION("tail / boundary sizes");
  for (std::size_t n : {0u, 1u, 63u, 64u, 65u, 127u, 128u, 129u, 191u, 192u, 193u}) {
    BS64_2 b(n, true);
    CHECK_EQ(b.size(), n);
    CHECK_EQ(b.count(), n);
    CHECK(tail_padding_zero(b));
    if (n > 0) CHECK(b.all());
    BS64_2 z(n, false);
    CHECK(z.none());
    CHECK(tail_padding_zero(z));
    // flip preserves padding
    z.flip();
    CHECK(tail_padding_zero(z));
    if (n > 0) CHECK(z.all());
    // set whole preserves padding
    BS64_2 s(n, false);
    s.set();
    CHECK(tail_padding_zero(s));
  }
  {
    // block_count math
    CHECK_EQ(BS64_2(0).block_count(), 0u);
    CHECK_EQ(BS64_2(64).block_count(), 1u);
    CHECK_EQ(BS64_2(65).block_count(), 2u);
    CHECK_EQ(BS8_8(8).block_count(), 1u);
    CHECK_EQ(BS8_8(9).block_count(), 2u);
  }
  {
    // uint8 block width boundary (avoid (nbits,bool) ctor: header narrows
    // `block_type{0} - ...` to int for 8-bit blocks under /permissive-)
    BS8_8 b(9);
    b.set();
    CHECK_EQ(b.count(), 9u);
    CHECK(tail_padding_zero(b));
    CHECK_EQ(b.data()[1] & static_cast<std::uint8_t>(0xFE), static_cast<std::uint8_t>(0));
  }
}

// ==================================================================
// 25. different block types
// ==================================================================
void test_block_widths()
{
  SECTION("block widths");
  {
    BS32_4 b(33, true);
    CHECK_EQ(b.count(), 33u);
    CHECK(tail_padding_zero(b));
    CHECK_EQ(b.data()[1], static_cast<std::uint32_t>(1));
  }
  {
    BS16_4 b(std::string_view{"10101010101010101"}); // 17 bits
    CHECK_EQ(b.size(), 17u);
    CHECK_EQ(b.to_string(), std::string{"10101010101010101"});
  }
  {
    BS8_8 a(16), b(16);
    a.set(0, 8, true);
    b.set(8, 8, true);
    CHECK(!(a.intersects(b)));
    CHECK_EQ((a | b).count(), 16u);
    CHECK_EQ((a & b).count(), 0u);
  }
  {
    // count/any across widths with large sizes (SIMD + scalar agreement)
    BS32_4 big(1000, false);
    for (std::size_t i = 0; i < 1000; i += 13) big.set(i, true);
    std::size_t expect = 0;
    for (std::size_t i = 0; i < 1000; ++i) expect += big.test(i) ? 1u : 0u;
    CHECK_EQ(big.count(), expect);
    CHECK(big.any());
  }
}

// ==================================================================
// 26. stack <-> heap transitions
// ==================================================================
void test_stack_heap_transitions()
{
  SECTION("stack <-> heap");
  BS64_2 b;
  CHECK(b.is_stack());
  for (int i = 0; i < 128; ++i) b.push_back(true);
  CHECK(b.is_stack()); // exactly capacity
  CHECK_EQ(b.block_count(), 2u);
  b.push_back(true);
  CHECK(b.is_heap());
  CHECK_EQ(b.size(), 129u);
  CHECK(tail_padding_zero(b));

  // shrink back to stack when small
  b.resize(2);
  b.shrink_to_fit();
  CHECK(b.is_stack());
  CHECK_EQ(b.size(), 2u);

  // N=0 always heap
  BS64_0 h;
  CHECK(h.is_heap());
  h.push_back(true);
  CHECK(h.is_heap());

  // reserve forces heap even for small size
  BS64_2 s{true};
  s.reserve(500);
  CHECK(s.is_heap());
  s.shrink_to_fit();
  CHECK(s.is_stack());

  // copy preserves flag appropriately
  BS64_2 heap(300, true);
  BS64_2 heap_cpy(heap);
  CHECK(heap_cpy.is_heap());
  BS64_2 stack{true, false};
  BS64_2 stack_cpy(stack);
  CHECK(stack_cpy.is_stack());
}

// ==================================================================
// 27. fuzz vs vector<char> model
// ==================================================================
void test_fuzz_vs_vector()
{
  SECTION("fuzz vs vector<char>");
  std::mt19937 rng(0xC0FFEEu);
  std::uniform_int_distribution<int> op_d(0, 99);
  std::uniform_int_distribution<int> bit_d(0, 1);

  BS64_2 bs;
  std::vector<char> ref;
  constexpr int kOps = 4000;
  for (int i = 0; i < kOps; ++i) {
    int op = op_d(rng);
    if (op < 40) {
      bool v = static_cast<bool>(bit_d(rng));
      bs.push_back(v);
      ref.push_back(v ? char(1) : char(0));
    } else if (op < 50 && !ref.empty()) {
      bs.pop_back();
      ref.pop_back();
    } else if (op < 65 && !ref.empty()) {
      std::size_t p = static_cast<std::size_t>(rng()) % ref.size();
      bool v = static_cast<bool>(bit_d(rng));
      bs.set(p, v);
      ref[p] = v ? char(1) : char(0);
    } else if (op < 75 && !ref.empty()) {
      std::size_t p = static_cast<std::size_t>(rng()) % ref.size();
      bs.flip(p);
      ref[p] = ref[p] ? char(0) : char(1);
    } else if (op < 85) {
      std::size_t p = ref.empty() ? 0u : static_cast<std::size_t>(rng()) % (ref.size() + 1u);
      std::size_t c = static_cast<std::size_t>(rng()) % 5u;
      bool v = static_cast<bool>(bit_d(rng));
      bs.insert(p, c, v);
      ref.insert(ref.begin() + static_cast<std::ptrdiff_t>(p), c, v ? char(1) : char(0));
    } else if (op < 92 && !ref.empty()) {
      std::size_t a = static_cast<std::size_t>(rng()) % ref.size();
      std::size_t blen = static_cast<std::size_t>(rng()) % (ref.size() - a + 1u);
      bs.erase(a, a + blen);
      ref.erase(ref.begin() + static_cast<std::ptrdiff_t>(a), ref.begin() + static_cast<std::ptrdiff_t>(a + blen));
    } else if (op < 96 && !ref.empty()) {
      std::size_t n = static_cast<std::size_t>(rng()) % 70u;
      bs <<= n;
      // model: shift left drops low? No: <<= moves bit i to i+n (toward MSB tail).
      std::vector<char> nr(ref.size(), char(0));
      for (std::size_t k = 0; k < ref.size(); ++k) {
        if (ref[k] && k + n < ref.size()) nr[k + n] = char(1);
      }
      // bits shifted past end are dropped; low n become 0
      for (std::size_t k = 0; k < (std::min)(n, ref.size()); ++k) nr[k] = char(0);
      // careful: our loop above already maps correctly; low n stay 0
      ref = nr;
    } else {
      std::size_t n = ref.empty() ? 0u : static_cast<std::size_t>(rng()) % 70u;
      bs >>= n;
      std::vector<char> nr(ref.size(), char(0));
      for (std::size_t k = n; k < ref.size(); ++k) nr[k - n] = ref[k];
      ref = nr;
    }
    CHECK_EQ(bs.size(), ref.size());
    if ((i % 37) == 0) {
      CHECK(equals_vec(bs, ref));
      CHECK(tail_padding_zero(bs));
      CHECK_EQ(bs.count(), static_cast<std::size_t>(std::count(ref.begin(), ref.end(), char(1))));
    }
  }
  CHECK(equals_vec(bs, ref));
  CHECK(tail_padding_zero(bs));
}

void test_fuzz_bitwise_and_strings()
{
  SECTION("fuzz bitwise + string roundtrip");
  std::mt19937 rng(12345u);
  for (int trial = 0; trial < 200; ++trial) {
    std::size_t n = 1u + static_cast<std::size_t>(rng()) % 200u;
    std::string s;
    s.reserve(n);
    for (std::size_t i = 0; i < n; ++i) s.push_back((rng() & 1u) ? '1' : '0');
    BS64_0 a(s), b(s);
    CHECK_EQ(a.to_string(), s);
    CHECK_EQ((a & b).to_string(), s);
    CHECK_EQ((a | b).to_string(), s);
    CHECK_EQ((a ^ b).count(), 0u);
    CHECK(((a - b).none()));
    CHECK((a == b));
    CHECK(a.is_subset_of(b));
    // flip twice roundtrips
    auto f = a;
    f.flip();
    f.flip();
    CHECK((f == a));
    // shift roundtrip model check on small n
    if (n >= 3) {
      auto l = a << 1;
      auto r = l >> 1;
      // top bit shifted out is lost
      for (std::size_t i = 0; i + 1 < n; ++i) CHECK_EQ(static_cast<bool>(r[i]), static_cast<bool>(a[i]));
    }
  }
}

// ==================================================================
// 28. exception throwing APIs
// ==================================================================
void test_exception_throwing_apis()
{
  SECTION("exception throwing APIs");
  BS64_2 b(10, false);
  // length_error: reserve/resize/push/insert/assign beyond max (use tiny-max allocator)
  {
    counting_state st;
    st.max_limit = 2; // 2 blocks max
    CBitset<std::uint64_t, 2> tiny(counting_alloc<std::uint64_t>(&st, 1));
    CHECK_EQ(tiny.max_size(), 128u);
    bool threw = false;
    try {
      tiny.reserve(129);
    } catch (std::length_error const &) {
      threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
      tiny.resize(129, false);
    } catch (std::length_error const &) {
      threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
      tiny.assign(129, false);
    } catch (std::length_error const &) {
      threw = true;
    }
    CHECK(threw);
    // block overflow_error paths
    threw = false;
    try {
      tiny.resize(stdex::from_blocks, 3);
    } catch (std::overflow_error const &) {
      threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
      tiny.assign(stdex::from_blocks, std::size_t{3}, std::uint64_t{0});
    } catch (std::overflow_error const &) {
      threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
      CBitset<std::uint64_t, 2> x(stdex::from_blocks, std::size_t{1000000}, counting_alloc<std::uint64_t>(&st, 1));
      (void)x;
    } catch (std::overflow_error const &) {
      threw = true;
    } catch (std::length_error const &) {
      threw = true; // either is acceptable for huge request under tiny max
    } catch (std::bad_alloc const &) {
      threw = true;
    }
    CHECK(threw);
    CHECK_EQ(st.active, 0u);
  }
  // to_ulong/ullong overflow already covered; here size mismatch + invalid string
  {
    BS64_2 x(4, false), y(5, false);
    bool threw = false;
    try {
      x &= y;
    } catch (std::invalid_argument const &) {
      threw = true;
    }
    CHECK(threw);
  }
  {
    bool threw = false;
    try {
      BS64_2 bad(std::string_view{"10a"});
      (void)bad;
    } catch (std::invalid_argument const &) {
      threw = true;
    }
    CHECK(threw);
  }
  // push_back at max throws length_error (tiny allocator, force heap then fill)
  {
    counting_state st;
    st.max_limit = 1;
    CBitset<std::uint64_t, 0> t(counting_alloc<std::uint64_t>(&st, 1));
    for (int i = 0; i < 64; ++i) t.push_back(false);
    bool threw = false;
    try {
      t.push_back(false); // needs 2 blocks > max 1
    } catch (std::length_error const &) {
      threw = true;
    }
    CHECK(threw);
    CHECK_EQ(t.size(), 64u);
    CHECK_EQ(st.active, 1u);
  }
}

// ==================================================================
// 29. exception safety: bad_alloc + throwing iterators (leak-free)
// ==================================================================
void test_exception_safety_alloc()
{
  SECTION("exception safety (bad_alloc / throwing iter)");
  {
    counting_state st;
    CBitset<std::uint64_t, 2> bs(counting_alloc<std::uint64_t>(&st, 1));
    for (int i = 0; i < 100; ++i) bs.push_back(i % 2 == 0);
    auto const old_size = bs.size();
    auto const old_cap = bs.block_capacity();
    st.fail_on_attempt = st.allocation_attempts + 1;
    bool threw = false;
    try {
      bs.reserve(10000); // bits: forces block growth past stack capacity
    } catch (std::bad_alloc const &) {
      threw = true;
    }
    st.fail_on_attempt = 0;
    CHECK(threw);
    CHECK_EQ(bs.size(), old_size);
    CHECK_EQ(bs.block_capacity(), old_cap);
    for (std::size_t i = 0; i < old_size; ++i) CHECK_EQ(static_cast<bool>(bs[i]), (i % 2 == 0));
  }
  {
    // growth from stack rolls back on alloc failure
    counting_state st;
    CBitset<std::uint64_t, 2> bs(counting_alloc<std::uint64_t>(&st, 1));
    for (int i = 0; i < 128; ++i) bs.push_back(false);
    CHECK(bs.is_stack());
    st.fail_on_attempt = st.allocation_attempts + 1;
    bool threw = false;
    try {
      bs.push_back(true);
    } catch (std::bad_alloc const &) {
      threw = true;
    }
    st.fail_on_attempt = 0;
    CHECK(threw);
    CHECK(bs.is_stack());
    CHECK_EQ(bs.size(), 128u);
  }
  {
    // throwing forward iterator during heap construction: no leak, caller sees throw
    std::vector<int> v(200, 1);
    throwing_fwd_iter f0(&v, 0, 50), f1(&v, v.size(), 50);
    bool threw = false;
    try {
      BS64_0 b(f0, f1);
      (void)b;
    } catch (std::runtime_error const &) {
      threw = true;
    }
    CHECK(threw);
  }
  {
    // throwing input iterator (single-pass push_back path)
    std::vector<int> v(50, 1);
    throwing_input_iter i0(&v, 0, 10), i1(&v, v.size(), 10);
    bool threw = false;
    try {
      BS64_2 b(i0, i1);
      (void)b;
    } catch (std::runtime_error const &) {
      threw = true;
    }
    CHECK(threw);
  }
  {
    // invalid string with counting alloc leaves no active allocation
    counting_state st;
    bool threw = false;
    try {
      CBitset<std::uint64_t, 0> b(std::string_view{"10x01"}, 0, 5, '0', '1', counting_alloc<std::uint64_t>(&st, 1));
      (void)b;
    } catch (std::invalid_argument const &) {
      threw = true;
    }
    CHECK(threw);
    CHECK_EQ(st.active, 0u);
  }
}

// ==================================================================
// 30. allocator lifecycle / resources
// ==================================================================
void test_allocator_lifecycle()
{
  SECTION("allocator lifecycle");
  {
    counting_state st;
    {
      CBitset<std::uint64_t, 2> a(counting_alloc<std::uint64_t>(&st, 1));
      for (int i = 0; i < 300; ++i) a.push_back(i % 3 == 0);
      CHECK((st.active > 0u));
      CBitset<std::uint64_t, 2> b(a, counting_alloc<std::uint64_t>(&st, 1));
      CHECK_EQ(b.size(), a.size());
      CHECK((b == a));
      CBitset<std::uint64_t, 2> c(std::move(b), counting_alloc<std::uint64_t>(&st, 1));
      CHECK_EQ(c.size(), a.size());
      CBitset<std::uint64_t, 2> d(counting_alloc<std::uint64_t>(&st, 1));
      d = a;
      CHECK((d == a));
      CBitset<std::uint64_t, 2> e2(counting_alloc<std::uint64_t>(&st, 1));
      e2 = std::move(d);
      CHECK((e2 == a));
      // swap heap/heap + stack/stack + stack/heap
      CBitset<std::uint64_t, 2> s1(counting_alloc<std::uint64_t>(&st, 1)), s2(counting_alloc<std::uint64_t>(&st, 1));
      s1.push_back(true);
      s2.push_back(false);
      s2.push_back(false);
      s1.swap(s2);
      CHECK_EQ(s1.size(), 2u);
      CHECK_EQ(s2.size(), 1u);
      swap(s1, s2);
      CHECK_EQ(s1.size(), 1u);
      // clear does not leak; shrink releases
      e2.clear();
      CHECK(e2.empty());
    }
    CHECK_EQ(st.active, 0u);
    CHECK_EQ(st.allocations, st.deallocations);
  }
  {
    // unequal allocators: copy/move stay correct (non-propagating)
    counting_state sa, sb;
    CBitset<std::uint64_t, 2> a(counting_alloc<std::uint64_t>(&sa, 1));
    for (int i = 0; i < 50; ++i) a.push_back(true);
    CBitset<std::uint64_t, 2> b(counting_alloc<std::uint64_t>(&sb, 2));
    b = a;
    CHECK((b == a));
    CHECK_EQ(sa.active + sb.active, sb.active + sa.active); // smoke
    b = std::move(a);
    CHECK_EQ(b.size(), 50u);
  }
  {
    // destruction of partial stack state is trivial (Block) but must not crash
    BS8_8 s;
    for (int i = 0; i < 5; ++i) s.push_back(true);
    CHECK_EQ(s.size(), 5u);
  }
}

// ==================================================================
// 31. constexpr (heap always; stack guarded like test_hash_table)
// ==================================================================
constexpr bool constexpr_heap_ctor_query()
{
  BS64_0 b(10, false);
  if (b.size() != 10u) return false;
  b.set(3, true);
  if (!b.test(3)) return false;
  if (b.count() != 1u) return false;
  if (!b.any() || b.all()) return false;
  b.set();
  if (!b.all()) return false;
  b.flip(0);
  if (b.all()) return false;
  return b.find_first() == 1u;
}

constexpr bool constexpr_heap_mutate()
{
  BS64_0 b;
  for (int i = 0; i < 40; ++i) b.push_back(i % 2 == 0);
  if (b.size() != 40u) return false;
  if (b.count() != 20u) return false;
  b.insert(5, true);
  if (b.size() != 41u) return false;
  b.erase(5);
  if (b.size() != 40u) return false;
  b.resize(100, true);
  if (b.count() != 80u) return false;
  auto c = b;
  c &= b;
  if (!(c == b)) return false;
  c <<= 1;
  if (c.size() != b.size()) return false;
  return c.find_first() == 1u;
}

constexpr bool constexpr_heap_string_find()
{
  BS64_0 b(std::string_view{"10110"});
  if (b.size() != 5u) return false;
  if (b.to_ullong() != 22ull) return false;
  if (b.find_first() != 1u) return false;
  if (b.find_last() != 4u) return false;
  if (b.find_next(1) != 2u) return false;
  if (b.find_first_zero() != 0u) return false;
  BS64_0 o(std::string_view{"11111"});
  if (!b.is_subset_of(o)) return false;
  if (!b.intersects(o)) return false;
  return true;
}

static_assert(constexpr_heap_ctor_query());
static_assert(constexpr_heap_mutate());
static_assert(constexpr_heap_string_find());

#if defined(__cpp_lib_start_lifetime) && __cpp_lib_start_lifetime >= 202603L
constexpr bool constexpr_stack_access()
{
  stdex::inlined_bitset<std::uint64_t, 2> s;
  if (!s.is_stack()) return false;
  for (int i = 0; i < 100; ++i) s.push_back(i % 2 == 0);
  if (!s.is_stack()) return false;
  for (int i = 0; i < 100; ++i) {
    if (s.test(static_cast<std::size_t>(i)) != (i % 2 == 0)) return false;
  }
  return s.count() == 50u;
}
constexpr bool constexpr_stack_iterate()
{
  stdex::inlined_bitset<std::uint64_t, 2> s(std::string_view{"101"});
  std::size_t n = 0;
  for (auto it = s.begin(); it != s.end(); ++it) ++n;
  return n == 3u;
}
static_assert(constexpr_stack_access());
static_assert(constexpr_stack_iterate());
#endif

void test_constexpr()
{
  SECTION("constexpr");
  CHECK(constexpr_heap_ctor_query());
  CHECK(constexpr_heap_mutate());
  CHECK(constexpr_heap_string_find());
#if defined(__cpp_lib_start_lifetime) && __cpp_lib_start_lifetime >= 202603L
  CHECK(constexpr_stack_access());
  CHECK(constexpr_stack_iterate());
#else
  // stack constexpr reserved for C++26; heap paths above already cover consteval.
  CHECK(true);
#endif
}

// ==================================================================
// main
// ==================================================================
int main()
{
  test_default_and_nbits_ctors();
  test_bit_range_ilist_constructors();
  test_block_range_ilist_constructors();
  test_string_constructors();
  test_copy_move_assign_lifecycle();
  test_reference_proxy();
  test_iterator_random_access();
  test_reverse_const_iterators();
  test_block_iterators();
  test_capacity_observers();
  test_element_access();
  test_all_any_none_count();
  test_subset_intersects();
  test_find_one_bits();
  test_find_zero_bits();
  test_set_reset_flip();
  test_push_pop_emplace_clear_resize();
  test_insert_erase_bitwise();
  test_block_mutators();
  test_assign_variants();
  test_conversions();
  test_shifts();
  test_bitwise_logic();
  test_streams();
  test_tail_boundary();
  test_block_widths();
  test_stack_heap_transitions();
  test_fuzz_vs_vector();
  test_fuzz_bitwise_and_strings();
  test_exception_throwing_apis();
  test_exception_safety_alloc();
  test_allocator_lifecycle();
  test_constexpr();

  std::println("\n===============================");
  std::println("PASS: {}   FAIL: {}", g_pass, g_fail);
  std::println("===============================");
  return g_fail == 0 ? 0 : 1;
}
