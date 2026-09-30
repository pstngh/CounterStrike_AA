# Writes a header naming the source tree's git commit, for logs that must say
# which build produced them. Run with -DSRC=<source dir> -DOUT=<header path>.
# The header is rewritten only when the commit changes, so an unchanged tree
# does not rebuild anything that includes it.

execute_process(COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${SRC}"
    OUTPUT_VARIABLE GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT GIT_COMMIT)
    set(GIT_COMMIT "unknown")
else()
    execute_process(COMMAND git status --porcelain --untracked-files=no
        WORKING_DIRECTORY "${SRC}"
        OUTPUT_VARIABLE GIT_CHANGES
        ERROR_QUIET)
    if(GIT_CHANGES)
        set(GIT_COMMIT "${GIT_COMMIT}-modified")
    endif()
endif()

set(CONTENT "#define KISAK_GIT_COMMIT \"${GIT_COMMIT}\"\n")
if(EXISTS "${OUT}")
    file(READ "${OUT}" OLD_CONTENT)
endif()
if(NOT "${CONTENT}" STREQUAL "${OLD_CONTENT}")
    file(WRITE "${OUT}" "${CONTENT}")
endif()
