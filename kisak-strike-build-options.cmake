# Welcome to the Kisak-Strike options file
# Options here can be freely changed, however it is recommended to set the options via command line.
# Example: cmake .. -DUSE_ROCKETUI=1 -DUSE_KISAK_PHYSICS=1 -DDEDICATED=0



# UI Options
option(USE_ROCKETUI "Use New Open Source Kisak-Strike RmlUI" ON)
option(USE_SCALEFORM "Use In-Complete Proprietary Flash UI with blob ( Not Recommended )" OFF)
# There is also a 3rd option, which is nothing. Nothing will enable base VGUI UI which is also incomplete.

# DEDICATED Server
option(DEDICATED "Build as DEDICATED server. This is Separate from the main build and they are not in-tree compatible.
Make sure to build with -DDEDICATED=0 once you want a regular client again." OFF)

# Physics Options
option(USE_KISAK_PHYSICS "Use the Open Source Physics Re-Build made for kisak-strike from various leaked sources" OFF)
option(USE_BULLET_PHYSICS "Use Open Source Bullet3 Physics Engine(zlib)" OFF)
option(USE_BULLET_PHYSICS_THREADED "Use Multi-Threading for the Bullet Physics Engine. Use convar 'bt_threadcount' to set." OFF)
# 3rd option is to have both of these OFF, the closed source blob from Valve will be used instead.

# Audio Options
option(USE_VALVE_HRTF "Linux only: link Valve's proprietary Steam Audio HRTF library. Copy libphonon3d.so from Valve's Linux binaries depot to ../game/bin/linux64 first" OFF)

# Gameplay Options
# The Mac gameplay preset (fixed loadouts, Allied Assault movement, local-host perks) is described in MACOS.md.
option(USE_MAC_PRESET "Build the Mac gameplay preset instead of stock CS:GO gameplay" ${APPLE})




# Code generation
# The default CPU target runs on any x86-64 CPU from 2009 on (x86-64-v2) or any
# Apple Silicon Mac, so the binaries work on machines other than the build host.
# Set -DKISAK_ARCH_FLAGS=-march=native to tune a build for this machine only.
if(APPLE)
    set(KISAK_ARCH_FLAGS_DEFAULT "-mcpu=apple-m1")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    set(KISAK_ARCH_FLAGS_DEFAULT "-march=x86-64-v2")
else()
    set(KISAK_ARCH_FLAGS_DEFAULT "-march=native")
endif()
set(KISAK_ARCH_FLAGS "${KISAK_ARCH_FLAGS_DEFAULT}" CACHE STRING "CPU target flags for every Kisak-Strike module")
separate_arguments(KISAK_ARCH_FLAGS_LIST UNIX_COMMAND "${KISAK_ARCH_FLAGS}")
option(RELEASE_DEBUG_INFO "Keep debug info in Release builds for symbolized crash backtraces" ON)

# Kisak-Strike Developer Options
# (Gamer Tip: use gdb command `b __asan::ReportGenericError` to break on ASAN errors)
option(USE_ASAN "Enable the Address Sanitizer GCC plugin, used for finding memory errors/bugs" OFF)
option(USE_TRACY "Enable Tracy Profiler support" OFF)
option(TRACY_STORE_LOGS "Turn off Tracy's On-Demand mode. With this flag the profiler will store logs and send them later when the UI connects. Consumes RAM quickly! Mainly useful for profiling the application startup." OFF)

#CMAKE_BUILD_TYPE is supported: RELEASE, DEBUG -- See source_posix_base.cmake for more compiler flags.