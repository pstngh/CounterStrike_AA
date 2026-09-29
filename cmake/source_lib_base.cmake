include("${CMAKE_MODULE_PATH}/common_functions.cmake")
include("${CMAKE_MODULE_PATH}/platform_dirs.cmake") #defines PLATSUBDIR
include("${CMAKE_MODULE_PATH}/source_base.cmake")


add_definitions(-DCSTRIKE15)

MacroRequired(SRCDIR)

MacroRequired(OUTLIBDIR "${SRCDIR}/lib/public${PLATSUBDIR}")

set( _STATICLIB_EXT ".a")
include("${CMAKE_MODULE_PATH}/source_lib_posix_base.cmake")

include("${CMAKE_MODULE_PATH}/source_video_base.cmake")