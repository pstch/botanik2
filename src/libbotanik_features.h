// REVIEW_35: DONE
#ifndef LIBBOTANIK_FEATURES_H
#define LIBBOTANIK_FEATURES_H

/** @file libbotanik_features.h
 *  @brief Compile-time feature flags for the Botanik library.
 *
 *  Each `BOTANIK_FEATURE_<X>` toggles one optional subsystem. They are usually
 *  set in the site-specific config.h (`#define BOTANIK_FEATURE_XXX 0`).
 * 
 *  We then use these preprecessor variables to gate code using `#if`. */

// Features flags
// ===========================================================================

/** @brief PLC coprocessor link. */
#ifndef BOTANIK_FEATURE_PLC
#  define BOTANIK_FEATURE_PLC 1
#endif

/** @brief Network stack (NetworkHandler / NameResolver). */
#ifndef BOTANIK_FEATURE_NETWORK
#  define BOTANIK_FEATURE_NETWORK 0
#endif

/** @brief NTP time sync.
 *  @note Requires BOTANIK_FEATURE_NETWORK */
#ifndef BOTANIK_FEATURE_NTP
#  define BOTANIK_FEATURE_NTP 0
#endif

/** @brief MQTT client & notifier.
 *  @note Requires BOTANIK_FEATURE_NETWORK */
#ifndef BOTANIK_FEATURE_MQTT
#  define BOTANIK_FEATURE_MQTT 0
#endif

/** @brief Sun altitude tracking */
#ifndef BOTANIK_FEATURE_SUN
#  define BOTANIK_FEATURE_SUN 1
#endif

/** @brief Interactive ANSI console. */
#ifndef BOTANIK_FEATURE_CONSOLE
#  define BOTANIK_FEATURE_CONSOLE 1
#endif

/** @brief UART log monitor. */
#ifndef BOTANIK_FEATURE_MONITOR
#  define BOTANIK_FEATURE_MONITOR 1
#endif

// Dependency rules
// ===========================================================================

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
