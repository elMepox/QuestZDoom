# Builds the handful of UZDoom build-time tools (re2c, lemon, zipdir) for the *host*.
# UZDoom's own CMake expects them as an "ImportExecutables.cmake" export when cross-compiling
# (see IMPORT_EXECUTABLES in external/UZDoom/CMakeLists.txt). uzdoom_hook.cmake drives this project.
cmake_minimum_required(VERSION 3.16)
project(DoomXRHostTools C CXX)

if(NOT UZDOOM_DIR)
  message(FATAL_ERROR "pass -DUZDOOM_DIR=<path to external/UZDoom>")
endif()

set(CMAKE_BUILD_TYPE Release)
set(BUILD_SHARED_LIBS OFF)
find_package(Threads REQUIRED)

# Same helper UZDoom's top-level CMakeLists defines for its libraries.
macro(make_release_only)
endmacro()

add_subdirectory(${UZDOOM_DIR}/libraries/miniz miniz)
add_subdirectory(${UZDOOM_DIR}/libraries/bzip2 bzip2)
add_subdirectory(${UZDOOM_DIR}/libraries/lzma lzma)

set(BZIP2_INCLUDE_DIR "${UZDOOM_DIR}/libraries/bzip2")
set(BZIP2_LIBRARIES bz2)
set(LZMA_INCLUDE_DIR "${UZDOOM_DIR}/libraries/lzma/C")

add_subdirectory(${UZDOOM_DIR}/tools tools)

export(TARGETS re2c lemon zipdir FILE "${CMAKE_BINARY_DIR}/ImportExecutables.cmake")
