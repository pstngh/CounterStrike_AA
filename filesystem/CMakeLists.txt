cmake_minimum_required(VERSION 3.16)
project(filesystem_stdio_client)
set(SRCDIR "${CMAKE_SOURCE_DIR}")
set(CMAKE_MODULE_PATH ${SRCDIR}/cmake)
set(OUTBINNAME "filesystem_stdio_client")
set(OUTBINDIR ${SRCDIR}/../game/bin)

include(${CMAKE_MODULE_PATH}/detect_platform.cmake)
include(${CMAKE_MODULE_PATH}/source_dll_base.cmake)

add_definitions(-DDONT_PROTECT_FILEIO_FUNCTIONS -DPROTECTED_THINGS_ENABLE)

target_sources(${OUTBINNAME} PRIVATE "basefilesystem.cpp")
target_sources(${OUTBINNAME} PRIVATE "basefilesystemasync.cpp")
target_sources(${OUTBINNAME} PRIVATE "filetracker.cpp")
target_sources(${OUTBINNAME} PRIVATE "filesystemasync.cpp")
target_sources(${OUTBINNAME} PRIVATE "filesystem_stdio.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SRCDIR}/public/kevvaluescompiler.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SRCDIR}/public/zip_utils.cpp")
target_sources(${OUTBINNAME} PRIVATE "QueuedLoader.cpp")
target_sources(${OUTBINNAME} PRIVATE "linux_support.cpp")

target_link_libraries(${OUTBINNAME} tier2_client vpklib_client libvstdlib_client)