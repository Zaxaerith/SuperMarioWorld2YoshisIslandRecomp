cmake_minimum_required(VERSION 3.20)
project(YoshisIslandRecomp C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build options
option(YI_BUILD_PLAY "Build interactive desktop player (SDL2)" ON)
option(YI_BUILD_HEADLESS "Build headless diagnostic test host" ON)
option(YI_BUILD_TESTS "Build launcher contract tests" ON)

# Force SDL2 backend for both snesrecomp and recomp-ui
set(SNESRECOMP_SDL_BACKEND "SDL2" CACHE STRING "Desktop SDL backend (SDL3 or SDL2)" FORCE)
set(RECOMP_UI_SDL3 OFF CACHE BOOL "Build recomp-ui against SDL3 instead of SDL2" FORCE)

# snesrecomp runner framework
set(SNESRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/snesrecomp")
include(${SNESRECOMP_ROOT}/runner/runner.cmake)

# recomp-ui launcher framework
set(RECOMP_UI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui" CACHE PATH "Root directory of the recomp-ui launcher repo")
include(${RECOMP_UI_ROOT}/recomp_ui.cmake)

# Recompiled Yoshi's Island game code (statically generated C translation units)
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/game/*.c")

add_library(snesrecomp_game STATIC ${GAME_SOURCES})
target_include_directories(snesrecomp_game PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/config"
  "${CMAKE_CURRENT_SOURCE_DIR}/snesrecomp/runner/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/snesrecomp/runner/src/snes")

if(NOT MSVC)
  target_compile_options(snesrecomp_game PRIVATE
    -w -Wno-implicit-function-declaration)
else()
  target_compile_options(snesrecomp_game PRIVATE /w)
endif()

# Platform-specific linker flags (Apple ld does not accept GNU linker options).
function(yi_set_link_options target)
  if(APPLE)
    target_link_options(${target} PRIVATE -Wl,-dead_strip)
  elseif(NOT MSVC)
    target_link_options(${target} PRIVATE -Wl,--gc-sections -static-libgcc -static-libstdc++)
  endif()
endfunction()

# Locate SDL2 for windowing, controller input and audio playback
if(YI_BUILD_PLAY OR YI_BUILD_HEADLESS OR YI_BUILD_TESTS)
  if(NOT DEFINED YI_SDL_ROOT AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/SDL2")
    set(YI_SDL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/SDL2" CACHE PATH "SDL2 root" FORCE)
  elseif(NOT DEFINED YI_SDL_ROOT AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../_sdl2/SDL2-2.32.8")
    set(YI_SDL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../_sdl2/SDL2-2.32.8" CACHE PATH "SDL2 root" FORCE)
  endif()

  find_package(SDL2 CONFIG QUIET)
  if(NOT SDL2_FOUND)
    find_package(SDL2 QUIET)
  endif()

  if(NOT TARGET SDL2::SDL2 AND NOT SDL2_FOUND)
    if(DEFINED YI_SDL_ROOT)
      set(SDL2_INCLUDE_DIRS "${YI_SDL_ROOT}/include" "${YI_SDL_ROOT}/include/SDL2")
      find_library(SDL2_LIBRARIES NAMES SDL2 PATHS "${YI_SDL_ROOT}/lib" NO_DEFAULT_PATH)
      if(NOT SDL2_LIBRARIES AND EXISTS "${YI_SDL_ROOT}/lib/SDL2.lib")
        set(SDL2_LIBRARIES "${YI_SDL_ROOT}/lib/SDL2.lib")
      endif()
    endif()
  endif()
endif()

# Interactive Player Target
if(YI_BUILD_PLAY)
  add_executable(yoshis_island_recomp src/desktop.c src/game_rtl.c ${SNESRECOMP_RUNNER_SOURCES})
  # GUI subsystem: no console window behind the SDL game window.
  set_target_properties(yoshis_island_recomp PROPERTIES
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME "YoshisIslandRecomp")
  target_include_directories(yoshis_island_recomp PRIVATE
    ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
    ${SNESRECOMP_ROOT}/runner/src/desktop
    ${CMAKE_CURRENT_SOURCE_DIR}/config
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

  if(TARGET SDL2::SDL2)
    target_link_libraries(yoshis_island_recomp PRIVATE SDL2::SDL2)
  else()
    target_include_directories(yoshis_island_recomp PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(yoshis_island_recomp PRIVATE ${SDL2_LIBRARIES})
  endif()

  target_compile_definitions(yoshis_island_recomp PRIVATE SDL_MAIN_HANDLED SNESRECOMP_TRACE=0 SNESRECOMP_ENABLE_MODS=0)
  if(NOT MSVC)
    target_compile_options(yoshis_island_recomp PRIVATE -w -Wno-implicit-function-declaration -ffunction-sections -fdata-sections)
  endif()
  yi_set_link_options(yoshis_island_recomp)
  target_link_libraries(yoshis_island_recomp PRIVATE snesrecomp_game ${SNESRECOMP_RUNNER_LIBRARIES} m)

  # recomp-ui native GUI launcher integration
  recomp_target_launcher_ui(yoshis_island_recomp CONSOLE snes)
  snesrecomp_rom_identity(yoshis_island_recomp "${CMAKE_CURRENT_SOURCE_DIR}/rom_identity.txt")
  snesrecomp_target_rom_cache(yoshis_island_recomp)

  # Stage default config and keybinds if missing
  add_custom_command(TARGET yoshis_island_recomp POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/config.ini
      -DDST=$<TARGET_FILE_DIR:yoshis_island_recomp>/config.ini
      -P ${SNESRECOMP_RUNNER_ROOT}/stage_if_missing.cmake
    COMMAND ${CMAKE_COMMAND}
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/keybinds.ini
      -DDST=$<TARGET_FILE_DIR:yoshis_island_recomp>/keybinds.ini
      -P ${SNESRECOMP_RUNNER_ROOT}/stage_if_missing.cmake
    VERBATIM)

  if(WIN32 AND DEFINED YI_SDL_ROOT AND EXISTS "${YI_SDL_ROOT}/bin/SDL2.dll")
    add_custom_command(TARGET yoshis_island_recomp POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${YI_SDL_ROOT}/bin/SDL2.dll" "$<TARGET_FILE_DIR:yoshis_island_recomp>")
  endif()
endif()

# Headless Diagnostic Target (yi_headless)
if(YI_BUILD_HEADLESS)
  add_executable(yi_headless src/headless.c src/game_rtl.c ${SNESRECOMP_RUNNER_SOURCES})
  target_include_directories(yi_headless PRIVATE
    ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
    ${SNESRECOMP_ROOT}/runner/src/desktop
    ${CMAKE_CURRENT_SOURCE_DIR}/config
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

  if(TARGET SDL2::SDL2)
    target_link_libraries(yi_headless PRIVATE SDL2::SDL2)
  else()
    target_include_directories(yi_headless PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(yi_headless PRIVATE ${SDL2_LIBRARIES})
  endif()

  target_compile_definitions(yi_headless PRIVATE SDL_MAIN_HANDLED SNESRECOMP_TRACE=0 SNESRECOMP_ENABLE_MODS=0)
  if(NOT MSVC)
    target_compile_options(yi_headless PRIVATE -w -Wno-implicit-function-declaration -ffunction-sections -fdata-sections)
  endif()
  yi_set_link_options(yi_headless)
  target_link_libraries(yi_headless PRIVATE snesrecomp_game ${SNESRECOMP_RUNNER_LIBRARIES} m)

  if(WIN32 AND DEFINED YI_SDL_ROOT AND EXISTS "${YI_SDL_ROOT}/bin/SDL2.dll")
    add_custom_command(TARGET yi_headless POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${YI_SDL_ROOT}/bin/SDL2.dll" "$<TARGET_FILE_DIR:yi_headless>")
  endif()
endif()

# Launcher Contract Test Target (launcher_contract)
if(YI_BUILD_TESTS)
  add_executable(launcher_contract tests/launcher_contract.c src/game_rtl.c ${SNESRECOMP_RUNNER_SOURCES})
  target_include_directories(launcher_contract PRIVATE
    ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
    ${SNESRECOMP_ROOT}/runner/src/desktop
    ${CMAKE_CURRENT_SOURCE_DIR}/config
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${RECOMP_UI_ROOT}/src
    ${RECOMP_UI_ROOT}/src/common)

  if(TARGET SDL2::SDL2)
    target_link_libraries(launcher_contract PRIVATE SDL2::SDL2)
  else()
    target_include_directories(launcher_contract PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(launcher_contract PRIVATE ${SDL2_LIBRARIES})
  endif()

  target_compile_definitions(launcher_contract PRIVATE SDL_MAIN_HANDLED SNESRECOMP_TRACE=0 SNESRECOMP_ENABLE_MODS=0)
  if(NOT MSVC)
    target_compile_options(launcher_contract PRIVATE -w -Wno-implicit-function-declaration -ffunction-sections -fdata-sections)
  endif()
  yi_set_link_options(launcher_contract)
  target_link_libraries(launcher_contract PRIVATE snesrecomp_game ${SNESRECOMP_RUNNER_LIBRARIES} m)

  recomp_target_launcher_ui(launcher_contract CONSOLE snes)

  if(WIN32 AND DEFINED YI_SDL_ROOT AND EXISTS "${YI_SDL_ROOT}/bin/SDL2.dll")
    add_custom_command(TARGET launcher_contract POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${YI_SDL_ROOT}/bin/SDL2.dll" "$<TARGET_FILE_DIR:launcher_contract>")
  endif()
endif()
