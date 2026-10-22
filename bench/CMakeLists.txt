include(FetchContent)
FetchContent_Declare(
  googlebenchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.9.1
  GIT_SHALLOW TRUE
  SYSTEM
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
set(OME_SAVED_CLANG_TIDY "${CMAKE_CXX_CLANG_TIDY}")
set(CMAKE_CXX_CLANG_TIDY "")
FetchContent_MakeAvailable(googlebenchmark)
set(CMAKE_CXX_CLANG_TIDY "${OME_SAVED_CLANG_TIDY}")
# Google Benchmark builds itself with -pedantic-errors, and Clang 23 reports its use of
# __COUNTER__ as a (C2y) extension. That's upstream code, so silence just that warning.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-Wno-c2y-extensions OME_HAS_WNO_C2Y_EXTENSIONS)
if(OME_HAS_WNO_C2Y_EXTENSIONS)
  target_compile_options(benchmark PRIVATE -Wno-c2y-extensions)
  target_compile_options(benchmark_main PRIVATE -Wno-c2y-extensions)
endif()

add_library(ome_workload STATIC workload.cpp)
target_include_directories(ome_workload PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ome_workload PUBLIC ome PRIVATE ome_warnings)

# Throughput (Google Benchmark).
add_executable(ome-bench book_bench.cpp)
target_link_libraries(ome-bench PRIVATE ome_workload ome_warnings benchmark::benchmark)

# Per-request latency percentiles.
add_executable(ome-latency latency.cpp)
target_link_libraries(ome-latency PRIVATE ome_workload ome_warnings)

# Replays an order file in the original input format (comparison with the original program).
add_executable(ome-replay replay.cpp)
target_link_libraries(ome-replay PRIVATE ome ome_warnings)
