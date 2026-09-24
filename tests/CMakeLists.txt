include(TestSupport)

function(stdex_add_test target source)
    cmake_parse_arguments(PARSE_ARGV 2 ARG "" "" "INCLUDES;LIBS;ARGS")

    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "stdex_add_test(${target}): unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()

    scaffold_add_test(${target}
        SOURCES "${source}"
        INCLUDES "${CMAKE_SOURCE_DIR}/include" ${ARG_INCLUDES}
        LIBS stdex ${ARG_LIBS}
        ARGS ${ARG_ARGS}
    )
endfunction()

stdex_add_test(test_stable_radix_sort test_stable_radix_sort.cpp)
stdex_add_test(test_hybrid_alloc test_hybrid_alloc.cpp)

# Parked: field_variant.hpp needs C++26 reflection (<meta>, P2996), which the
# pinned toolchains (MSVC 19.51, clang++ 22) don't ship yet. Re-enable with a
# P2996-capable compiler (at_index dispatch cases already merged into
# test_field_variant.cpp, no separate target needed).
# stdex_add_test(test_field_variant test_field_variant.cpp)

# Detail dir: public headers and this TU include sibling detail headers by
# bare filename (upstream layout), so it must be on the include path.
stdex_add_test(test_hash_table test_hash_table.cpp
    INCLUDES "${CMAKE_SOURCE_DIR}/include/unordered_flat_detail"
)

# Extracted detail headers (bit_traits, byte_utils, int128) plus the fastmod
# port standing on them; include/ alone suffices, no extra include dirs.
stdex_add_test(test_fastmod test_fastmod.cpp)

stdex_add_test(test_uninitialized_algorithms test_uninitialized_algorithms.cpp)
