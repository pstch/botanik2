# Run at BUILD time (from a custom target) to stamp the current git short hash into a
# generated header as BOTANIK_GIT_HASH. Writes only when the value changes, so it never
# forces a needless rebuild. Inputs: SRC (source dir), OUT (header path to write).
execute_process(
    COMMAND git -C "${SRC}" rev-parse --short HEAD
    OUTPUT_VARIABLE HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT HASH)
    set(HASH "unknown")
else()
    execute_process(COMMAND git -C "${SRC}" diff --quiet HEAD RESULT_VARIABLE DIRTY)
    if(DIRTY)
        set(HASH "${HASH}-dirty")
    endif()
endif()

set(CONTENT "#define BOTANIK_GIT_HASH \"${HASH}\"\n")
set(OLD "")
if(EXISTS "${OUT}")
    file(READ "${OUT}" OLD)
endif()
if(NOT OLD STREQUAL CONTENT)
    file(WRITE "${OUT}" "${CONTENT}")
endif()
