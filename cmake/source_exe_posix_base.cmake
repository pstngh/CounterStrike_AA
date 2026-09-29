include("${CMAKE_MODULE_PATH}/common_functions.cmake")
include("${CMAKE_MODULE_PATH}/source_posix_base.cmake")

MacroRequired(SRCDIR)
MacroRequired(OUTBINNAME)
MacroRequired(OUTBINDIR)

add_definitions( -DEXENAME=${OUTBINNAME} )

add_executable(${OUTBINNAME})

set_target_properties(${OUTBINNAME} PROPERTIES OUTPUT_NAME "${OUTBINNAME}")
set_target_properties(${OUTBINNAME} PROPERTIES SUFFIX "")
set_target_properties(${OUTBINNAME} PROPERTIES PREFIX "")

message("Adding executable target: ${OUTBINNAME}\n")

set_target_properties( ${OUTBINNAME} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${OUTBINDIR}"
        LIBRARY_OUTPUT_DIRECTORY "${OUTBINDIR}"
        RUNTIME_OUTPUT_DIRECTORY "${OUTBINDIR}"
        )

if( NOSKELETONBASE )
    message(STATUS "Not including Skeleton base.")
else()
    target_sources(${OUTBINNAME} PRIVATE "${SRCDIR}/public/tier0/memoverride.cpp")
endif()
