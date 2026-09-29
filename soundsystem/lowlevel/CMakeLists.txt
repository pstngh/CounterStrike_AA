cmake_minimum_required(VERSION 3.16)
project(soundsystem_lowlevel)
set(SRCDIR "${CMAKE_SOURCE_DIR}")
set(CMAKE_MODULE_PATH ${SRCDIR}/cmake)
set(OUTLIBNAME "soundsystem_lowlevel")

include(${CMAKE_MODULE_PATH}/detect_platform.cmake)
include(${CMAKE_MODULE_PATH}/source_lib_base.cmake)

if( LINUXALL )
    target_compile_options(${OUTLIBNAME} PRIVATE -fpic -fno-semantic-interposition)
endif()

target_sources(${OUTLIBNAME} PRIVATE "device_sdl.cpp")
target_sources(${OUTLIBNAME} PRIVATE "device_null.cpp")
target_sources(${OUTLIBNAME} PRIVATE "mix.cpp")
target_sources(${OUTLIBNAME} PRIVATE "simple_filter.cpp")