cmake_minimum_required(VERSION 4.0.0)

include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.2
  GIT_SHALLOW ON
)
FetchContent_MakeAvailable(Catch2)

add_executable(
  cyberverse_render_core_tests
  batch_loader_test.cpp
  error_test.cpp
  gpu_data_ref_test.cpp
  gpu_data_manager_test.cpp
  pipeline_manager_test.cpp
  render_packet_test.cpp
  resource_uri_test.cpp
  frame_context_test.cpp
  shader_key_test.cpp
  shader_manager_test.cpp
  render_phrase_test.cpp
  render_phrase_list_test.cpp
  stage_integration_test.cpp
  render_device_test.cpp
)

target_link_libraries(
  cyberverse_render_core_tests
  PRIVATE
  cyberverse::render_core::static_lib
  Catch2::Catch2WithMain
)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(cyberverse_render_core_tests PROPERTIES SKIP_RETURN_CODE 4)
