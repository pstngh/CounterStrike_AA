include("${CMAKE_MODULE_PATH}/common_functions.cmake")
include("${CMAKE_MODULE_PATH}/source_posix_base.cmake")

MacroRequired(OUTBINNAME)
MacroRequired(OUTBINDIR)

if(LINUX64 OR OSX64)
    set(OUTBINDIR "${OUTBINDIR}${PLATSUBDIR}")
endif()

#set(ConfigurationType "Dynamic Library (.dll)") #not used

#Target
add_library(${OUTBINNAME} SHARED)

set_target_properties(${OUTBINNAME} PROPERTIES OUTPUT_NAME "${OUTBINNAME}")
set_target_properties(${OUTBINNAME} PROPERTIES SUFFIX "${OUTDLLEXT}")
set_target_properties(${OUTBINNAME} PROPERTIES PREFIX "")

if(OSXALL)
    # CMake target names retain the Linux-oriented *_client suffix so internal
    # target dependencies remain unchanged. The on-disk names must match the
    # names used by Valve's macOS loader and the original macOS depot.
    string(REGEX REPLACE "_client$" "" OSX_OUTPUT_NAME "${OUTBINNAME}")
    set_target_properties(${OUTBINNAME} PROPERTIES OUTPUT_NAME "${OSX_OUTPUT_NAME}")

    # Source engine modules deliberately import globals and entry points from
    # one another at runtime (materialsystem <-> shaderapi <-> stdshader, for
    # example). ELF shared objects permit this by default. Mach-O dylibs need
    # the equivalent behavior requested explicitly.
    target_link_options(${OUTBINNAME} PRIVATE "LINKER:-undefined,dynamic_lookup")
endif()

target_compile_definitions(${OUTBINNAME} PRIVATE -DDLLNAME=${OUTBINNAME})

message("Adding dll target: ${OUTBINNAME}${OUTDLLEXT}\n")

set_target_properties( ${OUTBINNAME} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${OUTBINDIR}"
        LIBRARY_OUTPUT_DIRECTORY "${OUTBINDIR}"
        RUNTIME_OUTPUT_DIRECTORY "${OUTBINDIR}"
        )

if( NOSKELETONBASE )
    message(STATUS "Not including Skeleton base.")
else()
    # Skeleton Project - All derived projects get this as a starting base
    target_sources(${OUTBINNAME} PRIVATE "${SRCDIR}/public/tier0/memoverride.cpp")
endif()
