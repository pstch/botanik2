#ifndef LIBBOTANIK_CONFIG_IDENTITY_H
#define LIBBOTANIK_CONFIG_IDENTITY_H

/** @file identity.h
 *  @brief Project identity: what this build calls itself. */

#include "libbotanik/config/base.h"

// Defined and undefined per file, not in base.h: see base.h.
#define BOTANIK_CFG(type, name) static constexpr type name = BOTANIK_##name

namespace Botanik { namespace Config {

    /** @name Project identity
     *
     *  Who this unit is, in the three places it says so: the boot banner, the
     *  home screen, and the root of every MQTT topic it publishes.
     *  @{ */

    /** @brief Firmware version string, surfaced in the boot banner (hardware
     *  half comes from HAL::hardware_description()).
     *  @note Doxyfile's PROJECT_NUMBER quotes this; move the two together. */
#ifndef BOTANIK_FIRMWARE_VERSION
#  define BOTANIK_FIRMWARE_VERSION "0.13.12-dev"
#endif
    BOTANIK_CFG(const char*, FIRMWARE_VERSION);

    /** @brief Human-readable site (greenhouse) name, surfaced in the version
     *  command, home screen, and POST line. Pinned per site (matches the
     *  -DBOTANIK_SITE dir). Defined before the prefix so it can seed its
     *  default. */
#ifndef BOTANIK_SITE_NAME
#  define BOTANIK_SITE_NAME "generic"
#endif
    BOTANIK_CFG(const char*, SITE_NAME);

    /** @brief MQTT topic root shared by the alarm, telemetry, and log topics
     *  ("<prefix>/alarms/...", "<prefix>/<telemetry>/...",
     *  "<prefix>/<logs>/..."). Defaults to "default/<site>"; pinned per site. */
#ifndef BOTANIK_MQTT_TOPIC_PREFIX
#  define BOTANIK_MQTT_TOPIC_PREFIX "default/" BOTANIK_SITE_NAME
#endif
    BOTANIK_CFG(const char*, MQTT_TOPIC_PREFIX);

    /** @} */

} }

#undef BOTANIK_CFG

#endif // LIBBOTANIK_CONFIG_IDENTITY_H
