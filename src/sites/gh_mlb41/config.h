#ifndef SITES_GH_MLB41_CONFIG_H
#define SITES_GH_MLB41_CONFIG_H

/**
 * @file config.h
 * @brief gh_mlb41 overrides for libbotanik_config.h.
 *
 * CMake includes this (build-wide, PUBLIC on the botanik library) before
 * libbotanik_config.h's `#ifndef` defaults, so a `#define BOTANIK_<NAME>` here
 * wins for libbotanik and every consumer alike.
 */

#define BOTANIK_MQTT_TOPIC_PREFIX "botanik/greenhouse"
#define BOTANIK_SITE_NAME "gh_mlb41"

// This greenhouse's PLC publishes the net/NTP/MQTT config blocks, so enable the
// stack. Functional on both targets: OPTA over Mbed sockets, the simulator over
// real host BSD sockets (HAL::Net), so the sim genuinely brings up the link,
// syncs time, and connects.
#define BOTANIK_FEATURE_NETWORK 1
#define BOTANIK_FEATURE_NTP     1
#define BOTANIK_FEATURE_MQTT    1

#endif // SITES_GH_MLB41_CONFIG_H
