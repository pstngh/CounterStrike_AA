cmake_minimum_required(VERSION 3.16)
project(engine_client)
set(SRCDIR "${CMAKE_SOURCE_DIR}")
set(ESRCDIR "${SRCDIR}/engine")


#This engine only exists so you don't have to rebuild
#the entire engine with -DDEDICATED when you want to build dedicated
if( NOT DEDICATED )
    return()
endif()

include(../engine/engine_inc.cmake)