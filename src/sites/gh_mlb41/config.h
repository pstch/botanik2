#ifndef SITES_GH_MLB41_CONFIG_H
#define SITES_GH_MLB41_CONFIG_H

/**
 * @file config.h
 * @brief gh_mlb41 overrides for libbotanik_config.h.
 *
 * #define any BOTANIK_<NAME> here; CMake includes this (build-wide, PUBLIC on the
 * botanik library) before libbotanik_config.h's #ifndef defaults, so the value wins
 * for libbotanik and every consumer alike. Only override what this site needs.
 */

// MQTT topic namespace for this greenhouse's alarm notifications.
#define BOTANIK_MQTT_TOPIC_PREFIX "botanik/greenhouse"

#endif // SITES_GH_MLB41_CONFIG_H
