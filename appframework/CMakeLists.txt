cmake_minimum_required(VERSION 3.16)
project(appframework_client)
set(SRCDIR "${CMAKE_SOURCE_DIR}")
set(CMAKE_MODULE_PATH ${SRCDIR}/cmake)
set(OUTLIBNAME "appframework_client")
set(AF_SRCDIR "${SRCDIR}/appframework")

include(${CMAKE_MODULE_PATH}/detect_platform.cmake)
include(${CMAKE_MODULE_PATH}/source_lib_base.cmake)

if( LINUXALL )
    add_definitions(-DVERSION_SAFE_STEAM_API_INTERFACES -DDX_TO_GL_ABSTRACTION)
endif()

#Sources
if( LINUXALL )
    #Use system SDL2 for linux
    include_directories("/usr/include/SDL2")
    target_compile_options(${OUTLIBNAME} PRIVATE -fpic -fno-semantic-interposition)
endif()

target_sources(${OUTLIBNAME} PRIVATE "${AF_SRCDIR}/AppSystemGroup.cpp")
target_sources(${OUTLIBNAME} PRIVATE "${SRCDIR}/public/filesystem_init.cpp")
target_sources(${OUTLIBNAME} PRIVATE "${AF_SRCDIR}/posixapp.cpp")
if( SDL )
    target_sources(${OUTLIBNAME} PRIVATE "${AF_SRCDIR}/sdlmgr.cpp")
endif()
if( OSXALL )
    if( SDL )
        target_sources(${OUTLIBNAME} PRIVATE "${AF_SRCDIR}/glmrendererinfo_osx.mm")
    else()
        target_sources(${OUTLIBNAME} PRIVATE "${AF_SRCDIR}/cocoamgr.mm")
    endif()
endif()