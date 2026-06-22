#ifndef LIBBOTANIK_SIZES_H
#define LIBBOTANIK_SIZES_H

/**
 * @file libbotanik_sizes.h
 * @brief Memory-footprint contract - static_asserts pinning the sizes of the
 *        long-lived "big" objects. Included at the end of libbotanik.h, once
 *        all the pinned types are visible.
 */

#include "libbotanik.h"

// Memory-footprint contract
// ----------------------------------------------------------------------------
// Exact sizes of the long-lived "big" objects, pinned so an accidental layout
// change trips at compile time (tests/test_sizes.cpp prints the table and the
// rationale). Figures differ by word size - host x86-64 vs OPTA (ARM Cortex-M7,
// 32-bit) - so each is spelled per target. The payload-templated types are
// pinned against a representative probe payload (empty input, 60-byte output,
// mirroring the simulator site); a real site's combined PLC struct is checked
// against the PLC IDE's generated types in its own sketch.
namespace Botanik { namespace detail {

    struct ProbePlcIn  {};                   // empty payload (library log fields only)
    struct ProbePlcOut { uint32_t _w[15]; }; // 60 B, 4-aligned

// Pick the per-target footprint. Keyed off TARGET_SIMULATOR (the hardware macro is
// confined to the HAL, so we invert: absent simulator = OPTA-class/ARM build).
#if !defined(TARGET_SIMULATOR)
#  define BOTANIK_SIZE(host_, opta_) (opta_)
#else
#  define BOTANIK_SIZE(host_, opta_) (host_)
#endif

    // PLC shared memory + sync state machines
    static_assert(sizeof(PlcIO::LogEntry_t)                                == BOTANIK_SIZE(  268,   268), "LogEntry_t footprint changed");
    static_assert(sizeof(PlcIO::PlcOutputLogs_t)                           == BOTANIK_SIZE( 8580,  8580), "PlcOutputLogs_t footprint changed");
    static_assert(sizeof(PlcIO::PlcOutputConfig_t)                         == BOTANIK_SIZE(  556,   556), "PlcOutputConfig_t footprint changed");
    static_assert(sizeof(PlcIO::PlcOutput<ProbePlcOut>)                    == BOTANIK_SIZE( 9216,  9216), "PlcOutput footprint changed");
    static_assert(sizeof(PlcIO::PlcSyncContext)                            == BOTANIK_SIZE( 9232,  9200), "PlcSyncContext footprint changed");
    static_assert(sizeof(PlcIO::PlcEmulatorContext)                        == BOTANIK_SIZE( 8616,  8608), "PlcEmulatorContext footprint changed");
    static_assert(sizeof(PlcIO::PlcSyncHandler<ProbePlcIn, ProbePlcOut>)   == BOTANIK_SIZE( 9344,  9292), "PlcSyncHandler footprint changed");
    static_assert(sizeof(PlcIO::PlcEmulator<ProbePlcIn, ProbePlcOut>)      == BOTANIK_SIZE( 8712,  8688), "PlcEmulator footprint changed");

    // Logging
    static_assert(sizeof(Logs::BufferedLogWriter)                          == BOTANIK_SIZE( 8872,  8856), "BufferedLogWriter footprint changed");
    static_assert(sizeof(Logs::SerialLogWriter)                            == BOTANIK_SIZE( 8880,  8860), "SerialLogWriter footprint changed");
    static_assert(sizeof(Logs::Logger)                                     == BOTANIK_SIZE( 2200,  2152), "Logger footprint changed");

    // Terminal / serial
    static_assert(sizeof(Utils::BufferedSerial)                            == BOTANIK_SIZE( 4376,  4372), "BufferedSerial footprint changed");
    static_assert(sizeof(Terminal::TextEditor)                             == BOTANIK_SIZE(  976,   968), "TextEditor footprint changed");

    // Pages
    static_assert(sizeof(Pages::MetricPage)                                == BOTANIK_SIZE( 1232,  1224), "MetricPage footprint changed");
    static_assert(sizeof(Pages::ShellPage)                                 == BOTANIK_SIZE(19912, 19840), "ShellPage footprint changed");
    static_assert(sizeof(Pages::LogsPage)                                  == BOTANIK_SIZE(25424, 25396), "LogsPage footprint changed");
    static_assert(sizeof(Pages::HomePage<ProbePlcOut>)                     == BOTANIK_SIZE(11680, 11656), "HomePage footprint changed");
    static_assert(sizeof(Pages::StatusPage<ProbePlcOut>)                   == BOTANIK_SIZE( 1240,  1232), "StatusPage footprint changed");
    static_assert(sizeof(Pages::ShellScrollback)                           == BOTANIK_SIZE( 8274,  8274), "ShellScrollback footprint changed");
    static_assert(sizeof(Pages::LogsScrollback)                            == BOTANIK_SIZE(16530, 16530), "LogsScrollback footprint changed");
    static_assert(sizeof(Pages::HomeScrollback)                            == BOTANIK_SIZE( 1566,  1566), "HomeScrollback footprint changed");

#undef BOTANIK_SIZE

}}

#endif // LIBBOTANIK_SIZES_H
