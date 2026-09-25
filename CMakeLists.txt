cmake_minimum_required(VERSION 3.20)
project(YoshisIslandRecomp C)

set(CMAKE_C_STANDARD 11)

# snesrecomp runner framework
set(SNESRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/snesrecomp")
include(${SNESRECOMP_ROOT}/runner/runner.cmake)

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

# Locate SDL2
set(YI_SDL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/SDL2")
set(SDL2_INCLUDE_DIRS "${YI_SDL_ROOT}/include" "${YI_SDL_ROOT}/include/SDL2")
set(SDL2_LIBRARIES "${YI_SDL_ROOT}/lib/SDL2.lib")

add_executable(yoshis_island_recomp src/desktop.c src/game_rtl.c ${SNESRECOMP_RUNNER_SOURCES})
target_include_directories(yoshis_island_recomp PRIVATE
  ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
  ${SNESRECOMP_ROOT}/runner/src/desktop
  ${CMAKE_CURRENT_SOURCE_DIR}/config
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${SDL2_INCLUDE_DIRS})

target_compile_definitions(yoshis_island_recomp PRIVATE SDL_MAIN_HANDLED SNESRECOMP_TRACE=0 SNESRECOMP_ENABLE_MODS=0)
if(NOT MSVC)
  target_compile_options(yoshis_island_recomp PRIVATE -w -Wno-implicit-function-declaration -ffunction-sections -fdata-sections)
  target_link_options(yoshis_island_recomp PRIVATE -Wl,--gc-sections)
endif()

target_link_libraries(yoshis_island_recomp PRIVATE snesrecomp_game ${SDL2_LIBRARIES} ${SNESRECOMP_RUNNER_LIBRARIES} m)

# Diagnostic Headless Target (yi_headless)
add_executable(yi_headless src/headless.c src/game_rtl.c ${SNESRECOMP_RUNNER_SOURCES})
target_include_directories(yi_headless PRIVATE
  ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
  ${SNESRECOMP_ROOT}/runner/src/desktop
  ${CMAKE_CURRENT_SOURCE_DIR}/config
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${SDL2_INCLUDE_DIRS})

target_compile_definitions(yi_headless PRIVATE SDL_MAIN_HANDLED SNESRECOMP_TRACE=0 SNESRECOMP_ENABLE_MODS=0)
if(NOT MSVC)
  target_compile_options(yi_headless PRIVATE -w -Wno-implicit-function-declaration -ffunction-sections -fdata-sections)
  target_link_options(yi_headless PRIVATE -Wl,--gc-sections)
endif()
target_link_libraries(yi_headless PRIVATE snesrecomp_game ${SDL2_LIBRARIES} ${SNESRECOMP_RUNNER_LIBRARIES} m)

if(WIN32 AND EXISTS "${YI_SDL_ROOT}/bin/SDL2.dll")
  add_custom_command(TARGET yoshis_island_recomp POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${YI_SDL_ROOT}/bin/SDL2.dll" "$<TARGET_FILE_DIR:yoshis_island_recomp>")
endif()
