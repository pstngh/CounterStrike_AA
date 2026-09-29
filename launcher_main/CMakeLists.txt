cmake_minimum_required(VERSION 3.16)
#dont build this on dedicated build
if( DEDICATED )
    return()
endif()
project(launcher_main)
set(SRCDIR "${CMAKE_SOURCE_DIR}")
set(CMAKE_MODULE_PATH ${SRCDIR}/cmake)
set(OUTBINDIR "${SRCDIR}/../game")

include(${CMAKE_MODULE_PATH}/detect_platform.cmake)

if( OSX64 )
    set(OUTBINNAME "csgo_osx64")
elseif( LINUX64 )
    set(OUTBINNAME "csgo_linux64")
endif()

set(NOSTINKYLINKIES "1")
set(NOSKELETONBASE "1")

include(${CMAKE_MODULE_PATH}/source_exe_base.cmake)

target_sources(${OUTBINNAME} PRIVATE main.cpp)

if( LINUXALL )
    target_link_libraries(${OUTBINNAME} dl pthread)
endif()
