cmake_minimum_required(VERSION 3.20)
project(remote-cat VERSION 0.2.0 LANGUAGES CXX)

# Help IDEs (clangd) by generating compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Avoid hardcoding SDK paths; let CMake/xcrun pick the SDK automatically
# if(APPLE)
#   set(CMAKE_OSX_SYSROOT "macosx") # usually omit entirely
# endif()

# Default build type for single-config generators
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Core library
add_library(remcat_core STATIC
  src/libremcat/source.cpp
  src/libremcat/rfc/tftp_codec.cpp
  src/libremcat/backends/tftp_source.cpp
)
target_compile_features(remcat_core PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(remcat_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(remcat_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_include_directories(remcat_core
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

add_library(remcat::core ALIAS remcat_core)


# Main CLI executable
add_executable(remcat src/cli/remcat_main.cpp)
target_link_libraries(remcat PRIVATE remcat_core)
