// REVIEW_35: WIP
#ifndef LIBBOTANIK_SIZES_H
#define LIBBOTANIK_SIZES_H

/**
 * @file libbotanik_sizes.h
 * @brief Memory-footprint contract pinning the "big" objects' sizes.
 *
 * static_asserts pinning the sizes of the long-lived "big" objects, so an
 * accidental layout change trips at compile time (tests/test_sizes.cpp prints
 * the table). Included at the end of libbotanik.h.
 *
 * Figures differ by word size (host x86-64 vs OPTA ARM Cortex-M7, 32-bit), so
 * each is spelled per target. Payload-templated types are pinned against a
 * probe payload (empty input, 60-byte output, mirroring the sim site); a real
 * site's PLC struct is checked against the PLC IDE's generated types in its own
 * sketch.
 */

#include "libbotanik.h"

namespace Botanik { namespace detail {

    // empty payload (library log fields only)
    struct ProbePlcIn  {};
    struct ProbePlcOut { uint32_t _w[15]; }; // 60 B, 4-aligned

// Per-target footprint, keyed off TARGET_SIMULATOR (the hardware macro is
// confined to the HAL, so invert: absent simulator = OPTA-class/ARM build).
#if !defined(TARGET_SIMULATOR)
#  define BOTANIK_SIZE(host_, opta_) (opta_)
#else
#  define BOTANIK_SIZE(host_, opta_) (host_)
#endif

    // PLC shared memory + sync state machines
    static_assert(sizeof(PlcIO::LogEntry_t)                                == BOTANIK_SIZE(  268,   268), "LogEntry_t footprint changed");
    static_assert(sizeof(PlcIO::PlcOutputLogs_t)                           == BOTANIK_SIZE( 8580,  8580), "PlcOutputLogs_t footprint changed");
    static_assert(sizeof(PlcIO::PlcOutputConfig_t)                         == BOTANIK_SIZE(  552,   552), "PlcOutputConfig_t footprint changed");
    static_assert(sizeof(PlcIO::PlcOutput<ProbePlcOut>)                    == BOTANIK_SIZE( 9216,  9216), "PlcOutput footprint changed");
    static_assert(sizeof(PlcIO::PlcSyncContext)                            == BOTANIK_SIZE( 9784,  9748), "PlcSyncContext footprint changed");
    static_assert(sizeof(PlcIO::PlcSyncHandler<ProbePlcIn, ProbePlcOut>)   == BOTANIK_SIZE( 9928,  9856), "PlcSyncHandler footprint changed");
    // PlcEmulator{,Context} pins live with the emulator in botanik_sim/emulator.cpp.

    // Logging
    static_assert(sizeof(Logs::BufferedLogWriter)                          == BOTANIK_SIZE( 8872,  8856), "BufferedLogWriter footprint changed");
    static_assert(sizeof(Logs::SerialLogWriter)                            == BOTANIK_SIZE( 8928,  8884), "SerialLogWriter footprint changed");
    static_assert(sizeof(Logs::Logger)                                     == BOTANIK_SIZE(  280,   216), "Logger footprint changed");

    // Terminal / serial
    static_assert(sizeof(Utils::BufferedSerial)                            == BOTANIK_SIZE( 4416,  4392), "BufferedSerial footprint changed");
    static_assert(sizeof(Terminal::TextEditor)                             == BOTANIK_SIZE(  984,   972), "TextEditor footprint changed");

    // Pages
    static_assert(sizeof(Pages::MetricPage)                                == BOTANIK_SIZE( 1240,  1224), "MetricPage footprint changed");
    static_assert(sizeof(Pages::ShellPage)                                 == BOTANIK_SIZE(19968, 19868), "ShellPage footprint changed");
    static_assert(sizeof(Pages::LogsPage)                                  == BOTANIK_SIZE(25432, 25404), "LogsPage footprint changed");
    static_assert(sizeof(Pages::HomePage<ProbePlcOut>)                     == BOTANIK_SIZE(11696, 11656), "HomePage footprint changed");
    static_assert(sizeof(Pages::StatusPage<ProbePlcOut>)                   == BOTANIK_SIZE( 1248,  1232), "StatusPage footprint changed");
    static_assert(sizeof(Pages::ShellScrollback)                           == BOTANIK_SIZE( 8276,  8276), "ShellScrollback footprint changed");
    static_assert(sizeof(Pages::LogsScrollback)                            == BOTANIK_SIZE(16532, 16532), "LogsScrollback footprint changed");
    static_assert(sizeof(Pages::HomeScrollback)                            == BOTANIK_SIZE( 1568,  1568), "HomeScrollback footprint changed");

#undef BOTANIK_SIZE

}}

#endif // LIBBOTANIK_SIZES_H
