include(BenchmarkSupport)

scaffold_find_vcpkg_benchmark(_stdex_benchmark_found)
if(NOT _stdex_benchmark_found)
    message(WARNING
        "Skipping stdex benchmarks: vcpkg Google Benchmark (targets "
        "benchmark::benchmark and benchmark::benchmark_main) was not found. "
        "Set VCPKG_ROOT and install the 'benchmark' port.")
    return()
endif()

FindVcpkgPackage(boost_unordered LINKAGE STATIC CONFIG QUIET)
FindVcpkgPackage(absl LINKAGE STATIC CONFIG QUIET)

if(NOT TARGET Boost::unordered OR NOT TARGET absl::flat_hash_map)
    message(WARNING
        "Skipping stdex benchmarks: Boost.Unordered or Abseil was not found.")
    return()
endif()

function(stdex_add_bench target source)
    cmake_parse_arguments(PARSE_ARGV 2 ARG "OWN_MAIN" "" "INCLUDES;LIBS")

    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "stdex_add_bench(${target}): unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()

    if(ARG_OWN_MAIN)
        add_executable(${target} "${source}")
        target_include_directories(${target} PRIVATE
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
            "${CMAKE_SOURCE_DIR}/include"
            ${ARG_INCLUDES}
        )
        target_link_libraries(${target} PRIVATE
            benchmark::benchmark
            stdex
            ${ARG_LIBS}
        )
        scaffold_configure_target(${target})
    else()
        scaffold_add_benchmark(${target}
            SOURCES "${source}"
            INCLUDES "${CMAKE_SOURCE_DIR}/include" ${ARG_INCLUDES}
            LIBS stdex ${ARG_LIBS}
        )
    endif()
endfunction()

# stable_radix_sort_bench.cpp provides its own BENCHMARK_MAIN().
stdex_add_bench(bench_stable_radix_sort stable_radix_sort_bench.cpp
    OWN_MAIN
)

stdex_add_bench(bench_hash_table bench_hash_table.cpp
    INCLUDES "${CMAKE_SOURCE_DIR}/include/unordered_flat_detail"
    LIBS Boost::unordered absl::flat_hash_map
)

# Optional third-party comparison for bench_inlined_bitset.
# Default OFF: no network access, nothing is fetched, and the benchmark only
# compares stdex::inlined_bitset against std::bitset. Turn ON to also compare
# against sul::dynamic_bitset; the header is fetched from GitHub into the
# build directory (never into the source tree) and enabled in the TU via
# STDEX_BENCH_HAS_DYNAMIC_BITSET.
option(STDEX_BENCH_DYNAMIC_BITSET
    "Fetch sul::dynamic_bitset from GitHub for bench_inlined_bitset heap comparison" OFF)

stdex_add_bench(bench_inlined_bitset bench_inlined_bitset.cpp)

if(STDEX_BENCH_DYNAMIC_BITSET)
    include(FetchContent)
    FetchContent_Declare(dynamic_bitset
        GIT_REPOSITORY https://github.com/pinam45/dynamic_bitset.git
        GIT_TAG master
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(dynamic_bitset)
    target_include_directories(bench_inlined_bitset PRIVATE
        ${dynamic_bitset_SOURCE_DIR}/include)
    target_compile_definitions(bench_inlined_bitset PRIVATE
        STDEX_BENCH_HAS_DYNAMIC_BITSET=1)
endif()
