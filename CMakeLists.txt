cmake_minimum_required(VERSION 3.20)
project(model_renderer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Single-config generators start with no build type, which for this project
# means an unoptimised build that crawls on dense meshes.
get_property(_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _multi_config AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
               Debug Release RelWithDebInfo MinSizeRel)
endif()

include(cmake/CPM.cmake)

# ---------------------------------------------------------------- packages
#
# All three are fetched and built from source by CPM, so a fresh clone needs
# nothing installed but a compiler and CMake. Set CPM_SOURCE_CACHE to a
# directory to share the downloads between build trees and checkouts.

CPMAddPackage(
  NAME glfw
  GITHUB_REPOSITORY glfw/glfw
  GIT_TAG 3.4
  OPTIONS
    "GLFW_BUILD_EXAMPLES OFF"
    "GLFW_BUILD_TESTS OFF"
    "GLFW_BUILD_DOCS OFF"
    "GLFW_INSTALL OFF"
)

# The importers are the bulk of the configure-and-build time here. Exporters,
# tools and tests are all off: this program only ever reads.
CPMAddPackage(
  NAME assimp
  GITHUB_REPOSITORY assimp/assimp
  GIT_TAG v6.0.5
  OPTIONS
    "BUILD_SHARED_LIBS OFF"
    "ASSIMP_BUILD_TESTS OFF"
    "ASSIMP_BUILD_ASSIMP_TOOLS OFF"
    "ASSIMP_BUILD_SAMPLES OFF"
    "ASSIMP_INSTALL OFF"
    "ASSIMP_NO_EXPORT ON"
    "ASSIMP_WARNINGS_AS_ERRORS OFF"
    "ASSIMP_BUILD_DRACO OFF"
)

# Header-only, and with no CMake of its own, so it is downloaded and wrapped.
CPMAddPackage(
  NAME stb
  GITHUB_REPOSITORY nothings/stb
  GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
  DOWNLOAD_ONLY YES
)
add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE "${stb_SOURCE_DIR}")

# ------------------------------------------------------------------ target

add_executable(model-renderer
  src/main.cpp
  src/renderer.cpp
  src/model.cpp
  src/animation.cpp
  src/camera.cpp
  src/options.cpp
  src/gl33.c
  src/stb_impl.c
)

target_link_libraries(model-renderer PRIVATE glfw assimp stb)

# GL entry points are resolved at run time through glfwGetProcAddress, so
# there is no GL library to link and no loader dependency to install.
if(UNIX AND NOT APPLE)
  target_link_libraries(model-renderer PRIVATE ${CMAKE_DL_LIBS})
endif()

if(MSVC)
  target_compile_options(model-renderer PRIVATE /W4 /utf-8 /permissive-)
  # fopen and friends are used as the standard specifies them; MSVC's
  # preferred replacements are not portable.
  target_compile_definitions(model-renderer PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(model-renderer PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Keeps the binary next to the assets a user is likely to drag onto it, and
# out of the per-config subdirectories that multi-config generators use.
set_target_properties(model-renderer PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin"
)

if(MSVC)
  set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
               PROPERTY VS_STARTUP_PROJECT model-renderer)
endif()
