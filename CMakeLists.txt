cmake_minimum_required(VERSION 3.20)
project(lob LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(LOB_BUILD_TESTS "Build unit tests" ON)
option(LOB_BUILD_BENCH "Build microbenchmarks" ON)
option(LOB_BUILD_REPLAY "Build ITCH replay harness" ON)
option(LOB_NATIVE_ARCH "Compile with -march=native" ON)
option(LOB_LTO "Enable link-time optimization" ON)

add_library(lob_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(lob_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
        -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual
        -Wconversion -Wsign-conversion -Wnull-dereference -Wdouble-promotion
        -Wformat=2 -Wno-unused-parameter)
endif()

add_library(lob_release_flags INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(lob_release_flags INTERFACE
        $<$<CONFIG:Release>:-O3 -fno-omit-frame-pointer -fno-plt>)
    if(LOB_NATIVE_ARCH)
        target_compile_options(lob_release_flags INTERFACE
            $<$<CONFIG:Release>:-march=native -mtune=native>)
    endif()
    if(LOB_LTO)
        target_compile_options(lob_release_flags INTERFACE
            $<$<CONFIG:Release>:-flto=auto>)
        target_link_options(lob_release_flags INTERFACE
            $<$<CONFIG:Release>:-flto=auto>)
    endif()
elseif(MSVC)
    target_compile_options(lob_release_flags INTERFACE
        $<$<CONFIG:Release>:/O2 /Oi /GL>)
endif()

# Core matching-engine library. The matching engine itself lives entirely in
# headers (it is templated on the trade sink so LTO/inlining can collapse the
# hot path). Only the ITCH parser and a small instantiation TU sit here as
# real translation units.
add_library(lob_core
    src/lob_instantiate.cpp
    src/itch_parser.cpp)
target_include_directories(lob_core PUBLIC include)
target_link_libraries(lob_core PUBLIC lob_warnings lob_release_flags)

if(LOB_BUILD_REPLAY)
    add_executable(lob_replay src/replay.cpp)
    target_link_libraries(lob_replay PRIVATE lob_core)
endif()

if(LOB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(LOB_BUILD_BENCH)
    add_subdirectory(bench)
endif()
