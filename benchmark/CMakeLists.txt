cmake_minimum_required(VERSION 4.0.0)

set(BENCHMARK_ENABLE_TESTING OFF)

include(FetchContent)

FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark
  GIT_TAG v1.9.5
  GIT_SHALLOW ON
)
FetchContent_MakeAvailable(benchmark)

add_executable(cyberverse_render_core_benchmarks render_core_benchmarks.cpp)

target_link_libraries(
  cyberverse_render_core_benchmarks
  PRIVATE
  cyberverse::render_core::static_lib
  benchmark::benchmark
)
