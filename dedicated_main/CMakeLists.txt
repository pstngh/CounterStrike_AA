cmake_minimum_required(VERSION 3.16)
project(dedicated_main)
set(SRCDIR "${CMAKE_SOURCE_DIR}")
set(CMAKE_MODULE_PATH ${SRCDIR}/cmake)
set(OUTBINDIR "${SRCDIR}/../game")

include(${CMAKE_MODULE_PATH}/detect_platform.cmake)

set(NOSTINKYLINKIES "1")
set(NOSKELETONBASE "1")

set(OUTBINNAME "srcds_osx")

include(${CMAKE_MODULE_PATH}/source_exe_base.cmake)

target_sources(${OUTBINNAME} PRIVATE main.cpp)
target_sources(${OUTBINNAME} PRIVATE ${SRCDIR}/common/SteamAppStartup.cpp)
