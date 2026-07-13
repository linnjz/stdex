include(BenchmarkSupport)

scaffold_find_vcpkg_benchmark(_stdex_benchmark_found)
if(NOT _stdex_benchmark_found)
    message(WARNING
        "Skipping stdex benchmarks: vcpkg Google Benchmark (targets "
        "benchmark::benchmark and benchmark::benchmark_main) was not found. "
        "Set VCPKG_ROOT and install the 'benchmark' port.")
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
