cmake_minimum_required (VERSION 2.8.11)
project (FIX_UTF)
add_definitions(-std=c++11)
add_subdirectory("contrib/gtest-1.7.0")

include_directories("${CMAKE_SOURCE_DIR}/contrib/gtest-1.7.0/include")

add_library(fix_utf8 STATIC
    src/fix_utf8.cc)

add_executable(fix_utf8_test
    src/fix_utf8_test.cc)

target_link_libraries(fix_utf8_test fix_utf8 gtest_main) 

add_executable(benchmark
    src/benchmark.cc)

target_link_libraries(benchmark fix_utf8)
