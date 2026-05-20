include(FetchContent)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.8.3
)
FetchContent_MakeAvailable(benchmark)

add_executable(lob_bench
    bench_order_book.cpp
    bench_spsc_ring.cpp)
target_link_libraries(lob_bench PRIVATE lob_core benchmark::benchmark)
