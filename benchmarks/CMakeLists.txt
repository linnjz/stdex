include(BenchmarkSupport)

# Prefer vcpkg, but also accept a normal CMake installation found through
# CMAKE_PREFIX_PATH so the benchmarks are not silently dropped on systems
# without vcpkg.
set(_stdex_benchmark_found FALSE)
scaffold_find_vcpkg_benchmark(_stdex_benchmark_found)
if(NOT _stdex_benchmark_found)
    find_package(benchmark CONFIG QUIET)
    if(TARGET benchmark::benchmark AND TARGET benchmark::benchmark_main)
        set(_stdex_benchmark_found TRUE)
    endif()
endif()
if(NOT _stdex_benchmark_found)
    message(WARNING
        "Skipping stdex benchmarks: Google Benchmark (targets "
        "benchmark::benchmark and benchmark::benchmark_main) was not found. "
        "Set VCPKG_ROOT or CMAKE_PREFIX_PATH to a benchmark installation.")
    return()
endif()

# Vcpkg.cmake defaults VCPKG_ROOT even when that tree does not exist, so probe
# the actual triplet directories before calling FindVcpkgPackage().
set(_stdex_have_vcpkg_tree FALSE)
if(VCPKG_ROOT AND (EXISTS "${VCPKG_STATIC_PATH}" OR EXISTS "${VCPKG_DYNAMIC_PATH}"))
    set(_stdex_have_vcpkg_tree TRUE)
endif()
if(_stdex_have_vcpkg_tree)
    FindVcpkgPackage(boost_unordered LINKAGE STATIC CONFIG QUIET)
    FindVcpkgPackage(absl LINKAGE STATIC CONFIG QUIET)
endif()
if(NOT TARGET Boost::unordered)
    find_package(boost_unordered CONFIG QUIET)
endif()
if(NOT TARGET absl::flat_hash_map)
    find_package(absl CONFIG QUIET)
endif()

# bench_hash_table needs both comparison libraries; the other benchmarks do
# not, so a missing Boost/Abseil only skips that one target.
if(NOT TARGET Boost::unordered OR NOT TARGET absl::flat_hash_map)
    message(WARNING
        "Skipping bench_hash_table: Boost.Unordered or Abseil was not found.")
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

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target} PRIVATE "-Wno-error=#warnings")
    endif()
endfunction()

# bench_stable_radix_sort.cpp provides its own BENCHMARK_MAIN().
stdex_add_bench(bench_stable_radix_sort bench_stable_radix_sort.cpp
    OWN_MAIN
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT COMPILER_MSVC_LIKE)
    # Clang modules can bake _PSTL_PAR_BACKEND_TBB from a sibling target that
    # sees Boost's bundled TBB headers, while this target links no TBB.
    target_compile_definitions(bench_stable_radix_sort PRIVATE _GLIBCXX_USE_TBB_PAR_BACKEND=0)
endif()

if(TARGET Boost::unordered AND TARGET absl::flat_hash_map)
    stdex_add_bench(bench_hash_table bench_hash_table.cpp
        INCLUDES "${CMAKE_SOURCE_DIR}/include/unordered_flat_detail"
        LIBS Boost::unordered absl::flat_hash_map
    )
endif()

# int128 always compares against the MSVC-style two-word baseline; native
# __int128 and absl::uint128 are added automatically when the target exists.
set(_bench_int128_libs "")
if(TARGET absl::int128)
    list(APPEND _bench_int128_libs absl::int128)
endif()
stdex_add_bench(bench_int128 bench_int128.cpp LIBS ${_bench_int128_libs})
unset(_bench_int128_libs)

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
