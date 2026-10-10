cmake_minimum_required(VERSION 3.16)
project(shaderapimetal_client)
set(SRCDIR "${CMAKE_SOURCE_DIR}")
set(CMAKE_MODULE_PATH ${SRCDIR}/cmake)
set(OUTBINNAME "shaderapimetal_client")
set(OUTBINDIR ${SRCDIR}/../game/bin)

# The shader API built against ToMetal instead of ToGL: the same sources as
# shaderapidx9, with USE_METAL. The launcher loads it when started with -metal.

include(${CMAKE_MODULE_PATH}/detect_platform.cmake)
include(${CMAKE_MODULE_PATH}/source_dll_base.cmake)

set(SHADERAPI_SRCDIR "${SRCDIR}/materialsystem/shaderapidx9")

include_directories(../)
include_directories(${SHADERAPI_SRCDIR})
add_definitions(-DSHADERAPIDX9 -DPROTECTED_THINGS_ENABLE -Dstrncpy=use_Q_strncpy_instead -D_snprintf=use_Q_snprintf_instead)

add_definitions(-DDX_TO_GL_ABSTRACTION -DUSE_METAL)

#Source files
target_sources(${OUTBINNAME} PRIVATE "${SHADERAPI_SRCDIR}/colorformatdx8.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SHADERAPI_SRCDIR}/d3d_async.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SHADERAPI_SRCDIR}/hardwareconfig.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SHADERAPI_SRCDIR}/meshbase.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SHADERAPI_SRCDIR}/meshdx8.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SHADERAPI_SRCDIR}/shaderapidx8.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SHADERAPI_SRCDIR}/shaderdevicebase.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SHADERAPI_SRCDIR}/shaderapibase.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SHADERAPI_SRCDIR}/shaderdevicedx8.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SHADERAPI_SRCDIR}/shadershadowdx8.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SHADERAPI_SRCDIR}/texturedx8.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SHADERAPI_SRCDIR}/TransitionTable.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SHADERAPI_SRCDIR}/cvballoctracker.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SHADERAPI_SRCDIR}/vertexdecl.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SHADERAPI_SRCDIR}/vertexshaderdx8.cpp")
target_sources(${OUTBINNAME} PRIVATE "${SHADERAPI_SRCDIR}/winutils.cpp")

#Linker
target_link_libraries(${OUTBINNAME} videocfg_client)
target_link_libraries(${OUTBINNAME} libtometal_client)

target_link_libraries(${OUTBINNAME} bitmap_client tier2_client vtf_client bzip2_client )
