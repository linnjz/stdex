cmake_minimum_required(VERSION 3.30)

set(STDEX_SCAFFOLD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmake-scaffold"
    CACHE PATH "Path to the cmake-scaffold submodule")
get_filename_component(STDEX_SCAFFOLD_ROOT "${STDEX_SCAFFOLD_ROOT}" ABSOLUTE)
set(STDEX_SCAFFOLD_CMAKE_DIR "${STDEX_SCAFFOLD_ROOT}/template/cmake")

if(NOT EXISTS "${STDEX_SCAFFOLD_CMAKE_DIR}/CompilerOptions.cmake")
    message(FATAL_ERROR
        "cmake-scaffold submodule is missing or incomplete.\n"
        "Run: git submodule update --init --recursive")
endif()

list(PREPEND CMAKE_MODULE_PATH "${STDEX_SCAFFOLD_CMAKE_DIR}")

include(ImportStd)

set(CMAKE_C_STANDARD 23 CACHE STRING "C language standard")
set(CMAKE_CXX_STANDARD 23 CACHE STRING "C++ language standard")
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(stdex
    VERSION 1.0.0
    DESCRIPTION "Header-only C++ extension library"
    LANGUAGES C CXX
)

include(CTest)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

include(CompilerId)
include(Platform)
include(ShowIncludesPrefix)
include(OutputLayout)
include(TargetUtils)
include(CompilerOptions)

if(NOT TARGET stdex)
    add_library(stdex INTERFACE)
    target_include_directories(stdex INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_compile_features(stdex INTERFACE cxx_std_23)
endif()

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/CMakeLists.txt")
    add_subdirectory(benchmarks)
endif()
