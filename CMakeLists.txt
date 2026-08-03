cmake_minimum_required(VERSION 3.24)
project(RostoNatural VERSION 0.11.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ROSTO_BUILD_TESTS "Build deterministic core tests" ON)
option(ROSTO_BUILD_ADOBE_PLUGIN "Build the Premiere/After Effects effect adapter" OFF)
option(ROSTO_WITH_MEDIAPIPE "Build MediaPipe Face Landmarker adapter" OFF)
set(ROSTO_MEDIAPIPE_BRIDGE_LIBRARY "" CACHE FILEPATH
    "Path to the Bazel-built libRostoMediaPipe.dylib")

add_library(rosto_core STATIC
  core/src/cache.cpp
  core/src/flyaway.cpp
  core/src/retouch.cpp
  core/src/temporal.cpp
)
set_target_properties(rosto_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(rosto_core PUBLIC core/include)
target_compile_options(rosto_core PRIVATE -Wall -Wextra -Wpedantic)

if(ROSTO_WITH_MEDIAPIPE)
  if(NOT EXISTS "${ROSTO_MEDIAPIPE_BRIDGE_LIBRARY}")
    message(FATAL_ERROR
      "ROSTO_WITH_MEDIAPIPE requires ROSTO_MEDIAPIPE_BRIDGE_LIBRARY. "
      "Run scripts/prepare_mediapipe.sh first.")
  endif()
  add_library(rosto_mediapipe_bridge SHARED IMPORTED GLOBAL)
  set_target_properties(rosto_mediapipe_bridge PROPERTIES
    IMPORTED_LOCATION "${ROSTO_MEDIAPIPE_BRIDGE_LIBRARY}"
  )
  target_sources(rosto_core PRIVATE core/src/mediapipe_detector.cpp)
  target_compile_definitions(rosto_core PUBLIC ROSTO_WITH_MEDIAPIPE=1)
  target_include_directories(rosto_core PRIVATE mediapipe_bridge)
  target_link_libraries(rosto_core PUBLIC rosto_mediapipe_bridge)
endif()

add_executable(rosto_cli tools/rosto_cli.cpp)
target_link_libraries(rosto_cli PRIVATE rosto_core)

add_executable(rosto_benchmark tools/benchmark.cpp)
target_link_libraries(rosto_benchmark PRIVATE rosto_core)

add_executable(rosto_flyaway_analysis tools/flyaway_analysis_cli.cpp)
target_link_libraries(rosto_flyaway_analysis PRIVATE rosto_core)

add_executable(rosto_flyaway_render tools/flyaway_render_cli.cpp)
target_link_libraries(rosto_flyaway_render PRIVATE rosto_core)

if(ROSTO_BUILD_TESTS)
  enable_testing()
  add_executable(rosto_tests tests/core_tests.cpp)
  target_link_libraries(rosto_tests PRIVATE rosto_core)
  add_test(NAME rosto_core_tests COMMAND rosto_tests)

  if(NOT ROSTO_WITH_MEDIAPIPE)
    add_executable(rosto_mediapipe_adapter_tests
      core/src/mediapipe_detector.cpp
      tests/mediapipe_adapter_test.cpp
    )
    target_compile_definitions(
      rosto_mediapipe_adapter_tests PRIVATE ROSTO_WITH_MEDIAPIPE=1)
    target_include_directories(rosto_mediapipe_adapter_tests PRIVATE
      core/include
      mediapipe_bridge
    )
    add_test(
      NAME rosto_mediapipe_adapter_tests
      COMMAND rosto_mediapipe_adapter_tests
    )
  endif()
endif()

if(ROSTO_BUILD_ADOBE_PLUGIN)
  if(NOT DEFINED AE_SDK_ROOT)
    message(FATAL_ERROR "Set -DAE_SDK_ROOT=/path/to/AfterEffectsSDK")
  endif()
  add_subdirectory(adobe)
endif()
