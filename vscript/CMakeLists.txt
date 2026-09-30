cmake_minimum_required(VERSION 3.16)
project(vscript_client)
set(SRCDIR "${CMAKE_SOURCE_DIR}")
set(CMAKE_MODULE_PATH ${SRCDIR}/cmake)
set(OUTBINNAME "vscript_client")
set(OUTBINDIR ${SRCDIR}/../game/bin)

include(${CMAKE_MODULE_PATH}/detect_platform.cmake)
include(${CMAKE_MODULE_PATH}/source_dll_base.cmake)

add_definitions(-DVSCRIPT_DLL_EXPORT)
add_definitions(-DLUA_MOD_CASE_INSENSITIVE)

include_directories(./languages/squirrel/include)
include_directories(./languages/squirrel/sqplus)
include_directories(./languages/lua/lua-5.1.4/src)

target_sources(${OUTBINNAME} PRIVATE vscript.cpp)
if( OSXALL OR WIN64 )
    # The original macOS and Windows VPC projects embed Lua 5.1 in vscript.
    # Without these sources the macOS dylib links with dynamic-lookup stubs for
    # ScriptCreateLuaVM and fails as soon as the launcher loads it.
    target_sources(${OUTBINNAME} PRIVATE
        ./languages/lua/vlua/vlua.cpp
        ./languages/lua/vlua/vec3.cpp
        ./languages/lua/lua-5.1.4/src/lapi.c
        ./languages/lua/lua-5.1.4/src/lauxlib.c
        ./languages/lua/lua-5.1.4/src/lbaselib.c
        ./languages/lua/lua-5.1.4/src/lcode.c
        ./languages/lua/lua-5.1.4/src/ldblib.c
        ./languages/lua/lua-5.1.4/src/ldebug.c
        ./languages/lua/lua-5.1.4/src/ldo.c
        ./languages/lua/lua-5.1.4/src/ldump.c
        ./languages/lua/lua-5.1.4/src/lfunc.c
        ./languages/lua/lua-5.1.4/src/lgc.c
        ./languages/lua/lua-5.1.4/src/linit.c
        ./languages/lua/lua-5.1.4/src/liolib.c
        ./languages/lua/lua-5.1.4/src/llex.c
        ./languages/lua/lua-5.1.4/src/lmathlib.c
        ./languages/lua/lua-5.1.4/src/lmem.c
        ./languages/lua/lua-5.1.4/src/loadlib.c
        ./languages/lua/lua-5.1.4/src/lobject.c
        ./languages/lua/lua-5.1.4/src/lopcodes.c
        ./languages/lua/lua-5.1.4/src/loslib.c
        ./languages/lua/lua-5.1.4/src/lparser.c
        ./languages/lua/lua-5.1.4/src/lstate.c
        ./languages/lua/lua-5.1.4/src/lstring.c
        ./languages/lua/lua-5.1.4/src/lstrlib.c
        ./languages/lua/lua-5.1.4/src/ltable.c
        ./languages/lua/lua-5.1.4/src/ltablib.c
        ./languages/lua/lua-5.1.4/src/ltm.c
        ./languages/lua/lua-5.1.4/src/lundump.c
        ./languages/lua/lua-5.1.4/src/lvm.c
        ./languages/lua/lua-5.1.4/src/lzio.c
        ./languages/lua/lua-5.1.4/src/print.c
    )
endif()
target_sources(${OUTBINNAME} PRIVATE ./languages/squirrel/vsquirrel/vsquirrel.cpp)

#TODO: nut builder for nut_init.h? Or hardcode like the others

#Squirrel
target_sources(${OUTBINNAME} PRIVATE "./languages/squirrel/squirrel/sqapi.cpp" )
target_sources(${OUTBINNAME} PRIVATE "./languages/squirrel/squirrel/sqbaselib.cpp" )
target_sources(${OUTBINNAME} PRIVATE "./languages/squirrel/squirrel/sqclass.cpp" )
target_sources(${OUTBINNAME} PRIVATE "./languages/squirrel/squirrel/sqcompiler.cpp" )
target_sources(${OUTBINNAME} PRIVATE "./languages/squirrel/squirrel/sqdebug.cpp" )
target_sources(${OUTBINNAME} PRIVATE "./languages/squirrel/squirrel/sqfuncstate.cpp" )
target_sources(${OUTBINNAME} PRIVATE "./languages/squirrel/squirrel/sqlexer.cpp" )
target_sources(${OUTBINNAME} PRIVATE "./languages/squirrel/squirrel/sqmem.cpp" )
target_sources(${OUTBINNAME} PRIVATE "./languages/squirrel/squirrel/sqobject.cpp" )
target_sources(${OUTBINNAME} PRIVATE "./languages/squirrel/squirrel/sqstate.cpp" )
target_sources(${OUTBINNAME} PRIVATE "./languages/squirrel/squirrel/sqtable.cpp" )
target_sources(${OUTBINNAME} PRIVATE "./languages/squirrel/squirrel/sqvm.cpp" )
target_sources(${OUTBINNAME} PRIVATE "./languages/squirrel/sqdbg/sqrdbg.cpp" )
target_sources(${OUTBINNAME} PRIVATE "./languages/squirrel/sqdbg/sqdbgserver.cpp" )

#sqstdlib
target_sources(${OUTBINNAME} PRIVATE "./languages/squirrel/sqstdlib/sqstdaux.cpp")
target_sources(${OUTBINNAME} PRIVATE "./languages/squirrel/sqstdlib/sqstdblob.cpp" )
target_sources(${OUTBINNAME} PRIVATE "./languages/squirrel/sqstdlib/sqstdmath.cpp" )
target_sources(${OUTBINNAME} PRIVATE "./languages/squirrel/sqstdlib/sqstdrex.cpp" )
target_sources(${OUTBINNAME} PRIVATE "./languages/squirrel/sqstdlib/sqstdstream.cpp" )
target_sources(${OUTBINNAME} PRIVATE "./languages/squirrel/sqstdlib/sqstdstring.cpp" )

#sqplus
target_sources(${OUTBINNAME} PRIVATE "./languages/squirrel/sqplus/SqPlus.cpp" )
target_sources(${OUTBINNAME} PRIVATE "./languages/squirrel/sqplus/SquirrelBindingsUtils.cpp" )
target_sources(${OUTBINNAME} PRIVATE "./languages/squirrel/sqplus/SquirrelObject.cpp" )
target_sources(${OUTBINNAME} PRIVATE "./languages/squirrel/sqplus/SquirrelVM.cpp" )

target_link_libraries(${OUTBINNAME} libvstdlib_client mathlib_client)
