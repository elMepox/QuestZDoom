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

# --- 3. assets for the APK -------------------------------------------------------------------------
set(DOOMXR_HOOK_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "")
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _doomxr_add_asset_target)
function(_doomxr_add_asset_target)
  if(NOT DOOMXR_ASSET_DIR)
    return()
  endif()
  # zdoom's POST_BUILD step drops soundfonts/ and fm_banks/ next to the pk3s in ZDOOM_RESOURCE_DIR.
  add_custom_target(doomxr_assets ALL
    COMMAND ${CMAKE_COMMAND} -DSRC=${ZDOOM_RESOURCE_DIR} -DDST=${DOOMXR_ASSET_DIR}/uzdoom
            -P ${DOOMXR_HOOK_DIR}/copy_assets.cmake
    DEPENDS zdoom
    VERBATIM)
endfunction()
