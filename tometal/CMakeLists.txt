cmake_minimum_required(VERSION 3.16)
project(libtometal_client)
set(SRCDIR "${CMAKE_SOURCE_DIR}")
set(CMAKE_MODULE_PATH ${SRCDIR}/cmake)
set(OUTBINNAME "libtometal_client") #also the target name
set(OUTBINDIR ${SRCDIR}/../game/bin)

# ToMetal implements the Direct3D 9 interface on Metal, as ToGL does on
# OpenGL. The Metal build of the shader API (shaderapimetal) links it.

set(TOMETAL_SRCDIR "${SRCDIR}/tometal")

include(${CMAKE_MODULE_PATH}/detect_platform.cmake)
include(${CMAKE_MODULE_PATH}/source_dll_base.cmake)

include_directories("../")
include_directories("${TOMETAL_SRCDIR}")

add_definitions(-DTOMETAL_DLL_EXPORT -DUSE_METAL)

target_compile_options(${OUTBINNAME} PRIVATE -fpic -fno-semantic-interposition -Wno-narrowing)

#//-----------------------------------------------------------------------------
#// Sources & Headers
#//-----------------------------------------------------------------------------
set(TOMETAL_OBJC_SOURCES
    "${TOMETAL_SRCDIR}/mtladapter.mm"
    "${TOMETAL_SRCDIR}/mtlconnect.mm"
    "${TOMETAL_SRCDIR}/mtld3ddevice.mm"
    "${TOMETAL_SRCDIR}/mtldevice.mm"
    "${TOMETAL_SRCDIR}/mtlformats.mm"
    "${TOMETAL_SRCDIR}/mtlresources.mm"
)
set_source_files_properties(${TOMETAL_OBJC_SOURCES} PROPERTIES COMPILE_FLAGS "-fobjc-arc")

target_sources(${OUTBINNAME} PRIVATE "${TOMETAL_SRCDIR}/dx9asmtometal.cpp")
target_sources(${OUTBINNAME} PRIVATE "${TOMETAL_SRCDIR}/d3dx.cpp")
target_sources(${OUTBINNAME} PRIVATE ${TOMETAL_OBJC_SOURCES})

#//-----------------------------------------------------------------------------
#// Link
#//-----------------------------------------------------------------------------

target_link_libraries(${OUTBINNAME} tier2_client libvstdlib_client mathlib_client)
target_link_libraries(${OUTBINNAME} "-framework Metal" "-framework QuartzCore" "-framework Foundation" "-framework ApplicationServices")
