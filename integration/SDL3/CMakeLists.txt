# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.18)
project(SDL3PS5 C)
option(PS5_SDL3_HOST_TEST "Run real SDL3 with a host EGL contract double" OFF)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_PTHREADS ON CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Video and events only. The joystick subsystem stays on with SDL's own backend
# selection; a PS5 pad backend is a later milestone.
foreach(feature AUDIO GPU RENDER CAMERA HAPTIC HIDAPI POWER SENSOR DIALOG TRAY
                OPENGL OPENGLES VULKAN OFFSCREEN DUMMYVIDEO X11 WAYLAND KMSDRM
                DBUS IBUS LIBUDEV LIBURING SYSTEM_ICONV
                TEST_LIBRARY TESTS EXAMPLES)
    set(SDL_${feature} OFF CACHE BOOL "" FORCE)
endforeach()
if(PS5_SDL3_HOST_TEST)
    # No window system is wanted on the host; the driver under test is the only one used.
    set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "" FORCE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
else()
    message(FATAL_ERROR "Only the host contract build exists so far")
endif()
add_subdirectory("${SDL_SOURCE}" sdl)

# Use SDL's own video-device extension point. SDL_VIDEO_OPENGL enables SDL's GL
# attribute handling; SDL's EGL layer (SDL_VIDEO_OPENGL_EGL) stays off.
target_sources(SDL3-static PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/SDL_ps5video.c")
target_compile_definitions(SDL3-static PRIVATE SDL_VIDEO_DRIVER_PS5=1 SDL_VIDEO_OPENGL=1)
# Only the driver sees the SDK's EGL/GL headers; SDL keeps its bundled ones.
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/SDL_ps5video.c"
    TARGET_DIRECTORY SDL3-static
    PROPERTIES INCLUDE_DIRECTORIES "${PS5_OPENGL_PREFIX}/include")
add_library(sdl3-ps5-example OBJECT example.c)
target_link_libraries(sdl3-ps5-example PRIVATE SDL3::SDL3-static)
target_include_directories(sdl3-ps5-example PRIVATE "${PS5_OPENGL_PREFIX}/include")
target_compile_options(sdl3-ps5-example PRIVATE -Wall -Wextra -Werror)
if(PS5_SDL3_HOST_TEST)
    # Keep the mock outside the consumer and driver: no SDL API is mocked.
    add_executable(sdl3-ps5-contract test_contract.c $<TARGET_OBJECTS:sdl3-ps5-example>)
    target_compile_definitions(sdl3-ps5-example PRIVATE main=sdl3_ps5_example_main)
    target_compile_options(sdl3-ps5-contract PRIVATE -UNDEBUG -Wall -Wextra -Werror)
    target_include_directories(sdl3-ps5-contract PRIVATE "${PS5_OPENGL_PREFIX}/include")
    target_link_libraries(sdl3-ps5-contract PRIVATE SDL3::SDL3-static)
    enable_testing()
    add_test(NAME sdl3-ps5-contract COMMAND sdl3-ps5-contract)
endif()
