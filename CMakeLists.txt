cmake_minimum_required(VERSION 3.20)
project(remote-cat CXX)

if(APPLE)
  set(CMAKE_OSX_SYSROOT "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Core library
add_library(remcat_core STATIC src/libremcat/source.cpp)
target_include_directories(remcat_core PUBLIC src/libremcat/include)


# Main CLI executable
add_executable(remcat src/cli/remcat_main.cpp)
target_link_libraries(remcat PRIVATE remcat_core)
