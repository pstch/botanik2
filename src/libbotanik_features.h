#ifndef LIBBOTANIK_FEATURES_H
#define LIBBOTANIK_FEATURES_H

/**
 * @file libbotanik_features.h
 * @brief Compile-time feature flags for the Botanik library.
 *
 * Each `BOTANIK_FEATURE_<X>` toggles one optional subsystem. Pinned the same
 * way as config values:
 *  - CMake `-DBOTANIK_FEATURE_PLC=OFF` -> a PUBLIC `=0` define on the `botanik`
 *    library (build-wide, an ABI contract, exactly like BOTANIK_SITE_CONFIG).
 *  - Site `#define BOTANIK_FEATURE_PLC 0` in `sites/<name>/config.h` (first).
 *
 * Gate code with the preprocessor `#if` to drop member decls / ctor-init /
 * loop steps (C++11 has no `if constexpr`, so a `constexpr bool` can't remove
 * a member).
 *
 * @section feature_deps Dependency rules
 * The `#error` guards below fail the build loudly rather than silently
 * miscompiling:
 *  - NTP and MQTT need NETWORK (they run over the network stack).
 *  - NETWORK sources its config from the PLC config block, so it needs PLC
 *    until a config.h fallback lands.
 */

/** @brief PLC coprocessor link: PlcSyncHandler, PlcLogReader, the PLC-fault
 *  watchdog escalation, and the shared-memory payload. Off -> single-core. */
#ifndef BOTANIK_FEATURE_PLC
#  define BOTANIK_FEATURE_PLC 1
#endif

/** @brief Network stack (NetworkHandler / NameResolver). Reserved, default OFF
 *  (declared but not yet instantiated; off keeps a PLC-less build a single
 *  flag). */
#ifndef BOTANIK_FEATURE_NETWORK
#  define BOTANIK_FEATURE_NETWORK 0
#endif

/** @brief NTP time sync (NtpClient). Reserved, default OFF; requires
 *  NETWORK. */
#ifndef BOTANIK_FEATURE_NTP
#  define BOTANIK_FEATURE_NTP 0
#endif

/** @brief MQTT client + alarm notifier. Reserved, default OFF; requires
 *  NETWORK. */
#ifndef BOTANIK_FEATURE_MQTT
#  define BOTANIK_FEATURE_MQTT 0
#endif

/** @brief Sun tracking: the SunPositionTracker (geo-config-driven sunrise /
 *  sunset + day/night) and the solar-position math (sun/). Standalone - not
 *  instantiated by BotanikFirmware, so a site opts in by constructing one.
 *  Default ON (preserves the always-built behaviour); -DBOTANIK_FEATURE_SUN=OFF
 *  drops the sun/ sources and their tests. */
#ifndef BOTANIK_FEATURE_SUN
#  define BOTANIK_FEATURE_SUN 1
#endif

/** @brief ANSI terminal UI: AnsiTerminal, the built-in pages, the console UART,
 *  and the register_pages()/register_commands() hooks. Off -> headless; the
 *  logger, scheduler, health/alarm model and PLC link remain, diagnostics reach
 *  MONITOR via the SerialLogWriter. */
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

#endif // LIBBOTANIK_FEATURES_H
