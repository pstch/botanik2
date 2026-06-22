#ifndef LIBBOTANIK_FEATURES_H
#define LIBBOTANIK_FEATURES_H

/**
 * @file libbotanik_features.h
 * @brief Compile-time feature flags for the Botanik library.
 *
 * libbotanik is generic embedded-firmware machinery; not every firmware wants
 * every subsystem. Each `BOTANIK_FEATURE_<X>` below toggles one optional
 * subsystem and defaults to 1 (on) so existing builds are unchanged. A site or
 * the build pins a different value the same way config values are overridden:
 *
 *  - CMake: `-DBOTANIK_FEATURE_PLC=OFF` -> a PUBLIC `BOTANIK_FEATURE_PLC=0`
 *    define on the `botanik` library (build-wide, so every TU agrees - an ABI
 *    contract, exactly like BOTANIK_SITE_CONFIG).
 *  - Site:  `#define BOTANIK_FEATURE_PLC 0` in `sites/<name>/config.h`, which
 *    libbotanik_config.h includes before this header.
 *
 * Gate code two ways. Use the preprocessor `#if BOTANIK_FEATURE_PLC` to drop
 * member declarations / ctor-init entries / loop steps (a `constexpr bool`
 * cannot remove a member, and C++11 has no `if constexpr`). Use the
 * `Botanik::Features::PLC` constexpr mirror in expression context
 * (static_assert, a ternary whose arms both compile).
 *
 * @section feature_deps Dependency rules
 * Some features build on others; the `#error` guards below fail the build loudly
 * rather than silently miscompiling:
 *  - NTP and MQTT need NETWORK (they run over the network stack).
 *  - NETWORK currently sources its config (broker, NTP servers, DHCP/static)
 *    from the PLC config block, so it needs PLC until a config.h fallback lands.
 */

/** @brief PLC coprocessor link: PlcSyncHandler, PlcLogReader, the PLC-fault
 *  watchdog escalation, and the shared-memory payload threaded through the
 *  firmware. Off -> a single-core firmware with no second core to talk to. */
#ifndef BOTANIK_FEATURE_PLC
#  define BOTANIK_FEATURE_PLC 1
#endif

/** @brief Network stack (NetworkHandler / NameResolver). Reserved and default
 *  OFF: declared but not yet instantiated by the firmware, so toggling it has no
 *  effect today. Defaulting it off keeps a PLC-less build a single flag (it would
 *  otherwise pull in the PLC dependency below). */
#ifndef BOTANIK_FEATURE_NETWORK
#  define BOTANIK_FEATURE_NETWORK 0
#endif

/** @brief NTP time sync (TimeSyncHandler). Reserved, default OFF; requires NETWORK. */
#ifndef BOTANIK_FEATURE_NTP
#  define BOTANIK_FEATURE_NTP 0
#endif

/** @brief MQTT client + alarm notifier. Reserved, default OFF; requires NETWORK. */
#ifndef BOTANIK_FEATURE_MQTT
#  define BOTANIK_FEATURE_MQTT 0
#endif

/** @brief ANSI terminal UI: AnsiTerminal, the built-in shell/logs/alarms pages, the
 *  console UART they drive, and the BotanikFirmware register_pages()/
 *  register_commands() hooks. Off -> a headless firmware; the logger, scheduler,
 *  health/alarm model, and PLC link remain, and diagnostics still reach the MONITOR
 *  UART via MonitorLogWriter. */
#ifndef BOTANIK_FEATURE_TERMINAL
#  define BOTANIK_FEATURE_TERMINAL 1
#endif

// --- dependency rules (see file header) ---
#if BOTANIK_FEATURE_NTP && !BOTANIK_FEATURE_NETWORK
#  error "BOTANIK_FEATURE_NTP requires BOTANIK_FEATURE_NETWORK"
#endif
#if BOTANIK_FEATURE_MQTT && !BOTANIK_FEATURE_NETWORK
#  error "BOTANIK_FEATURE_MQTT requires BOTANIK_FEATURE_NETWORK"
#endif
#if BOTANIK_FEATURE_NETWORK && !BOTANIK_FEATURE_PLC
#  error "BOTANIK_FEATURE_NETWORK requires BOTANIK_FEATURE_PLC (network config comes from the PLC block; no config.h fallback yet)"
#endif

namespace Botanik { namespace Features {

    /** @brief constexpr mirrors of the macros, for use in expression context. */
    static constexpr bool PLC      = BOTANIK_FEATURE_PLC;
    static constexpr bool NETWORK  = BOTANIK_FEATURE_NETWORK;
    static constexpr bool NTP      = BOTANIK_FEATURE_NTP;
    static constexpr bool MQTT     = BOTANIK_FEATURE_MQTT;
    static constexpr bool TERMINAL = BOTANIK_FEATURE_TERMINAL;

} }

#endif // LIBBOTANIK_FEATURES_H
