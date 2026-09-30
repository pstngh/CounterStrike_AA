cmake_minimum_required(VERSION 3.16)
#dont build this on dedicated build
if( DEDICATED )
    return()
endif()

project(inputsystem_client)
set(SRCDIR "${CMAKE_SOURCE_DIR}")
set(CMAKE_MODULE_PATH ${SRCDIR}/cmake)
set(OUTBINNAME "inputsystem_client")
set(OUTBINDIR ${SRCDIR}/../game/bin)

include(${CMAKE_MODULE_PATH}/detect_platform.cmake)
include(${CMAKE_MODULE_PATH}/source_dll_base.cmake)

add_definitions(-DNO_STRING_T -DVERSION_SAFE_STEAM_API_INTERFACES -DPROTECTED_THINGS_ENABLE)

if( LINUXALL )
    target_link_libraries(${OUTBINNAME} SDL2)
endif()

target_sources(${OUTBINNAME} PRIVATE "inputstacksystem.cpp")
target_sources(${OUTBINNAME} PRIVATE "inputsystem.cpp")
target_sources(${OUTBINNAME} PRIVATE "inputsystem.h")
if( OSXALL )
    target_sources(${OUTBINNAME} PRIVATE "joystick_osx.cpp")
endif()
if( LINUXALL )
    target_sources(${OUTBINNAME} PRIVATE "joystick_linux.cpp")
endif()
if( WIN64 )
    target_sources(${OUTBINNAME} PRIVATE "joystick.cpp")
    target_sources(${OUTBINNAME} PRIVATE "xcontroller.cpp")
    target_link_libraries(${OUTBINNAME} libvstdlib_client)
endif()
target_sources(${OUTBINNAME} PRIVATE "steamcontroller.cpp")
target_sources(${OUTBINNAME} PRIVATE "key_translation.cpp")
target_sources(${OUTBINNAME} PRIVATE "key_translation.h")
target_sources(${OUTBINNAME} PRIVATE "${SRCDIR}/common/platforminputdevice.cpp")

target_link_libraries(${OUTBINNAME} tier2_client mathlib_client)
#Requires evil proprietary link to libsteam_api
target_link_libraries(${OUTBINNAME} ${STEAM_API_LIBRARY})

if( APPLE )
    target_link_libraries(${OUTBINNAME} "-framework IOKit" "-framework ForceFeedback" "-framework CoreFoundation" "${SDL2_LIBRARY}")
endif()
