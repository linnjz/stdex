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

#include "stable_radix_sort.hpp"

#if __has_include(<boost/sort/pdqsort/pdqsort.hpp>)
#  include <boost/sort/pdqsort/pdqsort.hpp>
#  include <boost/sort/spreadsort/float_sort.hpp>
#  include <boost/sort/spreadsort/integer_sort.hpp>
#  define STDEX_HAVE_BOOST_SORT 1
#endif

#include <algorithm>
#include <benchmark/benchmark.h>
#include <cstdint>
#include <random>
#include <span>
#include <type_traits>
#include <vector>

#if defined(_MSC_VER) && !defined(__clang__)
#  include <ppl.h>
#endif

#if defined(__cpp_lib_parallel_algorithm) || (defined(_MSC_VER) && !defined(__clang__))
#  include <execution>
#endif

namespace
{

constexpr uint32_t kSeed = 42;

constexpr stdex::radix_sort_options kLsdSeq = [] {
  stdex::radix_sort_options opts {};
  opts.sort_strategy    = stdex::radix_sort_strategy::lsd;
  opts.execution_policy = stdex::radix_execution_policy::seq;
  return opts;
}();

constexpr stdex::radix_sort_options kMsdSeq = [] {
  stdex::radix_sort_options opts {};
  opts.sort_strategy    = stdex::radix_sort_strategy::msd;
  opts.execution_policy = stdex::radix_execution_policy::seq;
  return opts;
}();

#ifdef _OPENMP
constexpr stdex::radix_sort_options kLsdPar = [] {
  stdex::radix_sort_options opts {};
  opts.sort_strategy    = stdex::radix_sort_strategy::lsd;
  opts.execution_policy = stdex::radix_execution_policy::par;
  return opts;
}();

constexpr stdex::radix_sort_options kMsdPar = [] {
  stdex::radix_sort_options opts {};
  opts.sort_strategy    = stdex::radix_sort_strategy::msd;
  opts.execution_policy = stdex::radix_execution_policy::par;
  return opts;
}();
#endif

template <typename T>
std::span<T const> generateData(size_t n)
{
  static std::vector<T> data = [] {
    std::vector<T> v(1000000);
    std::mt19937_64 gen(kSeed);
    if constexpr (std::is_floating_point_v<T>) {
      std::uniform_real_distribution<T> dist(-1e9, 1e9);
      std::generate(v.begin(), v.end(), [&] { return dist(gen); });
    } else {
      std::uniform_int_distribution<T> dist;
      std::generate(v.begin(), v.end(), [&] { return dist(gen); });
    }
    return v;
  }();
  return { data.data(), n };
}

template <typename T>
void stdSortKernel(size_t n)
{
  auto data = generateData<T>(n);
  std::vector<T> copy(data.begin(), data.end());
  std::sort(copy.begin(), copy.end());
  benchmark::DoNotOptimize(copy);
}

template <typename T>
void stdStableSortKernel(size_t n)
{
  auto data = generateData<T>(n);
  std::vector<T> copy(data.begin(), data.end());
  std::stable_sort(copy.begin(), copy.end());
  benchmark::DoNotOptimize(copy);
}

template <typename T>
void radixLsdSeqKernel(size_t n)
{
  auto data = generateData<T>(n);
  std::vector<T> copy(data.begin(), data.end());
  stdex::stable_radix_sort<kLsdSeq>(copy.begin(), copy.end());
  benchmark::DoNotOptimize(copy);
}

template <typename T>
void radixMsdSeqKernel(size_t n)
{
  auto data = generateData<T>(n);
  std::vector<T> copy(data.begin(), data.end());
  stdex::stable_radix_sort<kMsdSeq>(copy.begin(), copy.end());
  benchmark::DoNotOptimize(copy);
}

#ifdef STDEX_HAVE_BOOST_SORT
template <typename T>
void spreadsortKernel(size_t n)
{
  auto data = generateData<T>(n);
  std::vector<T> copy(data.begin(), data.end());
  if constexpr (std::is_floating_point_v<T>) {
    boost::sort::spreadsort::float_sort(copy.begin(), copy.end());
  } else {
    boost::sort::spreadsort::integer_sort(copy.begin(), copy.end());
  }
  benchmark::DoNotOptimize(copy);
}

template <typename T>
void pdqsortKernel(size_t n)
{
  auto data = generateData<T>(n);
  std::vector<T> copy(data.begin(), data.end());
  boost::sort::pdqsort(copy.begin(), copy.end());
  benchmark::DoNotOptimize(copy);
}
#else
template <typename T>
void spreadsortKernel(size_t)
{
}

template <typename T>
void pdqsortKernel(size_t)
{
}
#endif

#ifdef _OPENMP

#  if defined(__cpp_lib_parallel_algorithm) || (defined(_MSC_VER) && !defined(__clang__))
template <typename T>
void stdSortParKernel(size_t n)
{
  auto data = generateData<T>(n);
  std::vector<T> copy(data.begin(), data.end());
  std::sort(std::execution::par, copy.begin(), copy.end());
  benchmark::DoNotOptimize(copy);
}

template <typename T>
void stdStableSortParKernel(size_t n)
{
  auto data = generateData<T>(n);
  std::vector<T> copy(data.begin(), data.end());
  std::stable_sort(std::execution::par, copy.begin(), copy.end());
  benchmark::DoNotOptimize(copy);
}
#  endif

template <typename T>
void radixLsdParKernel(size_t n)
{
  auto data = generateData<T>(n);
  std::vector<T> copy(data.begin(), data.end());
  stdex::stable_radix_sort<kLsdPar>(copy.begin(), copy.end());
  benchmark::DoNotOptimize(copy);
}

template <typename T>
void radixMsdParKernel(size_t n)
{
  auto data = generateData<T>(n);
  std::vector<T> copy(data.begin(), data.end());
  stdex::stable_radix_sort<kMsdPar>(copy.begin(), copy.end());
  benchmark::DoNotOptimize(copy);
}

#  if defined(_MSC_VER) && !defined(__clang__)
template <typename T>
void msvcParallelRadixsortKernel(size_t n)
{
  auto data = generateData<T>(n);
  std::vector<T> copy(data.begin(), data.end());
  Concurrency::parallel_radixsort(copy.begin(), copy.end());
  benchmark::DoNotOptimize(copy);
}
#  endif
#endif

template <typename T, void (*Kernel)(size_t)>
void benchN(benchmark::State &state)
{
  size_t const n = static_cast<size_t>(state.range(0));
  for (auto _ : state) {
    Kernel(n);
  }
  state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(n));
}

} // namespace

// Section 1: Sequential comparison.
// Baseline: std::sort. Compared: stable_sort, spreadsort, pdqsort,
// stdex radix LSD/Seq, MSD/Seq. (DoubleRadixSort omitted: no stdex equivalent.)
#define STDEX_BENCH_SEQ(TypeTag, T)                                                                                                       \
  BENCHMARK(benchN<T, stdSortKernel<T>>)->Name(#TypeTag "_StdSort")->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);          \
  BENCHMARK(benchN<T, stdStableSortKernel<T>>)->Name(#TypeTag "_StableSort")->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000); \
  BENCHMARK(benchN<T, spreadsortKernel<T>>)->Name(#TypeTag "_Spreadsort")->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);    \
  BENCHMARK(benchN<T, pdqsortKernel<T>>)->Name(#TypeTag "_Pdqsort")->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);          \
  BENCHMARK(benchN<T, radixLsdSeqKernel<T>>)->Name(#TypeTag "_LsdSeq")->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);       \
  BENCHMARK(benchN<T, radixMsdSeqKernel<T>>)->Name(#TypeTag "_MsdSeq")->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000)

STDEX_BENCH_SEQ(Uint64, uint64_t);
STDEX_BENCH_SEQ(Int64, int64_t);
STDEX_BENCH_SEQ(Double, double);
STDEX_BENCH_SEQ(Int32, int32_t);
STDEX_BENCH_SEQ(Uint32, uint32_t);

#ifdef _OPENMP
// Section 2: Parallel comparison (requires OpenMP).
#  if defined(_MSC_VER) && !defined(__clang__)
#    define STDEX_BENCH_PAR(TypeTag, T)                                                                                                             \
      BENCHMARK(benchN<T, stdSortParKernel<T>>)->Name(#TypeTag "_StdSortPar")->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);          \
      BENCHMARK(benchN<T, stdStableSortParKernel<T>>)->Name(#TypeTag "_StableSortPar")->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000); \
      BENCHMARK(benchN<T, radixLsdParKernel<T>>)->Name(#TypeTag "_LsdPar")->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);             \
      BENCHMARK(benchN<T, radixMsdParKernel<T>>)->Name(#TypeTag "_MsdPar")->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000)
#    define STDEX_BENCH_PAR_INT(TypeTag, T) \
      STDEX_BENCH_PAR(TypeTag, T);          \
      BENCHMARK(benchN<T, msvcParallelRadixsortKernel<T>>)->Name(#TypeTag "_MsvcRadix")->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000)
STDEX_BENCH_PAR_INT(Uint64, uint64_t);
STDEX_BENCH_PAR_INT(Int64, int64_t);
STDEX_BENCH_PAR(Double, double);
STDEX_BENCH_PAR_INT(Int32, int32_t);
STDEX_BENCH_PAR_INT(Uint32, uint32_t);
#  else
#    define STDEX_BENCH_PAR(TypeTag, T)                                                                                                 \
      BENCHMARK(benchN<T, radixLsdParKernel<T>>)->Name(#TypeTag "_LsdPar")->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000); \
      BENCHMARK(benchN<T, radixMsdParKernel<T>>)->Name(#TypeTag "_MsdPar")->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000)
#    if defined(__cpp_lib_parallel_algorithm)
#      define STDEX_BENCH_PAR_FULL(TypeTag, T)                                                                                                        \
        BENCHMARK(benchN<T, stdSortParKernel<T>>)->Name(#TypeTag "_StdSortPar")->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);          \
        BENCHMARK(benchN<T, stdStableSortParKernel<T>>)->Name(#TypeTag "_StableSortPar")->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000); \
        STDEX_BENCH_PAR(TypeTag, T)
STDEX_BENCH_PAR_FULL(Uint64, uint64_t);
STDEX_BENCH_PAR_FULL(Int64, int64_t);
STDEX_BENCH_PAR_FULL(Double, double);
STDEX_BENCH_PAR_FULL(Int32, int32_t);
STDEX_BENCH_PAR_FULL(Uint32, uint32_t);
#    else
STDEX_BENCH_PAR(Uint64, uint64_t);
STDEX_BENCH_PAR(Int64, int64_t);
STDEX_BENCH_PAR(Double, double);
STDEX_BENCH_PAR(Int32, int32_t);
STDEX_BENCH_PAR(Uint32, uint32_t);
#    endif
#  endif
#endif

BENCHMARK_MAIN();
