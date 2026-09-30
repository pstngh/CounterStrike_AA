# Cross-compiles the Windows x64 build on Linux with MinGW-w64 GCC. The win32
# thread model with a static libgcc and libstdc++ leaves the binaries needing
# only system DLLs and SDL2.dll. SDL2 and FreeType are installed into the
# MinGW sysroot; see README.md.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc-win32)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-win32)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Link each binary's libraries as one group, so that, as for the Linux build's
# shared objects, their order doesn't matter.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++ -Wl,--start-group")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++ -Wl,--start-group")
set(CMAKE_C_STANDARD_LIBRARIES "-Wl,--end-group -lkernel32 -luser32 -lgdi32 -lwinspool -lshell32 -lole32 -loleaut32 -luuid -lcomdlg32 -ladvapi32")
set(CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_C_STANDARD_LIBRARIES}")
