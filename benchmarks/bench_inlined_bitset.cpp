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

// bench_inlined_bitset.cpp
/// \file Bitset comparison: stdex::inlined_bitset vs std::bitset vs sul::dynamic_bitset.
///
/// Grouping honors storage: stack-vs-stack and heap-vs-heap only.
///   Stack (256 bits, all stack-resident, no allocation):
///     StdexStack = inlined_bitset<uint64_t, 4>  (4*64 = 256 bits inline)
///     StdStack   = std::bitset<256>              (fixed, stack)
///   Heap (32768 bits ~ 4KB, all heap-resident):
///     StdexHeap  = inlined_bitset<uint64_t, 0>   (inline_capacity 0, always heap)
///     DynHeap    = sul::dynamic_bitset<unsigned long long> (vector-backed heap,
///                only when STDEX_BENCH_HAS_DYNAMIC_BITSET is set by
///                -DSTDEX_BENCH_DYNAMIC_BITSET=ON, which fetches the header
///                from GitHub into the build dir; otherwise heap benchmarks
///                are stdex-only and stack compares against std::bitset.)
///
/// std::bitset is excluded from heap group (it would still be stack storage,
/// breaking heap-vs-heap); sul::dynamic_bitset is excluded from stack group
/// (always heap). Methods without a counterpart on one side are still
/// benchmarked for stdex alone and marked (stdex-only) so "all public methods"
/// are covered: iterators, block API, insert/erase, find_zero/last/prev,
/// subset/intersects notes below.
///
/// Fairness: identical bit patterns (fixed seeds), same sizes, std::allocator
/// throughout, block width 64 on both heap sides. Only the timed op runs
/// inside the loop; fill/teardown stays in PauseTiming. Hashing N/A.
///
/// Run (Release): bench_inlined_bitset.exe [--benchmark_filter='Stack/.*']
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include <benchmark/benchmark.h>

#include "inlined_bitset.hpp"
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
#include <sul/dynamic_bitset.hpp>
#endif

namespace
{
constexpr std::size_t kStackBits = 256;
constexpr std::size_t kHeapBits = 32768;

using StdexStack = stdex::inlined_bitset<std::uint64_t, 4>;
using StdStack = std::bitset<kStackBits>;
using StdexHeap = stdex::inlined_bitset<std::uint64_t, 0>;
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
using DynHeap = sul::dynamic_bitset<unsigned long long>;
#endif

static_assert(StdexStack::bits_per_block == 64u);
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
static_assert(DynHeap::bits_per_block == 64u);
#endif

// --- shared patterns (fixed seeds) -------------------------------------------
std::string pattern_string(std::size_t n, unsigned seed)
{
  std::mt19937 rng(seed);
  std::string s;
  s.reserve(n);
  for (std::size_t i = 0; i < n; ++i) s.push_back((rng() & 1u) ? '1' : '0');
  return s;
}

template <class BS>
BS make_stdex_bits(std::size_t n, unsigned seed)
{
  std::string s = pattern_string(n, seed);
  return BS(std::string_view{s});
}

StdStack make_std_bits(unsigned seed)
{
  std::mt19937 rng(seed);
  StdStack b;
  for (std::size_t i = 0; i < kStackBits; ++i) b[i] = static_cast<bool>(rng() & 1u);
  return b;
}

#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
DynHeap make_dyn_bits(std::size_t n, unsigned seed)
{
  std::string s = pattern_string(n, seed);
  return DynHeap(s);
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET

StdexStack const &stack_a()
{
  static StdexStack const b = make_stdex_bits<StdexStack>(kStackBits, 42);
  return b;
}
StdexStack const &stack_b()
{
  static StdexStack const b = make_stdex_bits<StdexStack>(kStackBits, 43);
  return b;
}
StdStack const &std_a()
{
  static StdStack const b = make_std_bits(42);
  return b;
}
StdStack const &std_b()
{
  static StdStack const b = make_std_bits(43);
  return b;
}
StdexHeap const &heap_a()
{
  static StdexHeap const b = make_stdex_bits<StdexHeap>(kHeapBits, 42);
  return b;
}
StdexHeap const &heap_b()
{
  static StdexHeap const b = make_stdex_bits<StdexHeap>(kHeapBits, 43);
  return b;
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
DynHeap const &dyn_a()
{
  static DynHeap const b = make_dyn_bits(kHeapBits, 42);
  return b;
}
DynHeap const &dyn_b()
{
  static DynHeap const b = make_dyn_bits(kHeapBits, 43);
  return b;
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET

// Sparse heap patterns for find benchmarks (a set bit every ~512 positions).
StdexHeap const &heap_sparse()
{
  static StdexHeap const b = [] {
    StdexHeap x(kHeapBits, false);
    for (std::size_t i = 0; i < kHeapBits; i += 512) x.set(i, true);
    return x;
  }();
  return b;
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
DynHeap const &dyn_sparse()
{
  static DynHeap const b = [] {
    DynHeap x(kHeapBits, 0);
    for (std::size_t i = 0; i < kHeapBits; i += 512) x.set(i, true);
    return x;
  }();
  return b;
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET

// --- Stack group: construction ------------------------------------------------
void BM_Stack_Stdex_CtorBool(benchmark::State &st)
{
  for (auto _ : st) {
    StdexStack b(kStackBits, true);
    benchmark::DoNotOptimize(b);
  }
  st.SetItemsProcessed(static_cast<std::int64_t>(st.iterations() * kStackBits));
}
void BM_Stack_Std_CtorBool(benchmark::State &st)
{
  for (auto _ : st) {
    StdStack b;
    b.set();
    benchmark::DoNotOptimize(b);
  }
  st.SetItemsProcessed(static_cast<std::int64_t>(st.iterations() * kStackBits));
}
void BM_Stack_Stdex_CtorString(benchmark::State &st)
{
  std::string const s = pattern_string(kStackBits, 7);
  std::string_view const sv{s};
  for (auto _ : st) {
    StdexStack b(sv);
    benchmark::DoNotOptimize(b);
  }
  st.SetItemsProcessed(static_cast<std::int64_t>(st.iterations() * kStackBits));
}
void BM_Stack_Std_CtorString(benchmark::State &st)
{
  std::string const s = pattern_string(kStackBits, 7);
  for (auto _ : st) {
    StdStack b(s);
    benchmark::DoNotOptimize(b);
  }
  st.SetItemsProcessed(static_cast<std::int64_t>(st.iterations() * kStackBits));
}

// --- Stack group: set/reset/flip/test -----------------------------------------
void BM_Stack_Stdex_SetAll(benchmark::State &st)
{
  StdexStack base;
  base.resize(kStackBits, false);
  for (auto _ : st) {
    auto c = base;
    c.set();
    benchmark::DoNotOptimize(c);
  }
}
void BM_Stack_Std_SetAll(benchmark::State &st)
{
  StdStack const base;
  for (auto _ : st) {
    auto c = base;
    c.set();
    benchmark::DoNotOptimize(c);
  }
}
void BM_Stack_Stdex_ResetAll(benchmark::State &st)
{
  auto const base = stack_a();
  for (auto _ : st) {
    auto c = base;
    c.reset();
    benchmark::DoNotOptimize(c);
  }
}
void BM_Stack_Std_ResetAll(benchmark::State &st)
{
  auto const base = std_a();
  for (auto _ : st) {
    auto c = base;
    c.reset();
    benchmark::DoNotOptimize(c);
  }
}
void BM_Stack_Stdex_FlipAll(benchmark::State &st)
{
  auto const base = stack_a();
  for (auto _ : st) {
    auto c = base;
    c.flip();
    benchmark::DoNotOptimize(c);
  }
}
void BM_Stack_Std_FlipAll(benchmark::State &st)
{
  auto const base = std_a();
  for (auto _ : st) {
    auto c = base;
    c.flip();
    benchmark::DoNotOptimize(c);
  }
}
void BM_Stack_Stdex_SetPos(benchmark::State &st)
{
  for (auto _ : st) {
    st.PauseTiming();
    StdexStack c(kStackBits, false);
    st.ResumeTiming();
    for (std::size_t i = 0; i < kStackBits; ++i) c.set(i, true);
    benchmark::DoNotOptimize(c);
  }
  st.SetItemsProcessed(static_cast<std::int64_t>(st.iterations() * kStackBits));
}
void BM_Stack_Std_SetPos(benchmark::State &st)
{
  for (auto _ : st) {
    StdStack c;
    for (std::size_t i = 0; i < kStackBits; ++i) c.set(i);
    benchmark::DoNotOptimize(c);
  }
  st.SetItemsProcessed(static_cast<std::int64_t>(st.iterations() * kStackBits));
}
void BM_Stack_Stdex_Test(benchmark::State &st)
{
  auto const &base = stack_a();
  for (auto _ : st) {
    std::size_t sum = 0;
    for (std::size_t i = 0; i < kStackBits; ++i) sum += base.test(i) ? 1u : 0u;
    benchmark::DoNotOptimize(sum);
  }
  st.SetItemsProcessed(static_cast<std::int64_t>(st.iterations() * kStackBits));
}
void BM_Stack_Std_Test(benchmark::State &st)
{
  auto const &base = std_a();
  for (auto _ : st) {
    std::size_t sum = 0;
    for (std::size_t i = 0; i < kStackBits; ++i) sum += base.test(i) ? 1u : 0u;
    benchmark::DoNotOptimize(sum);
  }
  st.SetItemsProcessed(static_cast<std::int64_t>(st.iterations() * kStackBits));
}
void BM_Stack_Stdex_IndexLoop(benchmark::State &st)
{
  auto const &base = stack_a();
  for (auto _ : st) {
    std::size_t sum = 0;
    for (std::size_t i = 0; i < kStackBits; ++i) sum += static_cast<bool>(base[i]) ? 1u : 0u;
    benchmark::DoNotOptimize(sum);
  }
  st.SetItemsProcessed(static_cast<std::int64_t>(st.iterations() * kStackBits));
}
void BM_Stack_Std_IndexLoop(benchmark::State &st)
{
  auto const &base = std_a();
  for (auto _ : st) {
    std::size_t sum = 0;
    for (std::size_t i = 0; i < kStackBits; ++i) sum += base[i] ? 1u : 0u;
    benchmark::DoNotOptimize(sum);
  }
  st.SetItemsProcessed(static_cast<std::int64_t>(st.iterations() * kStackBits));
}

// --- Stack group: count/any/all/none -------------------------------------------
void BM_Stack_Stdex_Count(benchmark::State &st)
{
  auto const &base = stack_a();
  for (auto _ : st) benchmark::DoNotOptimize(base.count());
}
void BM_Stack_Std_Count(benchmark::State &st)
{
  auto const &base = std_a();
  for (auto _ : st) benchmark::DoNotOptimize(base.count());
}
void BM_Stack_Stdex_Any(benchmark::State &st)
{
  auto const &base = stack_a();
  for (auto _ : st) benchmark::DoNotOptimize(base.any());
}
void BM_Stack_Std_Any(benchmark::State &st)
{
  auto const &base = std_a();
  for (auto _ : st) benchmark::DoNotOptimize(base.any());
}
void BM_Stack_Stdex_All(benchmark::State &st)
{
  auto const &base = stack_a();
  for (auto _ : st) benchmark::DoNotOptimize(base.all());
}
void BM_Stack_Std_All(benchmark::State &st)
{
  auto const &base = std_a();
  for (auto _ : st) benchmark::DoNotOptimize(base.all());
}
void BM_Stack_Stdex_None(benchmark::State &st)
{
  auto const &base = stack_a();
  for (auto _ : st) benchmark::DoNotOptimize(base.none());
}
void BM_Stack_Std_None(benchmark::State &st)
{
  auto const &base = std_a();
  for (auto _ : st) benchmark::DoNotOptimize(base.none());
}

// --- Stack group: bitwise -------------------------------------------------------
void BM_Stack_Stdex_And(benchmark::State &st)
{
  auto const &a = stack_a();
  auto const &b = stack_b();
  for (auto _ : st) benchmark::DoNotOptimize(a & b);
}
void BM_Stack_Std_And(benchmark::State &st)
{
  auto const &a = std_a();
  auto const &b = std_b();
  for (auto _ : st) benchmark::DoNotOptimize(a & b);
}
void BM_Stack_Stdex_Or(benchmark::State &st)
{
  auto const &a = stack_a();
  auto const &b = stack_b();
  for (auto _ : st) benchmark::DoNotOptimize(a | b);
}
void BM_Stack_Std_Or(benchmark::State &st)
{
  auto const &a = std_a();
  auto const &b = std_b();
  for (auto _ : st) benchmark::DoNotOptimize(a | b);
}
void BM_Stack_Stdex_Xor(benchmark::State &st)
{
  auto const &a = stack_a();
  auto const &b = stack_b();
  for (auto _ : st) benchmark::DoNotOptimize(a ^ b);
}
void BM_Stack_Std_Xor(benchmark::State &st)
{
  auto const &a = std_a();
  auto const &b = std_b();
  for (auto _ : st) benchmark::DoNotOptimize(a ^ b);
}
void BM_Stack_Stdex_Sub(benchmark::State &st) // stdex-only: std::bitset has no difference
{
  auto const &a = stack_a();
  auto const &b = stack_b();
  for (auto _ : st) benchmark::DoNotOptimize(a - b);
}
void BM_Stack_Stdex_Not(benchmark::State &st)
{
  auto const &a = stack_a();
  for (auto _ : st) benchmark::DoNotOptimize(~a);
}
void BM_Stack_Std_Not(benchmark::State &st)
{
  auto const &a = std_a();
  for (auto _ : st) benchmark::DoNotOptimize(~a);
}
void BM_Stack_Stdex_Eq(benchmark::State &st)
{
  auto const &a = stack_a();
  auto const &b = stack_b();
  for (auto _ : st) benchmark::DoNotOptimize(a == b);
}
void BM_Stack_Std_Eq(benchmark::State &st)
{
  auto const &a = std_a();
  auto const &b = std_b();
  for (auto _ : st) benchmark::DoNotOptimize(a == b);
}
void BM_Stack_Stdex_AndAssign(benchmark::State &st)
{
  auto const &a = stack_a();
  auto const &b = stack_b();
  for (auto _ : st) {
    st.PauseTiming();
    auto c = a;
    st.ResumeTiming();
    c &= b;
    benchmark::DoNotOptimize(c);
  }
}
void BM_Stack_Std_AndAssign(benchmark::State &st)
{
  auto const &a = std_a();
  auto const &b = std_b();
  for (auto _ : st) {
    st.PauseTiming();
    auto c = a;
    st.ResumeTiming();
    c &= b;
    benchmark::DoNotOptimize(c);
  }
}

// --- Stack group: shifts ----------------------------------------------------------
void BM_Stack_Stdex_Shl(benchmark::State &st)
{
  auto const &a = stack_a();
  for (auto _ : st) benchmark::DoNotOptimize(a << 7);
}
void BM_Stack_Std_Shl(benchmark::State &st)
{
  auto const &a = std_a();
  for (auto _ : st) benchmark::DoNotOptimize(a << 7);
}
void BM_Stack_Stdex_Shr(benchmark::State &st)
{
  auto const &a = stack_a();
  for (auto _ : st) benchmark::DoNotOptimize(a >> 7);
}
void BM_Stack_Std_Shr(benchmark::State &st)
{
  auto const &a = std_a();
  for (auto _ : st) benchmark::DoNotOptimize(a >> 7);
}
void BM_Stack_Stdex_ShlAssign(benchmark::State &st)
{
  auto const &a = stack_a();
  for (auto _ : st) {
    st.PauseTiming();
    auto c = a;
    st.ResumeTiming();
    c <<= 7;
    benchmark::DoNotOptimize(c);
  }
}
void BM_Stack_Std_ShlAssign(benchmark::State &st)
{
  auto const &a = std_a();
  for (auto _ : st) {
    st.PauseTiming();
    auto c = a;
    st.ResumeTiming();
    c <<= 7;
    benchmark::DoNotOptimize(c);
  }
}

// --- Stack group: conversions -------------------------------------------------------
void BM_Stack_Stdex_ToString(benchmark::State &st)
{
  auto const &a = stack_a();
  for (auto _ : st) benchmark::DoNotOptimize(a.to_string());
}
void BM_Stack_Std_ToString(benchmark::State &st)
{
  auto const &a = std_a();
  for (auto _ : st) benchmark::DoNotOptimize(a.to_string());
}
void BM_Stack_Stdex_ToUllong(benchmark::State &st) // low bits only so no overflow throw
{
  StdexStack a(kStackBits, false);
  a.set(0, 32, true);
  for (auto _ : st) benchmark::DoNotOptimize(a.to_ullong());
}
void BM_Stack_Std_ToUllong(benchmark::State &st)
{
  StdStack a;
  for (std::size_t i = 0; i < 32; ++i) a.set(i);
  for (auto _ : st) benchmark::DoNotOptimize(a.to_ullong());
}

// --- Stack group: stdex-only (no std counterpart) --------------------------------------
void BM_Stack_Stdex_SetRange(benchmark::State &st)
{
  for (auto _ : st) {
    st.PauseTiming();
    StdexStack c(kStackBits, false);
    st.ResumeTiming();
    c.set(17, 200, true);
    benchmark::DoNotOptimize(c);
  }
}
void BM_Stack_Stdex_FindFirst(benchmark::State &st)
{
  auto const &a = stack_a();
  for (auto _ : st) benchmark::DoNotOptimize(a.find_first());
}
// helper for the small sparse stack pattern used below
inline StdexStack heap_sparse_mini()
{
  StdexStack x(kStackBits, false);
  x.set(3, true);
  x.set(100, true);
  x.set(200, true);
  return x;
}
void BM_Stack_Stdex_FindNext(benchmark::State &st)
{
  auto const a = heap_sparse_mini();
  for (auto _ : st) {
    std::size_t p = a.find_first();
    std::size_t sum = 0;
    while (p != StdexStack::npos) {
      sum += p;
      p = a.find_next(p);
    }
    benchmark::DoNotOptimize(sum);
  }
}
void BM_Stack_Stdex_Subset(benchmark::State &st) // std::bitset has no subset API
{
  auto const &a = stack_a();
  auto const &b = stack_b();
  for (auto _ : st) benchmark::DoNotOptimize(a.is_subset_of(b));
}
void BM_Stack_Stdex_Iterate(benchmark::State &st) // iterators are stdex-only
{
  auto const &a = stack_a();
  for (auto _ : st) {
    std::size_t sum = 0;
    for (auto it = a.begin(); it != a.end(); ++it) sum += *it ? 1u : 0u;
    benchmark::DoNotOptimize(sum);
  }
}

// --- Heap group: construction ----------------------------------------------------------
// NOTE: sul::dynamic_bitset has no all-ones (nbits, bool) ctor: its init_val
// only seeds the low blocks, the rest stay zero. Both sides hence build the
// all-true bitset via resize(true) from empty so the timed work is equal.
void BM_Heap_Stdex_CtorBool(benchmark::State &st)
{
  for (auto _ : st) {
    StdexHeap b;
    b.resize(kHeapBits, true);
    benchmark::DoNotOptimize(b);
  }
  st.SetItemsProcessed(static_cast<std::int64_t>(st.iterations() * kHeapBits));
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_CtorBool(benchmark::State &st)
{
  for (auto _ : st) {
    DynHeap b;
    b.resize(kHeapBits, true);
    benchmark::DoNotOptimize(b);
  }
  st.SetItemsProcessed(static_cast<std::int64_t>(st.iterations() * kHeapBits));
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Stdex_CtorString(benchmark::State &st)
{
  std::string const s = pattern_string(kHeapBits, 7);
  std::string_view const sv{s};
  for (auto _ : st) {
    StdexHeap b(sv);
    benchmark::DoNotOptimize(b);
  }
  st.SetItemsProcessed(static_cast<std::int64_t>(st.iterations() * kHeapBits));
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_CtorString(benchmark::State &st)
{
  std::string const s = pattern_string(kHeapBits, 7);
  for (auto _ : st) {
    DynHeap b(s);
    benchmark::DoNotOptimize(b);
  }
  st.SetItemsProcessed(static_cast<std::int64_t>(st.iterations() * kHeapBits));
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET

// --- Heap group: set/reset/flip/test ------------------------------------------------------
// NOTE: the working copy is made under PauseTiming so only set/reset/flip is
// timed; otherwise a 512-block heap copy+allocation dominates the numbers.
void BM_Heap_Stdex_SetAll(benchmark::State &st)
{
  StdexHeap const base(kHeapBits, false);
  for (auto _ : st) {
    st.PauseTiming();
    auto c = base;
    st.ResumeTiming();
    c.set();
    benchmark::DoNotOptimize(c);
  }
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_SetAll(benchmark::State &st)
{
  DynHeap const base(kHeapBits, 0ULL);
  for (auto _ : st) {
    st.PauseTiming();
    auto c = base;
    st.ResumeTiming();
    c.set();
    benchmark::DoNotOptimize(c);
  }
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Stdex_ResetAll(benchmark::State &st)
{
  auto const base = heap_a();
  for (auto _ : st) {
    st.PauseTiming();
    auto c = base;
    st.ResumeTiming();
    c.reset();
    benchmark::DoNotOptimize(c);
  }
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_ResetAll(benchmark::State &st)
{
  auto const base = dyn_a();
  for (auto _ : st) {
    st.PauseTiming();
    auto c = base;
    st.ResumeTiming();
    c.reset();
    benchmark::DoNotOptimize(c);
  }
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Stdex_FlipAll(benchmark::State &st)
{
  auto const base = heap_a();
  for (auto _ : st) {
    st.PauseTiming();
    auto c = base;
    st.ResumeTiming();
    c.flip();
    benchmark::DoNotOptimize(c);
  }
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_FlipAll(benchmark::State &st)
{
  auto const base = dyn_a();
  for (auto _ : st) {
    st.PauseTiming();
    auto c = base;
    st.ResumeTiming();
    c.flip();
    benchmark::DoNotOptimize(c);
  }
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Stdex_SetRange(benchmark::State &st)
{
  for (auto _ : st) {
    st.PauseTiming();
    StdexHeap c(kHeapBits, false);
    st.ResumeTiming();
    c.set(1000, 20000, true);
    benchmark::DoNotOptimize(c);
  }
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_SetRange(benchmark::State &st)
{
  for (auto _ : st) {
    st.PauseTiming();
    DynHeap c(kHeapBits, 0ULL);
    st.ResumeTiming();
    c.set(1000, 20000, true);
    benchmark::DoNotOptimize(c);
  }
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Stdex_Test(benchmark::State &st)
{
  auto const &base = heap_a();
  for (auto _ : st) {
    std::size_t sum = 0;
    for (std::size_t i = 0; i < kHeapBits; i += 7) sum += base.test(i) ? 1u : 0u;
    benchmark::DoNotOptimize(sum);
  }
  st.SetItemsProcessed(static_cast<std::int64_t>(st.iterations() * (kHeapBits / 7)));
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_Test(benchmark::State &st)
{
  auto const &base = dyn_a();
  for (auto _ : st) {
    std::size_t sum = 0;
    for (std::size_t i = 0; i < kHeapBits; i += 7) sum += base.test(i) ? 1u : 0u;
    benchmark::DoNotOptimize(sum);
  }
  st.SetItemsProcessed(static_cast<std::int64_t>(st.iterations() * (kHeapBits / 7)));
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Stdex_IndexLoop(benchmark::State &st)
{
  auto const &base = heap_a();
  for (auto _ : st) {
    std::size_t sum = 0;
    for (std::size_t i = 0; i < kHeapBits; ++i) sum += static_cast<bool>(base[i]) ? 1u : 0u;
    benchmark::DoNotOptimize(sum);
  }
  st.SetItemsProcessed(static_cast<std::int64_t>(st.iterations() * kHeapBits));
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_IndexLoop(benchmark::State &st)
{
  auto const &base = dyn_a();
  for (auto _ : st) {
    std::size_t sum = 0;
    for (std::size_t i = 0; i < kHeapBits; ++i) sum += static_cast<bool>(base[i]) ? 1u : 0u;
    benchmark::DoNotOptimize(sum);
  }
  st.SetItemsProcessed(static_cast<std::int64_t>(st.iterations() * kHeapBits));
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET

// --- Heap group: count/any/all/none (SIMD hot paths above 256B) ------------------------------
void BM_Heap_Stdex_Count(benchmark::State &st)
{
  auto const &base = heap_a();
  for (auto _ : st) benchmark::DoNotOptimize(base.count());
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_Count(benchmark::State &st)
{
  auto const &base = dyn_a();
  for (auto _ : st) benchmark::DoNotOptimize(base.count());
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Stdex_Any(benchmark::State &st)
{
  auto const &base = heap_a();
  for (auto _ : st) benchmark::DoNotOptimize(base.any());
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_Any(benchmark::State &st)
{
  auto const &base = dyn_a();
  for (auto _ : st) benchmark::DoNotOptimize(base.any());
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Stdex_All(benchmark::State &st)
{
  StdexHeap const full(kHeapBits, true);
  for (auto _ : st) benchmark::DoNotOptimize(full.all());
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_All(benchmark::State &st)
{
  DynHeap full;
  full.resize(kHeapBits, true);
  for (auto _ : st) benchmark::DoNotOptimize(full.all());
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET

// --- Heap group: bitwise -------------------------------------------------------------------------
void BM_Heap_Stdex_And(benchmark::State &st)
{
  auto const &a = heap_a();
  auto const &b = heap_b();
  for (auto _ : st) benchmark::DoNotOptimize(a & b);
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_And(benchmark::State &st)
{
  auto const &a = dyn_a();
  auto const &b = dyn_b();
  for (auto _ : st) benchmark::DoNotOptimize(a & b);
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Stdex_Or(benchmark::State &st)
{
  auto const &a = heap_a();
  auto const &b = heap_b();
  for (auto _ : st) benchmark::DoNotOptimize(a | b);
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_Or(benchmark::State &st)
{
  auto const &a = dyn_a();
  auto const &b = dyn_b();
  for (auto _ : st) benchmark::DoNotOptimize(a | b);
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Stdex_Xor(benchmark::State &st)
{
  auto const &a = heap_a();
  auto const &b = heap_b();
  for (auto _ : st) benchmark::DoNotOptimize(a ^ b);
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_Xor(benchmark::State &st)
{
  auto const &a = dyn_a();
  auto const &b = dyn_b();
  for (auto _ : st) benchmark::DoNotOptimize(a ^ b);
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Stdex_Sub(benchmark::State &st)
{
  auto const &a = heap_a();
  auto const &b = heap_b();
  for (auto _ : st) benchmark::DoNotOptimize(a - b);
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_Sub(benchmark::State &st)
{
  auto const &a = dyn_a();
  auto const &b = dyn_b();
  for (auto _ : st) benchmark::DoNotOptimize(a - b);
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Stdex_Not(benchmark::State &st)
{
  auto const &a = heap_a();
  for (auto _ : st) benchmark::DoNotOptimize(~a);
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_Not(benchmark::State &st)
{
  auto const &a = dyn_a();
  for (auto _ : st) benchmark::DoNotOptimize(~a);
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Stdex_AndAssign(benchmark::State &st)
{
  auto const &a = heap_a();
  auto const &b = heap_b();
  for (auto _ : st) {
    st.PauseTiming();
    auto c = a;
    st.ResumeTiming();
    c &= b;
    benchmark::DoNotOptimize(c);
  }
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_AndAssign(benchmark::State &st)
{
  auto const &a = dyn_a();
  auto const &b = dyn_b();
  for (auto _ : st) {
    st.PauseTiming();
    auto c = a;
    st.ResumeTiming();
    c &= b;
    benchmark::DoNotOptimize(c);
  }
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Stdex_Eq(benchmark::State &st)
{
  auto const &a = heap_a();
  auto const &b = heap_b();
  for (auto _ : st) benchmark::DoNotOptimize(a == b);
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_Eq(benchmark::State &st)
{
  auto const &a = dyn_a();
  auto const &b = dyn_b();
  for (auto _ : st) benchmark::DoNotOptimize(a == b);
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET

// --- Heap group: shifts -------------------------------------------------------------------------------
void BM_Heap_Stdex_Shl(benchmark::State &st)
{
  auto const &a = heap_a();
  for (auto _ : st) benchmark::DoNotOptimize(a << 100);
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_Shl(benchmark::State &st)
{
  auto const &a = dyn_a();
  for (auto _ : st) benchmark::DoNotOptimize(a << 100);
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Stdex_Shr(benchmark::State &st)
{
  auto const &a = heap_a();
  for (auto _ : st) benchmark::DoNotOptimize(a >> 100);
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_Shr(benchmark::State &st)
{
  auto const &a = dyn_a();
  for (auto _ : st) benchmark::DoNotOptimize(a >> 100);
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET

// --- Heap group: find/subset -------------------------------------------------------------------------------
void BM_Heap_Stdex_FindFirst(benchmark::State &st)
{
  auto const &a = heap_sparse();
  for (auto _ : st) benchmark::DoNotOptimize(a.find_first());
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_FindFirst(benchmark::State &st)
{
  auto const &a = dyn_sparse();
  for (auto _ : st) benchmark::DoNotOptimize(a.find_first());
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Stdex_FindNext(benchmark::State &st)
{
  auto const &a = heap_sparse();
  for (auto _ : st) {
    std::size_t p = a.find_first();
    std::size_t sum = 0;
    while (p != StdexHeap::npos) {
      sum += p;
      p = a.find_next(p);
    }
    benchmark::DoNotOptimize(sum);
  }
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_FindNext(benchmark::State &st)
{
  auto const &a = dyn_sparse();
  for (auto _ : st) {
    std::size_t p = a.find_first();
    std::size_t sum = 0;
    while (p != DynHeap::npos) {
      sum += p;
      p = a.find_next(p);
    }
    benchmark::DoNotOptimize(sum);
  }
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Stdex_Subset(benchmark::State &st)
{
  auto const &a = heap_a();
  auto const &b = heap_b();
  for (auto _ : st) benchmark::DoNotOptimize(a.is_subset_of(b));
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_Subset(benchmark::State &st)
{
  auto const &a = dyn_a();
  auto const &b = dyn_b();
  for (auto _ : st) benchmark::DoNotOptimize(a.is_subset_of(b));
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Stdex_Intersects(benchmark::State &st)
{
  auto const &a = heap_a();
  auto const &b = heap_b();
  for (auto _ : st) benchmark::DoNotOptimize(a.intersects(b));
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_Intersects(benchmark::State &st)
{
  auto const &a = dyn_a();
  auto const &b = dyn_b();
  for (auto _ : st) benchmark::DoNotOptimize(a.intersects(b));
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
// stdex-only zero-find (dynamic_bitset has no zero-find API)
void BM_Heap_Stdex_FindFirstZero(benchmark::State &st)
{
  auto const &a = heap_a();
  for (auto _ : st) benchmark::DoNotOptimize(a.find_first_zero());
}

// --- Heap group: growth/mutation (no std::bitset counterpart) ------------------------------------------
void BM_Heap_Stdex_PushBack(benchmark::State &st)
{
  for (auto _ : st) {
    st.PauseTiming();
    StdexHeap c(kHeapBits, false);
    st.ResumeTiming();
    for (int i = 0; i < 256; ++i) c.push_back(i % 2 == 0);
    benchmark::DoNotOptimize(c);
  }
  st.SetItemsProcessed(static_cast<std::int64_t>(st.iterations() * 256));
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_PushBack(benchmark::State &st)
{
  for (auto _ : st) {
    st.PauseTiming();
    DynHeap c(kHeapBits, 0ULL);
    st.ResumeTiming();
    for (int i = 0; i < 256; ++i) c.push_back(i % 2 == 0);
    benchmark::DoNotOptimize(c);
  }
  st.SetItemsProcessed(static_cast<std::int64_t>(st.iterations() * 256));
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Stdex_Resize(benchmark::State &st)
{
  for (auto _ : st) {
    st.PauseTiming();
    StdexHeap c(kHeapBits, false);
    st.ResumeTiming();
    c.resize(kHeapBits + 1024, true);
    benchmark::DoNotOptimize(c);
  }
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_Resize(benchmark::State &st)
{
  for (auto _ : st) {
    st.PauseTiming();
    DynHeap c(kHeapBits, 0ULL);
    st.ResumeTiming();
    c.resize(kHeapBits + 1024, true);
    benchmark::DoNotOptimize(c);
  }
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET

// --- Heap group: conversions ---------------------------------------------------------------------------------
void BM_Heap_Stdex_ToString(benchmark::State &st)
{
  auto const &a = heap_a();
  for (auto _ : st) benchmark::DoNotOptimize(a.to_string());
}
#ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
void BM_Heap_Dyn_ToString(benchmark::State &st)
{
  auto const &a = dyn_a();
  for (auto _ : st) benchmark::DoNotOptimize(a.to_string());
}
#endif // STDEX_BENCH_HAS_DYNAMIC_BITSET

// --- Heap group: stdex-only block/iterator/insert surface --------------------------------------------------------
// No counterparts in std::bitset/sul::dynamic_bitset; measured alone for completeness.
void BM_Heap_Stdex_BlockIterate(benchmark::State &st)
{
  auto const &a = heap_a();
  for (auto _ : st) {
    std::size_t sum = 0;
    for (auto it = a.block_begin(); it != a.block_end(); ++it) sum += static_cast<std::size_t>(*it != 0);
    benchmark::DoNotOptimize(sum);
  }
}
void BM_Heap_Stdex_BitIterate(benchmark::State &st)
{
  auto const &a = heap_a();
  for (auto _ : st) {
    std::size_t sum = 0;
    for (auto it = a.begin(); it != a.end(); ++it) sum += *it ? 1u : 0u;
    benchmark::DoNotOptimize(sum);
  }
}
void BM_Heap_Stdex_InsertErase(benchmark::State &st)
{
  for (auto _ : st) {
    st.PauseTiming();
    auto c = heap_a();
    st.ResumeTiming();
    c.insert(1000, 64, true);
    c.erase(1000, 1064);
    benchmark::DoNotOptimize(c);
  }
}

struct Registrar
{
  Registrar()
  {
    using S = benchmark::State;
    auto reg = [](char const *n, auto fn) {
      benchmark::RegisterBenchmark(n, fn)->Unit(benchmark::kMicrosecond);
    };
    // Stack vs stack.
    reg("Stack/Stdex_CtorBool", BM_Stack_Stdex_CtorBool);
    reg("Stack/Std_CtorBool", BM_Stack_Std_CtorBool);
    reg("Stack/Stdex_CtorString", BM_Stack_Stdex_CtorString);
    reg("Stack/Std_CtorString", BM_Stack_Std_CtorString);
    reg("Stack/Stdex_SetAll", BM_Stack_Stdex_SetAll);
    reg("Stack/Std_SetAll", BM_Stack_Std_SetAll);
    reg("Stack/Stdex_ResetAll", BM_Stack_Stdex_ResetAll);
    reg("Stack/Std_ResetAll", BM_Stack_Std_ResetAll);
    reg("Stack/Stdex_FlipAll", BM_Stack_Stdex_FlipAll);
    reg("Stack/Std_FlipAll", BM_Stack_Std_FlipAll);
    reg("Stack/Stdex_SetPos", BM_Stack_Stdex_SetPos);
    reg("Stack/Std_SetPos", BM_Stack_Std_SetPos);
    reg("Stack/Stdex_Test", BM_Stack_Stdex_Test);
    reg("Stack/Std_Test", BM_Stack_Std_Test);
    reg("Stack/Stdex_IndexLoop", BM_Stack_Stdex_IndexLoop);
    reg("Stack/Std_IndexLoop", BM_Stack_Std_IndexLoop);
    reg("Stack/Stdex_Count", BM_Stack_Stdex_Count);
    reg("Stack/Std_Count", BM_Stack_Std_Count);
    reg("Stack/Stdex_Any", BM_Stack_Stdex_Any);
    reg("Stack/Std_Any", BM_Stack_Std_Any);
    reg("Stack/Stdex_All", BM_Stack_Stdex_All);
    reg("Stack/Std_All", BM_Stack_Std_All);
    reg("Stack/Stdex_None", BM_Stack_Stdex_None);
    reg("Stack/Std_None", BM_Stack_Std_None);
    reg("Stack/Stdex_And", BM_Stack_Stdex_And);
    reg("Stack/Std_And", BM_Stack_Std_And);
    reg("Stack/Stdex_Or", BM_Stack_Stdex_Or);
    reg("Stack/Std_Or", BM_Stack_Std_Or);
    reg("Stack/Stdex_Xor", BM_Stack_Stdex_Xor);
    reg("Stack/Std_Xor", BM_Stack_Std_Xor);
    reg("Stack/Stdex_Sub_stdexOnly", BM_Stack_Stdex_Sub);
    reg("Stack/Stdex_Not", BM_Stack_Stdex_Not);
    reg("Stack/Std_Not", BM_Stack_Std_Not);
    reg("Stack/Stdex_Eq", BM_Stack_Stdex_Eq);
    reg("Stack/Std_Eq", BM_Stack_Std_Eq);
    reg("Stack/Stdex_AndAssign", BM_Stack_Stdex_AndAssign);
    reg("Stack/Std_AndAssign", BM_Stack_Std_AndAssign);
    reg("Stack/Stdex_Shl", BM_Stack_Stdex_Shl);
    reg("Stack/Std_Shl", BM_Stack_Std_Shl);
    reg("Stack/Stdex_Shr", BM_Stack_Stdex_Shr);
    reg("Stack/Std_Shr", BM_Stack_Std_Shr);
    reg("Stack/Stdex_ShlAssign", BM_Stack_Stdex_ShlAssign);
    reg("Stack/Std_ShlAssign", BM_Stack_Std_ShlAssign);
    reg("Stack/Stdex_ToString", BM_Stack_Stdex_ToString);
    reg("Stack/Std_ToString", BM_Stack_Std_ToString);
    reg("Stack/Stdex_ToUllong", BM_Stack_Stdex_ToUllong);
    reg("Stack/Std_ToUllong", BM_Stack_Std_ToUllong);
    reg("Stack/Stdex_SetRange_stdexOnly", BM_Stack_Stdex_SetRange);
    reg("Stack/Stdex_FindFirst_stdexOnly", BM_Stack_Stdex_FindFirst);
    reg("Stack/Stdex_FindNext_stdexOnly", BM_Stack_Stdex_FindNext);
    reg("Stack/Stdex_Subset_stdexOnly", BM_Stack_Stdex_Subset);
    reg("Stack/Stdex_Iterate_stdexOnly", BM_Stack_Stdex_Iterate);
    // Heap vs heap.
    reg("Heap/Stdex_CtorBool", BM_Heap_Stdex_CtorBool);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_CtorBool", BM_Heap_Dyn_CtorBool);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_CtorString", BM_Heap_Stdex_CtorString);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_CtorString", BM_Heap_Dyn_CtorString);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_SetAll", BM_Heap_Stdex_SetAll);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_SetAll", BM_Heap_Dyn_SetAll);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_ResetAll", BM_Heap_Stdex_ResetAll);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_ResetAll", BM_Heap_Dyn_ResetAll);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_FlipAll", BM_Heap_Stdex_FlipAll);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_FlipAll", BM_Heap_Dyn_FlipAll);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_SetRange", BM_Heap_Stdex_SetRange);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_SetRange", BM_Heap_Dyn_SetRange);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_Test", BM_Heap_Stdex_Test);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_Test", BM_Heap_Dyn_Test);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_IndexLoop", BM_Heap_Stdex_IndexLoop);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_IndexLoop", BM_Heap_Dyn_IndexLoop);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_Count", BM_Heap_Stdex_Count);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_Count", BM_Heap_Dyn_Count);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_Any", BM_Heap_Stdex_Any);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_Any", BM_Heap_Dyn_Any);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_All", BM_Heap_Stdex_All);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_All", BM_Heap_Dyn_All);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_And", BM_Heap_Stdex_And);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_And", BM_Heap_Dyn_And);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_Or", BM_Heap_Stdex_Or);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_Or", BM_Heap_Dyn_Or);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_Xor", BM_Heap_Stdex_Xor);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_Xor", BM_Heap_Dyn_Xor);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_Sub", BM_Heap_Stdex_Sub);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_Sub", BM_Heap_Dyn_Sub);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_Not", BM_Heap_Stdex_Not);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_Not", BM_Heap_Dyn_Not);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_AndAssign", BM_Heap_Stdex_AndAssign);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_AndAssign", BM_Heap_Dyn_AndAssign);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_Eq", BM_Heap_Stdex_Eq);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_Eq", BM_Heap_Dyn_Eq);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_Shl", BM_Heap_Stdex_Shl);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_Shl", BM_Heap_Dyn_Shl);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_Shr", BM_Heap_Stdex_Shr);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_Shr", BM_Heap_Dyn_Shr);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_FindFirst", BM_Heap_Stdex_FindFirst);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_FindFirst", BM_Heap_Dyn_FindFirst);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_FindNext", BM_Heap_Stdex_FindNext);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_FindNext", BM_Heap_Dyn_FindNext);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_Subset", BM_Heap_Stdex_Subset);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_Subset", BM_Heap_Dyn_Subset);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_Intersects", BM_Heap_Stdex_Intersects);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_Intersects", BM_Heap_Dyn_Intersects);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_FindFirstZero_stdexOnly", BM_Heap_Stdex_FindFirstZero);
    reg("Heap/Stdex_PushBack", BM_Heap_Stdex_PushBack);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_PushBack", BM_Heap_Dyn_PushBack);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_Resize", BM_Heap_Stdex_Resize);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_Resize", BM_Heap_Dyn_Resize);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_ToString", BM_Heap_Stdex_ToString);
    #ifdef STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Dyn_ToString", BM_Heap_Dyn_ToString);
    #endif // STDEX_BENCH_HAS_DYNAMIC_BITSET
    reg("Heap/Stdex_BlockIterate_stdexOnly", BM_Heap_Stdex_BlockIterate);
    reg("Heap/Stdex_BitIterate_stdexOnly", BM_Heap_Stdex_BitIterate);
    reg("Heap/Stdex_InsertErase_stdexOnly", BM_Heap_Stdex_InsertErase);
  }
} g_registrar;
} // namespace
