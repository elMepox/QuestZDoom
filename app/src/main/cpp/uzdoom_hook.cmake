# Injected into the UZDoom CMake project via -DCMAKE_PROJECT_UZDoom_INCLUDE=... (runs right after
# its project() call). This lets us build UZDoom *unmodified* (apart from patches/uzdoom) while
# supplying what an Android cross build needs and the upstream project cannot know about:
#   1. SDL2 (built from the external/SDL submodule) instead of a system SDL2
#   2. host-built build tools (re2c, lemon, zipdir)
#   3. a target that exports engine data (pk3s, soundfonts) into the APK assets
if(NOT ANDROID)
  message(FATAL_ERROR "uzdoom_hook.cmake is only meant for Android builds")
endif()

set(_uz "${CMAKE_CURRENT_SOURCE_DIR}")

# --- 0. compiler cache -----------------------------------------------------------------------
# Use ccache when it is installed so that a rebuild after a CMake re-configure or `clean` does not
# recompile the whole engine. Opt out with -DDOOMXR_NO_CCACHE=ON.
if(NOT DOOMXR_NO_CCACHE)
  find_program(DOOMXR_CCACHE ccache)
  if(DOOMXR_CCACHE)
    message(STATUS "Using ccache: ${DOOMXR_CCACHE}")
    set(CMAKE_C_COMPILER_LAUNCHER "${DOOMXR_CCACHE}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${DOOMXR_CCACHE}")
    # ccache cannot reuse results for translation units that use a precompiled header ("Could not use
    # precompiled header"), which is nearly all of the engine. Compile the headers normally instead.
    set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)
  endif()
endif()

# --- 1. SDL2 ---------------------------------------------------------------------------------
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
add_subdirectory("${DOOMXR_SDL_DIR}" "${CMAKE_BINARY_DIR}/sdl2")

# UZDoom does find_package(SDL2) via its own cmake/FindSDL2.cmake, which checks the cache first.
# Headers are included as <SDL2/SDL.h>, so expose the SDL include dir under an "SDL2" name.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/sdl2-include")
file(CREATE_LINK "${DOOMXR_SDL_DIR}/include" "${CMAKE_BINARY_DIR}/sdl2-include/SDL2" SYMBOLIC)
set(SDL2_INCLUDE_DIR "${CMAKE_BINARY_DIR}/sdl2-include" CACHE PATH "" FORCE)
set(SDL2_LIBRARY_TEMP SDL2 CACHE STRING "" FORCE)   # link by target name

# --- 2. host tools -----------------------------------------------------------------------------
set(_host_bin "${CMAKE_BINARY_DIR}/host-tools")
if(NOT EXISTS "${_host_bin}/ImportExecutables.cmake")
  message(STATUS "Building host tools (re2c, lemon, zipdir)")
  execute_process(
    COMMAND ${CMAKE_COMMAND} -S "${DOOMXR_HOST_TOOLS_SRC}" -B "${_host_bin}" -G "Unix Makefiles"
            -DUZDOOM_DIR=${_uz} -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_C_COMPILER=cc -DCMAKE_CXX_COMPILER=c++
            -DCMAKE_C_COMPILER_LAUNCHER=${CMAKE_C_COMPILER_LAUNCHER}
            -DCMAKE_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER}
    RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "configuring host tools failed (${_rc})")
  endif()
  execute_process(
    COMMAND ${CMAKE_COMMAND} --build "${_host_bin}" --target re2c lemon zipdir --parallel
    RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "building host tools failed (${_rc})")
  endif()
endif()
set(IMPORT_EXECUTABLES "${_host_bin}/ImportExecutables.cmake" CACHE FILEPATH "" FORCE)

# --- 3. OpenAL Soft ---------------------------------------------------------------------------
# UZDoom dlopen()s "libopenal.so" on Android (patched), so build it as a plain-named shared lib
# and let DoomXRActivity System.loadLibrary("openal") before the engine starts.
if(DOOMXR_OPENAL_DIR)
  set(LIBTYPE SHARED CACHE STRING "" FORCE)
  foreach(_o ALSOFT_UTILS ALSOFT_EXAMPLES ALSOFT_INSTALL ALSOFT_INSTALL_CONFIG ALSOFT_INSTALL_HRTF_DATA
             ALSOFT_INSTALL_AMBDEC_PRESETS ALSOFT_INSTALL_EXAMPLES ALSOFT_INSTALL_UTILS ALSOFT_TESTS)
    set(${_o} OFF CACHE BOOL "" FORCE)
  endforeach()
  set(ALSOFT_BACKEND_OBOE OFF CACHE BOOL "" FORCE)
  set(ALSOFT_BACKEND_OPENSL ON CACHE BOOL "" FORCE)
  set(ALSOFT_REQUIRE_OPENSL ON CACHE BOOL "" FORCE)
  add_subdirectory("${DOOMXR_OPENAL_DIR}" "${CMAKE_BINARY_DIR}/openal")
  # Android only packages lib*.so, so drop the versioned soname (libopenal.so.1.x).
  set_target_properties(OpenAL PROPERTIES VERSION "" SOVERSION "")
endif()

# --- 4. OpenXR layer -------------------------------------------------------------------------------
# The Khronos loader comes from the openxr_loader_for_android AAR via prefab (see app/build.gradle.kts).
find_package(OpenXR REQUIRED CONFIG)
add_library(doomxr_xr STATIC "${CMAKE_CURRENT_LIST_DIR}/xr/xr_vr.cpp")
set_target_properties(doomxr_xr PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(doomxr_xr PUBLIC "${CMAKE_CURRENT_LIST_DIR}/xr")
target_include_directories(doomxr_xr PRIVATE "${CMAKE_BINARY_DIR}/sdl2-include")
target_link_libraries(doomxr_xr PUBLIC OpenXR::openxr_loader EGL GLESv3 log)

# --- 5. assets for the APK -------------------------------------------------------------------------
set(DOOMXR_HOOK_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "")
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _doomxr_finish)
function(_doomxr_finish)
  # zdoom uses the plain target_link_libraries signature, so must we.
  target_link_libraries(zdoom doomxr_xr)
  target_compile_definitions(zdoom PRIVATE DOOMXR_OPENXR=1)
  if(TARGET OpenAL)
    add_dependencies(zdoom OpenAL)
  endif()
  if(NOT DOOMXR_ASSET_DIR)
    return()
  endif()
  # Gradle only asks ninja for the shared libraries, so the asset copy must be a dependency of zdoom.
  # (It must not depend on zdoom itself: soundfonts/banks are taken straight from the source tree.)
  set(_pk3_targets)
  foreach(_t uzdoom_pk3 brightmaps_pk3 lights_pk3 game_support_pk3 game_widescreen_gfx_pk3)
    if(TARGET ${_t})
      list(APPEND _pk3_targets ${_t})
    endif()
  endforeach()
  add_custom_target(doomxr_assets
    COMMAND ${CMAKE_COMMAND} -DPK3_DIR=${ZDOOM_OUTPUT_DIR} -DUZDOOM_SRC=${CMAKE_SOURCE_DIR}
            -DDST=${DOOMXR_ASSET_DIR}/uzdoom -P ${DOOMXR_HOOK_DIR}/copy_assets.cmake
    DEPENDS ${_pk3_targets}
    VERBATIM)
  add_dependencies(zdoom doomxr_assets)
endfunction()
