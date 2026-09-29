cmake_minimum_required(VERSION 3.16)
project(engine_client)
set(SRCDIR "${CMAKE_SOURCE_DIR}")
set(ESRCDIR "${CMAKE_CURRENT_SOURCE_DIR}")

#This is the non-dedicated engine
if( DEDICATED )
    return()
endif()

include(engine_inc.cmake)