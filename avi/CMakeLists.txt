#TODO: Not Done. Moving to the tier libraries first and following the build-order -lwss

cmake_minimum_required(VERSION 3.16)
project(valve_avi_client)
set(SRCDIR "${CMAKE_SOURCE_DIR}")
set(OUTBINNAME "valve_avi_client")
set(CMAKE_MODULE_PATH ${SRCDIR}/cmake)
set(OUTBINDIR "${SRCDIR}/../game/bin")

include(${CMAKE_MODULE_PATH}/detect_platform.cmake)
include(${CMAKE_MODULE_PATH}/source_dll_base.cmake)
#include("${CMAKE_MODULE_PATH}/source_video_base.cmake") already included in source_bases.

#Compiler
if( LINUXALL )
    #these dont exist... Seems to work... Commented out
    #include_directories(${SRCDIR}/thirdparty/sdl/include)
    #include_directories(${SRCDIR}/thirdparty/sdl_mixer)
endif()


#linker
if( LINUXALL )
    #target_link_options(${OUTBINNAME} PRIVATE "-z muldefs -L${SRCDIR}/thirdparty/sdl/build/.libs")
    target_link_libraries(${OUTBINNAME} SDL2)
endif()

#source files
if( OSXALL )
    target_sources(${OUTBINNAME} PRIVATE "avi_osx.cpp")
else()
    target_sources(${OUTBINNAME} PRIVATE "bink.cpp")
endif()

target_link_libraries(${OUTBINNAME} tier2_client tier3_client libvstdlib_client)
if( LINUXALL )
    #target_link_libraries(${OUTBINNAME} ${SRCDIR}/lib/common/libSDL2_mixer.a)
    target_link_libraries(${OUTBINNAME} SDL2_mixer)
endif()
