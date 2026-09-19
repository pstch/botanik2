#ifndef LIBBOTANIK_SIZES_H
#define LIBBOTANIK_SIZES_H

/**
 * @file libbotanik_sizes.h
 * @brief Memory-footprint contract - static_asserts pinning the sizes of the
 *        long-lived objects. Included at the end of libbotanik.h.
 *
 * This answers WHICH TYPE grew; the linker's map answers whether the IMAGE
 * did. Both are wanted: an aggregate that moved names no cause, and a type
 * that moved says nothing about the headroom left.
 */

#include "libbotanik.h"

// Only for the site that HAS a contract. Every size here depends on the site's
// config - Vars::Bank alone is sized by VAR_MAX, STR_POOL_BYTES and the two
// ANALOG_*_MAX - so a number pinned for the reference site is simply wrong for
// another, and `sites/bare` exists precisely to be different. The reference site
// opts in from its config.h; nothing else pays.
#if defined(BOTANIK_PIN_SIZES) && BOTANIK_PIN_SIZES

namespace Botanik { namespace detail {

    // Figures differ by word size - host x86-64 against OPTA (Cortex-M7,
    // 32-bit) - so each is spelled per target.
#if defined(TARGET_OPTA)
#  define BOTANIK_SIZE(host_, opta_) (opta_)
#else
#  define BOTANIK_SIZE(host_, opta_) (host_)
#endif

    // Scheduling and lifecycle
    static_assert(sizeof(Tasks::Registry)          == BOTANIK_SIZE(  5640,   4616), "Tasks::Registry footprint changed");
    static_assert(sizeof(Scheduler::LoopScheduler) == BOTANIK_SIZE(   320,    312), "LoopScheduler footprint changed");

    // Logging and health
    static_assert(sizeof(Logs::Logger)             == BOTANIK_SIZE(   288,    240), "Logs::Logger footprint changed");
    static_assert(sizeof(Health::HealthMonitor)    == BOTANIK_SIZE(  3672,   3560), "HealthMonitor footprint changed");
    static_assert(sizeof(Alarms::AlarmController)  == BOTANIK_SIZE(  3248,   3208), "AlarmController footprint changed");

    // Serial
    static_assert(sizeof(Utils::BufferedSerial)    == BOTANIK_SIZE(  4384,   4376), "BufferedSerial footprint changed");

    /** The single largest library object, and the one a site's config moves:
     *  VAR_MAX drives four arrays inside it. */
    static_assert(sizeof(Vars::Bank)               == BOTANIK_SIZE( 16312,  16288), "Vars::Bank footprint changed");
    static_assert(sizeof(Vars::ConfigBinder)       == BOTANIK_SIZE(   568,    348), "ConfigBinder footprint changed");

#if BOTANIK_FEATURE_CONSOLE
    static_assert(sizeof(Terminal::AnsiTerminal)   == BOTANIK_SIZE(  8600,   8596), "AnsiTerminal footprint changed");
    static_assert(sizeof(Terminal::Console)        == BOTANIK_SIZE(  9224,   9112), "Console footprint changed");
    static_assert(sizeof(Terminal::TextEditor)     == BOTANIK_SIZE(   120,    100), "TextEditor footprint changed");
#endif
#if BOTANIK_FEATURE_SHELL_PAGE
    static_assert(sizeof(Pages::ShellPage)         == BOTANIK_SIZE( 14000,  13888), "ShellPage footprint changed");
#endif
#if BOTANIK_FEATURE_LOGS_PAGE
    static_assert(sizeof(Pages::LogsPage)          == BOTANIK_SIZE( 18112,  18072), "LogsPage footprint changed");
#endif
#if BOTANIK_FEATURE_HOME_PAGE
    static_assert(sizeof(Pages::HomePage)          == BOTANIK_SIZE(  3488,   3432), "HomePage footprint changed");
#endif

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
    static_assert(sizeof(Modbus::RtuMaster)        == BOTANIK_SIZE(  2832,   2792), "RtuMaster footprint changed");
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
    static_assert(sizeof(Modbus::TcpSlave)         == BOTANIK_SIZE(  3664,   3608), "TcpSlave footprint changed");
#endif

    /** The aggregate: every flag above moves it, which is why it is pinned only
     *  under the same opt-in. It is the firmware singleton's whole `.bss`
     *  footprint, so it can be read against the linker's map for the
     *  image. */
    static_assert(sizeof(BotanikFirmware)          == BOTANIK_SIZE(109096, 106608), "BotanikFirmware footprint changed");

#undef BOTANIK_SIZE

}}  // namespace Botanik::detail

#endif // BOTANIK_PIN_SIZES

#endif // LIBBOTANIK_SIZES_H
