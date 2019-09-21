# this code was lifted from https://github.com/ulfjack/ryu, Boost licensed

cmake_minimum_required(VERSION 3.0)
include_directories(${CMAKE_CURRENT_LIST_DIR})
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
add_library(ryu ryu/generic_128.c ryu/f2s.c ryu/d2fixed.c ryu/d2s.c)

target_include_directories(
    ryu PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
