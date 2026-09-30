set(SDL "1")

add_definitions(-DGL_GLEXT_PROTOTYPES -DDX_TO_GL_ABSTRACTION)
if( SDL )
    add_definitions(-DUSE_SDL)
    #Use system SDL2 for linux.
    if( LINUXALL )
        include_directories("/usr/include/SDL2")
    else()
        find_path(SDL2_INCLUDE_DIR SDL.h PATH_SUFFIXES SDL2 REQUIRED)
        include_directories("${SDL2_INCLUDE_DIR}")
    endif()
endif()
