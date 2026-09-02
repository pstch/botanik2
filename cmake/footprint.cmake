# Footprint check for the OPTA image, run after the link.
#
# Neither ceiling is enforceable in the linker script: its .heap absorbs
# whatever .bss leaves, so static RAM growth shrinks the heap rather than
# overflowing the region, and its FLASH LENGTH is computed from CM4_BINARY_START
# (in QSPI), which reads as ~1.4 GB. So the sizes are read back off the linked
# ELF and compared here.
#
# Invoked as: cmake -DELF=... -DSIZE_TOOL=... -DFLASH_MAX=... -DRAM_MAX=...
#             -DOUT=... -P cmake/footprint.cmake

execute_process(COMMAND ${SIZE_TOOL} -A ${ELF}
                OUTPUT_VARIABLE _sz RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "footprint: ${SIZE_TOOL} failed on ${ELF}")
endif()

# One pass over `size -A`, summing by section. FLASH is what the loader writes:
# code, the exception tables, and .data's initialisers. STATIC RAM is .data plus
# .bss - the part of the SRAM region that is spoken for before the heap gets
# what is left. .lwip_sec and .heap are deliberately not counted: the first is
# its own region, and the second is the remainder being measured against.
set(_flash 0)
set(_ram 0)
string(REPLACE "\n" ";" _lines "${_sz}")
foreach(_line IN LISTS _lines)
    if(_line MATCHES "^(\\.[A-Za-z0-9_.]+)[ \t]+([0-9]+)[ \t]+([0-9]+)")
        set(_name "${CMAKE_MATCH_1}")
        set(_bytes "${CMAKE_MATCH_2}")
        if(_name STREQUAL ".text" OR _name STREQUAL ".ARM.extab"
           OR _name STREQUAL ".ARM.exidx" OR _name STREQUAL ".data")
            math(EXPR _flash "${_flash} + ${_bytes}")
        endif()
        if(_name STREQUAL ".data" OR _name STREQUAL ".bss"
           OR _name STREQUAL ".uninitialized")
            math(EXPR _ram "${_ram} + ${_bytes}")
        endif()
    endif()
endforeach()

if(OUT)
    math(EXPR _flash_pct "(${_flash} * 100) / ${FLASH_MAX}")
    math(EXPR _ram_pct   "(${_ram} * 100) / ${RAM_MAX}")
    file(WRITE ${OUT}
         "{\n  \"flash\": ${_flash},\n  \"flash_max\": ${FLASH_MAX},\n"
         "  \"static_ram\": ${_ram},\n  \"ram_max\": ${RAM_MAX},\n"
         "  \"flash_pct\": ${_flash_pct},\n  \"ram_pct\": ${_ram_pct}\n}\n")
endif()

message(STATUS "footprint: flash ${_flash} / ${FLASH_MAX}, "
               "static RAM ${_ram} / ${RAM_MAX}")

# 0 disables a ceiling, which is what a bring-up branch wants.
if(FLASH_MAX GREATER 0 AND _flash GREATER FLASH_MAX)
    message(FATAL_ERROR
            "flash ${_flash} B is over the ${FLASH_MAX} B ceiling. "
            "Raise BOTANIK_FLASH_MAX_BYTES only after reading why it grew - "
            "docs/dev/footprint.md says where the image goes.")
endif()
if(RAM_MAX GREATER 0 AND _ram GREATER RAM_MAX)
    message(FATAL_ERROR
            "static RAM ${_ram} B is over the ${RAM_MAX} B ceiling. "
            "This is the load-bearing half: what it takes comes out of the "
            "heap, which lwIP and Mbed draw on at runtime. See "
            "docs/dev/footprint.md.")
endif()
