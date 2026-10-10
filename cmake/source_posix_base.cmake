include("${CMAKE_MODULE_PATH}/common_functions.cmake")

MacroRequired(SRCDIR)
MacroRequired(_DLL_EXT)

set(LIBPUBLIC "${SRCDIR}/lib/public${PLATSUBDIR}") #this is where static libs are
set(STEAM_API_LIBRARY steam_api_offline)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
    message(STATUS "Build type not specified")
endif(NOT CMAKE_BUILD_TYPE)

set(OSX_DEBUG_FLAGS " -gdwarf-2 -g2 ")
if(RELEASE_DEBUG_INFO)
    set(OSX_RELEASE_DEBUG_FLAGS "${OSX_DEBUG_FLAGS}")
endif()

string(TOUPPER "${CMAKE_BUILD_TYPE}" BUILD_TYPE_UPPER)
if (BUILD_TYPE_UPPER STREQUAL "DEBUG")
    message(STATUS "Building in Debug mode")
    add_definitions(-DBASE -DDEBUG -D_DEBUG -DDBGFLAG_ASSERT)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OSX_DEBUG_FLAGS} -Og ${KISAK_ARCH_FLAGS}")
else()
    message(STATUS "Building in Release mode")
    add_definitions(-DBASE -DNDEBUG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OSX_RELEASE_DEBUG_FLAGS} -O2 ${KISAK_ARCH_FLAGS}")
endif()

include_directories("${SRCDIR}/common")
include_directories("${SRCDIR}/public")
include_directories("${SRCDIR}/public/tier0")
include_directories("${SRCDIR}/public/tier1")
add_definitions(-DGNUC -DPOSIX -DCOMPILER_GCC -DMEMOVERRIDE_MODULE=${PROJECT_NAME} -D_DLL_EXT=${_DLL_EXT})
if(DEDICATED)
    add_definitions(-DDEDICATED)
endif()
add_definitions(-D_OSX -DOSX -D_DARWIN_UNLIMITED_SELECT -DFD_SETSIZE=10240)
# Darwin keeps iconv in a separate system library; tier1's conversion
# helpers are pulled into several shared modules.
find_library(ICONV_LIBRARY iconv REQUIRED)
link_libraries("${ICONV_LIBRARY}")

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
add_definitions(-DPOSIX -D_POSIX)
if(OSX64)
    add_definitions(-DPLATFORM_64BITS)
endif()

if(NOT IS_LIB_PROJECT)
    if( NOSTINKYLINKIES )
        message(STATUS "skipping stinky linkie")
    else()
        link_libraries("libtier0_client")
        link_libraries("tier1_client")
        link_libraries("interfaces_client")
    endif()
endif()
