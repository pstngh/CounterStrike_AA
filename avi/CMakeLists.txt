#TODO: Not Done. Moving to the tier libraries first and following the build-order -lwss

cmake_minimum_required(VERSION 3.16)
project(valve_avi_client)
set(SRCDIR "${CMAKE_SOURCE_DIR}")
set(OUTBINNAME "valve_avi_client")
set(CMAKE_MODULE_PATH ${SRCDIR}/cmake)
set(OUTBINDIR "${SRCDIR}/../game/bin")

include(${CMAKE_MODULE_PATH}/detect_platform.cmake)
include(${CMAKE_MODULE_PATH}/source_dll_base.cmake)

#linker
if( LINUXALL )
    target_link_libraries(${OUTBINNAME} SDL2)
endif()

#source files
if( OSXALL )
    target_sources(${OUTBINNAME} PRIVATE "avi_osx.cpp")
endif()

target_link_libraries(${OUTBINNAME} tier2_client tier3_client libvstdlib_client)
if( LINUXALL )
    target_link_libraries(${OUTBINNAME} SDL2_mixer)
endif()
