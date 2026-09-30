cmake_minimum_required(VERSION 3.16)
#dont build this on dedicated build
if( DEDICATED )
    return()
endif()
project(launcher_client)
set(SRCDIR "${CMAKE_SOURCE_DIR}")
set(CMAKE_MODULE_PATH ${SRCDIR}/cmake)
set(OUTBINNAME "launcher_client")
set(OUTBINDIR ${SRCDIR}/../game/bin)

set(NOSTINKYLINKIES "1") #link this project carefully ourselves

include(${CMAKE_MODULE_PATH}/detect_platform.cmake)
include(${CMAKE_MODULE_PATH}/source_dll_base.cmake)

add_definitions(-DLAUNCHERONLY -DALLOW_TEXT_MODE=1)
if( USE_SCALEFORM )
    add_definitions(-DINCLUDE_SCALEFORM)
elseif( USE_ROCKETUI )
    add_definitions(-DINCLUDE_ROCKETUI)
endif()

if( USE_KISAK_PHYSICS )
    add_definitions(-DUSE_KISAK_PHYSICS)
elseif( USE_BULLET_PHYSICS )
    add_definitions(-DUSE_BULLET_PHYSICS)
endif()

if( LINUXALL )
    target_link_libraries(${OUTBINNAME} SDL2)
elseif( APPLE )
    # Launcher owns the SDL window/event loop on macOS. Linking it explicitly
    # also makes SDL available to TOGL's runtime-resolved entry points.
    target_link_libraries(${OUTBINNAME} "${SDL2_LIBRARY}")
endif()

target_sources(${OUTBINNAME} PRIVATE "${SRCDIR}/public/filesystem_init.cpp")
target_sources(${OUTBINNAME} PRIVATE "launcher.cpp")
target_sources(${OUTBINNAME} PRIVATE "reslistgenerator.cpp")

target_link_libraries(${OUTBINNAME} appframework_client tier1_client tier2_client tier3_client interfaces_client )
target_link_libraries(${OUTBINNAME} ${STEAM_API_LIBRARY}) # Link to proprietary steamapi
target_link_libraries(${OUTBINNAME} libtogl_client) # Link to precompiled togl
