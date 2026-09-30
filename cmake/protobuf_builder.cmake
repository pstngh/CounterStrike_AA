include("${CMAKE_MODULE_PATH}/common_functions.cmake")

MacroRequired(GENERATED_PROTO_DIR)
MacroRequired(SRCDIR)

include_directories(${GENERATED_PROTO_DIR})
include_directories(${SRCDIR}/thirdparty/protobuf-2.5.0/src)

# This is a target added in /thirdparty/protobuf-2.x. Use its generated full
# path: GUI generators happened to find `protoc` by name, but a clean Ninja
# build on macOS does not put the build directory on PATH.
set(PROTO_COMPILER "$<TARGET_FILE:protoc>")

#Built a .proto file and add the resulting C++ to the target.
macro( TargetBuildAndAddProto TARGET_NAME PROTO_FILE PROTO_OUTPUT_FOLDER )
    set(PROTO_FILENAME)
    get_filename_component(PROTO_FILENAME ${PROTO_FILE} NAME_WLE) #name without any extensions (/home/gamer/swag.proto = swag)

    add_custom_command(
            OUTPUT "${PROTO_OUTPUT_FOLDER}/${PROTO_FILENAME}.pb.cc"
                   "${PROTO_OUTPUT_FOLDER}/${PROTO_FILENAME}.pb.h"
            COMMAND ${PROTO_COMPILER}
            ARGS --cpp_out=. --proto_path=${SRCDIR}/game/shared/cstrike15 --proto_path=${SRCDIR}/thirdparty/protobuf-2.5.0/src --proto_path=${SRCDIR}/gcsdk --proto_path=${SRCDIR}/game/shared --proto_path=${SRCDIR}/common ${PROTO_FILE}
            DEPENDS ${PROTO_FILE} protoc
            WORKING_DIRECTORY ${PROTO_OUTPUT_FOLDER}
            COMMENT "Running homemade protoc compiler on ${PROTO_FILE} - output (${PROTO_OUTPUT_FOLDER}/${PROTO_FILENAME}.pb.cc)"
            VERBATIM
    )

    #add the output folder in the include path.
    target_include_directories(${TARGET_NAME} PRIVATE ${PROTO_OUTPUT_FOLDER})
    target_sources(${TARGET_NAME} PRIVATE ${PROTO_OUTPUT_FOLDER}/${PROTO_FILENAME}.pb.cc)
endmacro()
