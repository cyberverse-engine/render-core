cmake_minimum_required(VERSION 4.0.0)
project(cyberverse_render_core CXX)

option(CYBERVERSE_RENDER_CORE_BUILD_TESTS "Build unit test cases" ON)
option(CYBERVERSE_RENDER_CORE_BUILD_BENCHMARKS "Build benchmarks" ON)
option(CYBERVERSE_RENDER_CORE_WITH_DRSN "Build pipeline_manager DRSN support" ON)
option(CYBERVERSE_RENDER_CORE_WITH_GPU_UPLOAD_MANAGER "Use Diligent GPUUploadManager" ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# DiligentEngine's Apple platform layer has Objective-C++ sources.
if(APPLE)
  enable_language(OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 23)
  set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

# Import engine dependencies
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(engine_dep_diligent)
include(engine_dep_geometry)
include(engine_dep_platform)
include(engine_dep_runtime)
resolve_engine_dep_geometry()
resolve_engine_dep_platform()
resolve_engine_dep_diligent()
resolve_engine_dep_runtime()

add_subdirectory(src)

if(CYBERVERSE_RENDER_CORE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

if(CYBERVERSE_RENDER_CORE_BUILD_BENCHMARKS)
  add_subdirectory(benchmark)
endif()
