# Kisak-Strike: Gentoo Offensive
# Main CMake File

cmake_minimum_required(VERSION 3.16)
project(KisakStrike)

# Bundled third-party projects still declare CMake 2.x/3.0 minimums. CMake 4
# rejects those unless a supported policy floor is supplied for subdirectories.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0" AND NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

include(kisak-strike-build-options.cmake)

if(USE_MAC_PRESET)
    add_definitions(-DUSE_MAC_PRESET)
endif()

if(APPLE)
    if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        message(FATAL_ERROR "macOS builds support Apple Silicon (arm64) only")
    endif()
    include_directories(BEFORE "${CMAKE_SOURCE_DIR}/thirdparty/macos_compat")
    include_directories(BEFORE "${CMAKE_SOURCE_DIR}/thirdparty/sse2neon")
    # The 2017 codebase intentionally initializes signed SIMD masks with
    # hexadecimal bit patterns outside INT_MAX. Modern Clang diagnoses those
    # pre-C++11 initializers as errors even though the resulting bits are exact.
    add_compile_options(-Wno-c++11-narrowing)
    add_compile_definitions(
        SSE2NEON_PRECISE_MINMAX=1
        SSE2NEON_PRECISE_SQRT=1
        SSE2NEON_PRECISE_DIV=1
    )

    # Homebrew's sdl2-compat package provides SDL2. Prefer its opt link, then
    # fall back to the default search (the second call is skipped once found).
    find_library(SDL2_LIBRARY SDL2 PATHS /opt/homebrew/opt/sdl2/lib NO_DEFAULT_PATH)
    find_library(SDL2_LIBRARY SDL2 REQUIRED)

    # Offline Steam API shim; game modules find it through their @rpath.
    add_library(steam_api_offline SHARED thirdparty/steam_api_offline/steam_api_offline.cpp)
    target_include_directories(steam_api_offline PRIVATE "${CMAKE_SOURCE_DIR}/public")
    target_compile_definitions(steam_api_offline PRIVATE STEAM_API_EXPORTS GNUC VERSION_SAFE_STEAM_API_INTERFACES)
    set_target_properties(steam_api_offline PROPERTIES
        OUTPUT_NAME steam_offline
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../game/bin/osx64")
endif()

add_subdirectory(appframework)
add_subdirectory(external/crypto++-5.61)
add_subdirectory(interfaces)
add_subdirectory(gcsdk)
add_subdirectory(tier0)
add_subdirectory(tier1)
add_subdirectory(tier2)
add_subdirectory(tier3)
add_subdirectory(vstdlib)
add_subdirectory(mathlib) #also does mathlib_extended.
add_subdirectory(vpklib)
add_subdirectory(vgui2/matsys_controls)
add_subdirectory(vgui2/src)
add_subdirectory(vgui2/vgui_controls)
add_subdirectory(vgui2/vgui_surfacelib)
add_subdirectory(soundsystem/lowlevel)
add_subdirectory(thirdparty/quickhull)
if(APPLE)
    # tcmalloc is only linked by the Linux launchers. Its bundled autotools
    # source does not configure on current macOS SDKs.
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "e2k")
    # We use sustem gperftools-2.5 on OS Elbrus
else()
    add_subdirectory(thirdparty/gperftools-2.8.1 EXCLUDE_FROM_ALL) #We include this version instead of distro-pkg because there is a false positive in ASAN
endif()
# Only build the vendored targets the engine links (tcmalloc_minimal, libprotobuf, protoc).
add_subdirectory(thirdparty/protobuf-2.5.0/cmake EXCLUDE_FROM_ALL)
add_subdirectory(utils/bzip2)
add_subdirectory(utils/jpeglib)
add_subdirectory(videocfg)
add_subdirectory(vtf)

add_subdirectory(avi)
add_subdirectory(bitmap)
add_subdirectory(bonesetup)
add_subdirectory(choreoobjects)
add_subdirectory(datacache)
add_subdirectory(dedicated)
add_subdirectory(dedicated_main)
add_subdirectory(dmxloader)
add_subdirectory(engine)
add_subdirectory(engine_ds)
add_subdirectory(filesystem)
add_subdirectory(inputsystem)
add_subdirectory(localize)
add_subdirectory(materialsystem)
add_subdirectory(materialsystem/shaderlib)
add_subdirectory(materialsystem/shaderapiempty)
add_subdirectory(materialsystem/stdshaders)
add_subdirectory(materialsystem/shaderapidx9)
add_subdirectory(meshutils)
add_subdirectory(particles)
add_subdirectory(raytrace)
add_subdirectory(resourcefile)
add_subdirectory(responserules/runtime)
add_subdirectory(scenefilecache)
add_subdirectory(serverbrowser)
add_subdirectory(soundemittersystem)
add_subdirectory(studiorender)
add_subdirectory(togl)
add_subdirectory(vguimatsurface)
add_subdirectory(vscript)

add_subdirectory(game/client) #CSGO Client
add_subdirectory(game/server) #CSGO Server
add_subdirectory(matchmaking) #Does all 4 matchmaking projects

add_subdirectory(launcher)
add_subdirectory(launcher_main) #The actual executable that just dlopen's launcher

# USE_SCALEFORM compiles the Scaleform HUD into game/client instead, which
# loads Valve's prebuilt scaleformui_client.so at runtime.
if( USE_ROCKETUI AND NOT USE_SCALEFORM )
    #Custom Kisak-Strike UI powered by RmlUI
    add_subdirectory(thirdparty/RmlUi)
    add_subdirectory(rocketui)
endif()

#Frankenstein physics lib sewn together from 3 different leaks by mad scientist lwss
if( USE_KISAK_PHYSICS )
    message("Using Open-Source Kisakstrike Physics rebuild!\n")
    add_subdirectory(ivp/havana/havok/hk_base)
    add_subdirectory(ivp/havana/havok/hk_math)
    add_subdirectory(ivp/havana/havok/hk_physics/constraint)
    add_subdirectory(ivp/ivp_compact_builder)
    add_subdirectory(ivp/ivp_physics)
    add_subdirectory(vphysics)
#Experimental open source Bullet Physics
elseif( USE_BULLET_PHYSICS )
    #lwss: threading is experimental
    if( USE_BULLET_PHYSICS_THREADED )
        add_definitions(-DBT_THREADSAFE -DBT_USE_OPENMP)
        set(BULLET2_MULTITHREADING ON CACHE BOOL "" FORCE)
        set(BULLET2_USE_OPEN_MP_MULTITHREADING ON CACHE BOOL "" FORCE)
    endif()
    message("Using Bullet Open-Source Physics\n")
    set(BUILD_BULLET2_DEMOS OFF CACHE BOOL "" FORCE)
    set(BUILD_CPU_DEMOS OFF CACHE BOOL "" FORCE)
    set(BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    set(BUILD_BULLET3 OFF CACHE BOOL "" FORCE)
    set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_ENET OFF CACHE BOOL "" FORCE)
    set(BUILD_CLSOCKET OFF CACHE BOOL "" FORCE)
    set(INSTALL_LIBS OFF CACHE BOOL "" FORCE)
    set(OpenGL_GL_PREFERENCE LEGACY)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" BUILD_TYPE_UPPER)
    if (BUILD_TYPE_UPPER STREQUAL "DEBUG")
        message("Adding Bullet debug flag\n")
        add_definitions(-DBT_DEBUG)
        add_definitions(-DBT_DEBUG_MEMORY_ALLOCATIONS)
    endif()
    add_subdirectory(thirdparty/bullet3)
    add_subdirectory(vphysics_bullet)
else()
    message("Using Valve Standard VPhysics .so blob!\n")
endif()
