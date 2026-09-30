cmake_minimum_required(VERSION 3.16)
project(shaderapidx9_client)
set(SRCDIR "${CMAKE_SOURCE_DIR}")
set(CMAKE_MODULE_PATH ${SRCDIR}/cmake)
set(OUTBINNAME "shaderapidx9_client")
set(OUTBINDIR ${SRCDIR}/../game/bin)

include(${CMAKE_MODULE_PATH}/detect_platform.cmake)
include(${CMAKE_MODULE_PATH}/source_dll_base.cmake)

include_directories(../)
add_definitions(-DSHADERAPIDX9 -DPROTECTED_THINGS_ENABLE -Dstrncpy=use_Q_strncpy_instead -D_snprintf=use_Q_snprintf_instead)

add_definitions(-DGL_GLEXT_PROTOTYPES -DDX_TO_GL_ABSTRACTION)

#Source files
target_sources(${OUTBINNAME} PRIVATE "colorformatdx8.cpp")
target_sources(${OUTBINNAME} PRIVATE "d3d_async.cpp")
if( NOT OSXALL )
    target_sources(${OUTBINNAME} PRIVATE "${SRCDIR}/public/filesystem_helpers.cpp")
endif()
target_sources(${OUTBINNAME} PRIVATE "hardwareconfig.cpp")
target_sources(${OUTBINNAME} PRIVATE "meshbase.cpp")
target_sources(${OUTBINNAME} PRIVATE "meshdx8.cpp")
target_sources(${OUTBINNAME} PRIVATE "shaderapidx8.cpp")
target_sources(${OUTBINNAME} PRIVATE "shaderdevicebase.cpp")
target_sources(${OUTBINNAME} PRIVATE "shaderapibase.cpp")
target_sources(${OUTBINNAME} PRIVATE "shaderdevicedx8.cpp")
target_sources(${OUTBINNAME} PRIVATE "shadershadowdx8.cpp")
target_sources(${OUTBINNAME} PRIVATE "texturedx8.cpp")
target_sources(${OUTBINNAME} PRIVATE "TransitionTable.cpp")
target_sources(${OUTBINNAME} PRIVATE "cvballoctracker.cpp")
target_sources(${OUTBINNAME} PRIVATE "vertexdecl.cpp")
target_sources(${OUTBINNAME} PRIVATE "vertexshaderdx8.cpp")
target_sources(${OUTBINNAME} PRIVATE "winutils.cpp")

#Linker
target_link_libraries(${OUTBINNAME} videocfg_client)
target_link_libraries(${OUTBINNAME} libtogl_client)

target_link_libraries(${OUTBINNAME} bitmap_client tier2_client vtf_client bzip2_client )
