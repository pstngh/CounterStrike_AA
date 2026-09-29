cmake_minimum_required(VERSION 3.16)
project(dedicated_client)
set(SRCDIR "${CMAKE_SOURCE_DIR}")
set(CMAKE_MODULE_PATH ${SRCDIR}/cmake)
set(OUTBINNAME "dedicated_client")
set(OUTBINDIR ${SRCDIR}/../game/bin)

include(${CMAKE_MODULE_PATH}/detect_platform.cmake)
include(${CMAKE_MODULE_PATH}/source_dll_base.cmake)

include_directories(${SRCDIR}/engine)
add_definitions(-DDEDICATED -DLAUNCHERONLY)

if( USE_KISAK_PHYSICS )
    add_definitions(-DUSE_KISAK_PHYSICS)
elseif( USE_BULLET_PHYSICS )
    add_definitions(-DUSE_BULLET_PHYSICS)
endif()

#Source
target_sources(${OUTBINNAME} PRIVATE "sys_linux.cpp")
target_sources(${OUTBINNAME} PRIVATE "filesystem.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SRCDIR}/public/filesystem_init.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SRCDIR}/common/netapi.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SRCDIR}/common/SteamAppStartup.cpp")
target_sources(${OUTBINNAME} PRIVATE "sys_common.cpp")
target_sources(${OUTBINNAME} PRIVATE "sys_ded.cpp")
target_sources(${OUTBINNAME} PRIVATE "sys_subproc.cpp")

## Console ##
target_sources(${OUTBINNAME} PRIVATE "console/conproc.cpp")
target_sources(${OUTBINNAME} PRIVATE "console/textconsole.cpp")
target_sources(${OUTBINNAME} PRIVATE "console/TextConsoleUnix.cpp")

## VGUI ##
target_sources(${OUTBINNAME} PRIVATE "vgui/CreateMultiplayerGameServerPage.cpp")
target_sources(${OUTBINNAME} PRIVATE "vgui/MainPanel.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SRCDIR}/public/vgui_controls/vgui_controls.cpp")
target_sources(${OUTBINNAME} PRIVATE "vgui/vguihelpers.cpp")

## Filesystem ##
target_sources(${OUTBINNAME} PRIVATE "${SRCDIR}/filesystem/filetracker.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SRCDIR}/filesystem/basefilesystem.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SRCDIR}/filesystem/basefilesystemasync.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SRCDIR}/filesystem/filesystem_stdio.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SRCDIR}/filesystem/filesystemasync.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SRCDIR}/public/zip_utils.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SRCDIR}/filesystem/linux_support.cpp")

target_link_libraries(${OUTBINNAME} appframework_client dmxloader_client mathlib_client tier2_client tier3_client libvstdlib_client vpklib_client)

if( OSXALL )
    target_link_libraries(${OUTBINNAME} vgui_controls_client)
endif()