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

// bench_int128.cpp
//
// Compares stdex::uint128_t against:
//   * the compiler's native unsigned __int128 (GCC/Clang),
//   * absl::uint128 when Abseil is available (vcpkg on Windows),
//   * a local two-word baseline that uses the same _umul128/_addcarry_u64
//     primitives as the MSVC STL implementation, so MSVC has a meaningful
//     "MSVC-style" reference even though it has no native 128-bit scalar.
//
// Build/run on Windows through CMake (vcpkg supplies Google Benchmark and
// Abseil). On WSL, build Google Benchmark from source and compile this file
// with -I<benchmark>/include -L<build>/src -lbenchmark -lbenchmark_main.

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

#if defined(_MSC_VER)
#  include <intrin.h>
#endif

#include "bit_traits.hpp"
#include "int128.hpp"

#if defined(__has_include)
#  if __has_include(<absl/numeric/int128.h>)
#    include <absl/numeric/int128.h>
#    define STDEX_BENCH_HAS_ABSL 1
#  endif
#endif

#ifndef STDEX_BENCH_HAS_ABSL
#  define STDEX_BENCH_HAS_ABSL 0
#endif

namespace
{
constexpr std::size_t kSamples = 1024;

#if defined(__SIZEOF_INT128__)
using native_uint128 = __uint128_t;
#endif

// A deliberately small MSVC-STL-style baseline: two 64-bit words plus the
// same intrinsics MSVC uses for wide arithmetic.
struct alignas(16) msvc_uint128
{
  std::uint64_t low;
  std::uint64_t high;
};

constexpr msvc_uint128 msvc_add(msvc_uint128 const lhs, msvc_uint128 const rhs) noexcept
{
  msvc_uint128 result {};
  result.low = lhs.low + rhs.low;
  result.high = lhs.high + rhs.high + (result.low < lhs.low ? 1ULL : 0ULL);
  return result;
}

constexpr msvc_uint128 msvc_mul_exact(msvc_uint128 const lhs, msvc_uint128 const rhs) noexcept
{
#if defined(_MSC_VER) && defined(_M_X64)
  if (!std::is_constant_evaluated()) {
    std::uint64_t high = 0;
    std::uint64_t const low = _umul128(lhs.low, rhs.low, &high);
    high += lhs.low * rhs.high + lhs.high * rhs.low;
    return { low, high };
  }
#endif
  // 64x64 -> 128 partial product, then the cross terms (same shape as the
  // _umul128 path; constexpr evaluation uses the split fallback).
  std::uint64_t const lhs_low  = static_cast<std::uint32_t>(lhs.low);
  std::uint64_t const lhs_high = lhs.low >> 32U;
  std::uint64_t const rhs_low  = static_cast<std::uint32_t>(rhs.low);
  std::uint64_t const rhs_high = rhs.low >> 32U;
  std::uint64_t const p00      = lhs_low * rhs_low;
  std::uint64_t const p01      = lhs_low * rhs_high;
  std::uint64_t const p10      = lhs_high * rhs_low;
  std::uint64_t const p11      = lhs_high * rhs_high;
  std::uint64_t const middle   = (p00 >> 32U) + static_cast<std::uint32_t>(p01) + static_cast<std::uint32_t>(p10);
  std::uint64_t const low      = (middle << 32U) + static_cast<std::uint32_t>(p00);
  std::uint64_t const product_high = p11 + (p01 >> 32U) + (p10 >> 32U) + (middle >> 32U);
  std::uint64_t const high         = lhs.low * rhs.high + lhs.high * rhs.low + product_high;
  return { low, high };
}

std::uint64_t g_low[kSamples] {};
std::uint64_t g_high[kSamples] {};
std::uint64_t g_divisor[kSamples] {};

void initialize_data()
{
  std::uint64_t state = 0x9E37'79B9'7F4A'7C15ULL;
  for (std::size_t index = 0; index < kSamples; ++index) {
    state += 0x9E37'79B9'7F4A'7C15ULL;
    std::uint64_t z = state;
    z               = (z ^ (z >> 30U)) * 0xBF58'476D'1CE4'E5B9ULL;
    z               = (z ^ (z >> 27U)) * 0x94D0'49BB'1331'11EBULL;
    g_low[index]    = z ^ (z >> 31U);
    state += 0x9E37'79B9'7F4A'7C15ULL;
    z               = state;
    z               = (z ^ (z >> 30U)) * 0xBF58'476D'1CE4'E5B9ULL;
    z               = (z ^ (z >> 27U)) * 0x94D0'49BB'1331'11EBULL;
    g_high[index]   = z ^ (z >> 31U);
    g_divisor[index] = (g_low[index] | 1U) ^ (g_high[index] << 1U);
    if (g_divisor[index] == 0) g_divisor[index] = 1;
  }
}

struct initialize_guard
{
  initialize_guard() { initialize_data(); }
};

initialize_guard const g_initialize {};

} // namespace

// ---------------------------------------------------------------------------
// add
// ---------------------------------------------------------------------------

static void BM_StdexAdd(benchmark::State &state)
{
  std::size_t index = 0;
  for (auto _ : state) {
    stdex::uint128_t const lhs = stdex::uint128_t::from_words(g_low[index % kSamples], g_high[index % kSamples]);
    stdex::uint128_t const rhs = stdex::uint128_t::from_words(g_low[(index + 1) % kSamples], g_high[(index + 1) % kSamples]);
    stdex::uint128_t result = lhs + rhs;
    benchmark::DoNotOptimize(result);
    ++index;
  }
}

static void BM_MsvcAdd(benchmark::State &state)
{
  std::size_t index = 0;
  for (auto _ : state) {
    msvc_uint128 result = msvc_add({ g_low[index % kSamples], g_high[index % kSamples] },
                                         { g_low[(index + 1) % kSamples], g_high[(index + 1) % kSamples] });
    benchmark::DoNotOptimize(result);
    ++index;
  }
}

#if defined(__SIZEOF_INT128__)
static void BM_NativeAdd(benchmark::State &state)
{
  std::size_t index = 0;
  for (auto _ : state) {
    native_uint128 const lhs    = (static_cast<native_uint128>(g_high[index % kSamples]) << 64U) | g_low[index % kSamples];
    native_uint128 const rhs    = (static_cast<native_uint128>(g_high[(index + 1) % kSamples]) << 64U) | g_low[(index + 1) % kSamples];
    native_uint128 result = lhs + rhs;
    benchmark::DoNotOptimize(result);
    ++index;
  }
}
#endif

#if STDEX_BENCH_HAS_ABSL
static void BM_AbslAdd(benchmark::State &state)
{
  std::size_t index = 0;
  for (auto _ : state) {
    absl::uint128 const lhs    = absl::MakeUint128(g_high[index % kSamples], g_low[index % kSamples]);
    absl::uint128 const rhs    = absl::MakeUint128(g_high[(index + 1) % kSamples], g_low[(index + 1) % kSamples]);
    absl::uint128 result = lhs + rhs;
    benchmark::DoNotOptimize(result);
    ++index;
  }
}
#endif

// ---------------------------------------------------------------------------
// multiply
// ---------------------------------------------------------------------------

static void BM_StdexMul(benchmark::State &state)
{
  std::size_t index = 0;
  for (auto _ : state) {
    stdex::uint128_t const lhs = stdex::uint128_t::from_words(g_low[index % kSamples], g_high[index % kSamples]);
    stdex::uint128_t const rhs = stdex::uint128_t::from_words(g_low[(index + 1) % kSamples], g_high[(index + 1) % kSamples]);
    stdex::uint128_t result = lhs * rhs;
    benchmark::DoNotOptimize(result);
    ++index;
  }
}

static void BM_MsvcMul(benchmark::State &state)
{
  std::size_t index = 0;
  for (auto _ : state) {
    msvc_uint128 result = msvc_mul_exact({ g_low[index % kSamples], g_high[index % kSamples] },
                                               { g_low[(index + 1) % kSamples], g_high[(index + 1) % kSamples] });
    benchmark::DoNotOptimize(result);
    ++index;
  }
}

#if defined(__SIZEOF_INT128__)
static void BM_NativeMul(benchmark::State &state)
{
  std::size_t index = 0;
  for (auto _ : state) {
    native_uint128 const lhs    = (static_cast<native_uint128>(g_high[index % kSamples]) << 64U) | g_low[index % kSamples];
    native_uint128 const rhs    = (static_cast<native_uint128>(g_high[(index + 1) % kSamples]) << 64U) | g_low[(index + 1) % kSamples];
    native_uint128 result = lhs * rhs;
    benchmark::DoNotOptimize(result);
    ++index;
  }
}
#endif

#if STDEX_BENCH_HAS_ABSL
static void BM_AbslMul(benchmark::State &state)
{
  std::size_t index = 0;
  for (auto _ : state) {
    absl::uint128 const lhs    = absl::MakeUint128(g_high[index % kSamples], g_low[index % kSamples]);
    absl::uint128 const rhs    = absl::MakeUint128(g_high[(index + 1) % kSamples], g_low[(index + 1) % kSamples]);
    absl::uint128 result = lhs * rhs;
    benchmark::DoNotOptimize(result);
    ++index;
  }
}
#endif

// ---------------------------------------------------------------------------
// divide
// ---------------------------------------------------------------------------

static void BM_StdexDiv(benchmark::State &state)
{
  std::size_t index = 0;
  for (auto _ : state) {
    stdex::uint128_t const lhs = stdex::uint128_t::from_words(g_low[index % kSamples], g_high[index % kSamples] >> 2U);
    stdex::uint128_t const rhs = stdex::uint128_t::from_words(g_divisor[index % kSamples], 0);
    stdex::uint128_t result = lhs / rhs;
    benchmark::DoNotOptimize(result);
    ++index;
  }
}

#if defined(__SIZEOF_INT128__)
static void BM_NativeDiv(benchmark::State &state)
{
  std::size_t index = 0;
  for (auto _ : state) {
    native_uint128 const lhs    = (static_cast<native_uint128>(g_high[index % kSamples] >> 2U) << 64U) | g_low[index % kSamples];
    native_uint128 const rhs    = g_divisor[index % kSamples];
    native_uint128 result = lhs / rhs;
    benchmark::DoNotOptimize(result);
    ++index;
  }
}
#endif

#if STDEX_BENCH_HAS_ABSL
static void BM_AbslDiv(benchmark::State &state)
{
  std::size_t index = 0;
  for (auto _ : state) {
    absl::uint128 const lhs    = absl::MakeUint128(g_high[index % kSamples] >> 2U, g_low[index % kSamples]);
    absl::uint128 const rhs    = absl::MakeUint128(0, g_divisor[index % kSamples]);
    absl::uint128 result = lhs / rhs;
    benchmark::DoNotOptimize(result);
    ++index;
  }
}
#endif

// ---------------------------------------------------------------------------
// bit operations
// ---------------------------------------------------------------------------

static void BM_StdexPopcount(benchmark::State &state)
{
  std::size_t index = 0;
  for (auto _ : state) {
    stdex::uint128_t const value = stdex::uint128_t::from_words(g_low[index % kSamples], g_high[index % kSamples]);
    benchmark::DoNotOptimize(stdex::popcount(value));
    ++index;
  }
}

static void BM_StdexRotate(benchmark::State &state)
{
  std::size_t index = 0;
  for (auto _ : state) {
    stdex::uint128_t const value = stdex::uint128_t::from_words(g_low[index % kSamples], g_high[index % kSamples]);
    benchmark::DoNotOptimize(stdex::rotl(value, 37));
    ++index;
  }
}

static void BM_StdexCompare(benchmark::State &state)
{
  std::size_t index = 0;
  for (auto _ : state) {
    stdex::uint128_t const lhs = stdex::uint128_t::from_words(g_low[index % kSamples], g_high[index % kSamples]);
    stdex::uint128_t const rhs = stdex::uint128_t::from_words(g_low[(index + 1) % kSamples], g_high[(index + 1) % kSamples]);
    benchmark::DoNotOptimize(lhs < rhs);
    ++index;
  }
}

#if defined(__SIZEOF_INT128__)
static void BM_NativeCompare(benchmark::State &state)
{
  std::size_t index = 0;
  for (auto _ : state) {
    native_uint128 const lhs = (static_cast<native_uint128>(g_high[index % kSamples]) << 64U) | g_low[index % kSamples];
    native_uint128 const rhs = (static_cast<native_uint128>(g_high[(index + 1) % kSamples]) << 64U) | g_low[(index + 1) % kSamples];
    benchmark::DoNotOptimize(lhs < rhs);
    ++index;
  }
}
#endif

BENCHMARK(BM_StdexAdd);
BENCHMARK(BM_MsvcAdd);
#if defined(__SIZEOF_INT128__)
BENCHMARK(BM_NativeAdd);
#endif
#if STDEX_BENCH_HAS_ABSL
BENCHMARK(BM_AbslAdd);
#endif

BENCHMARK(BM_StdexMul);
BENCHMARK(BM_MsvcMul);
#if defined(__SIZEOF_INT128__)
BENCHMARK(BM_NativeMul);
#endif
#if STDEX_BENCH_HAS_ABSL
BENCHMARK(BM_AbslMul);
#endif

BENCHMARK(BM_StdexDiv);
#if defined(__SIZEOF_INT128__)
BENCHMARK(BM_NativeDiv);
#endif
#if STDEX_BENCH_HAS_ABSL
BENCHMARK(BM_AbslDiv);
#endif

BENCHMARK(BM_StdexPopcount);
BENCHMARK(BM_StdexRotate);
BENCHMARK(BM_StdexCompare);
#if defined(__SIZEOF_INT128__)
BENCHMARK(BM_NativeCompare);
#endif

BENCHMARK_MAIN();
