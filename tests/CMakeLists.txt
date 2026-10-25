include(TestSupport)

function(stdex_add_test target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "INCLUDES;LIBS;ARGS")

    if(NOT ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "stdex_add_test(${target}): at least one source is required")
    endif()

    scaffold_add_test(${target}
        SOURCES ${ARG_UNPARSED_ARGUMENTS}
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

stdex_add_test(test_inline_storage test_inline_storage.cpp)

stdex_add_test(test_inlined_bitset test_inlined_bitset.cpp)

# LinnSingleton lives outside include/ and its cross-TU / short-macro checks
# need several translation units.
find_package(Threads REQUIRED)
stdex_add_test(test_singleton
    macro/test_singleton.cpp
    macro/test_singleton_short_macros.cpp
    macro/test_singleton_cross_tu_a.cpp
    macro/test_singleton_cross_tu_b.cpp
    INCLUDES "${CMAKE_SOURCE_DIR}/marco"
    LIBS Threads::Threads
)

option(STDEX_ENABLE_QT "Enable Qt-dependent tests (test_property)" OFF)
if(STDEX_ENABLE_QT)
    set(CMAKE_AUTOMOC ON)
    list(APPEND CMAKE_PREFIX_PATH "E:/Qt/6.6.2/msvc2019_64") # Set your Qt library path

    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)

    stdex_add_test(test_property
        macro/test_property.cpp
        INCLUDES "${CMAKE_SOURCE_DIR}/marco"
        LIBS Qt${QT_VERSION_MAJOR}::Core
    )
else()
    stdex_add_test(test_property
        macro/test_property.cpp
        INCLUDES "${CMAKE_SOURCE_DIR}/marco"
    )
endif()