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

// Per-target footprint, keyed off TARGET_SIMULATOR (the hardware macro is
// confined to the HAL, so invert: absent simulator = OPTA-class/ARM build).
#if !defined(TARGET_SIMULATOR)
#  define BOTANIK_SIZE(host_, opta_) (opta_)
#else
#  define BOTANIK_SIZE(host_, opta_) (host_)
#endif

    static_assert(sizeof(Logs::LogEntry)                                == BOTANIK_SIZE(  272,   272), "Logs::LogEntry footprint changed");
    // PlcEmulator{,Context} pins live with the emulator in botanik_sim/emulator.cpp.

    // Logging
    static_assert(sizeof(Logs::BufferedLogWriter)                          == BOTANIK_SIZE( 9000,  8992), "BufferedLogWriter footprint changed");
    static_assert(sizeof(Logs::SerialLogWriter)                            == BOTANIK_SIZE( 9056,  9016), "SerialLogWriter footprint changed");
    static_assert(sizeof(Logs::Logger)                                     == BOTANIK_SIZE(  280,   216), "Logger footprint changed");

    // Terminal / serial
    static_assert(sizeof(Utils::BufferedSerial)                            == BOTANIK_SIZE( 4424,  4396), "BufferedSerial footprint changed");
    static_assert(sizeof(Terminal::TextEditor)                             == BOTANIK_SIZE(  984,   976), "TextEditor footprint changed");

    // Pages
    static_assert(sizeof(Pages::MetricPage)                                == BOTANIK_SIZE( 1096,  1080), "MetricPage footprint changed");
    static_assert(sizeof(Pages::ShellPage)                                 == BOTANIK_SIZE(20104, 20008), "ShellPage footprint changed");
    static_assert(sizeof(Pages::LogsPage)                                  == BOTANIK_SIZE(25560, 25536), "LogsPage footprint changed");
    static_assert(sizeof(Pages::HomePage)                                  == BOTANIK_SIZE(11688, 11648), "HomePage footprint changed");
    static_assert(sizeof(Pages::ShellScrollback)                           == BOTANIK_SIZE( 8272,  8272), "ShellScrollback footprint changed");
    static_assert(sizeof(Pages::LogsScrollback)                            == BOTANIK_SIZE(16528, 16528), "LogsScrollback footprint changed");
    static_assert(sizeof(Pages::HomeScrollback)                            == BOTANIK_SIZE( 1564,  1564), "HomeScrollback footprint changed");

#undef BOTANIK_SIZE

}}

#endif // LIBBOTANIK_SIZES_H
