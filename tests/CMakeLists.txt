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
