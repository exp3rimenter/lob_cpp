cmake_minimum_required(VERSION 3.25)

project(
    limit_order_book
    VERSION 1.0.0
    DESCRIPTION "High-Performance Limit Order Book"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/CompilerOptions.cmake)
include(cmake/Dependencies.cmake)

add_subdirectory(src)
add_subdirectory(tools)

option(LOB_BUILD_TESTS "Build tests" ON)
option(LOB_BUILD_BENCH "Build benchmarks" ON)

if(LOB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(LOB_BUILD_BENCH)
    add_subdirectory(bench)
endif()
