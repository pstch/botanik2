#ifndef PLCIO_CONFIG_H
#define PLCIO_CONFIG_H

/**
 * @file config.h
 * @brief Generic PLC->host config shared-memory layout (network/NTP/MQTT/geo).
 *
 * Site-independent: lives in libbotanik, spliced into PlcOutput<> ahead of the site
 * `data`. Layout must match the PLC IDE's generated config type byte-for-byte (see
 * @ref md_docs_2PLC__CONFIG). Explicitly padded (layout identical packed or not), and
 * each block's `serial` leads it (word-aligned) so a consumer can poll it through a
 * uint32_t* and re-apply only when the PLC bumps it (watch-and-reset in ConfigHandler).
 */

#include <cstdint>
#include <cstddef>   // offsetof

#include "libbotanik_config.h"

namespace Botanik { namespace PlcIO {

    /** @name PLC configuration
     *  @{ */

    static_assert(Config::PLC_HOST_STR_LEN % 4 == 0,
                  "PLC_HOST_STR_LEN must be a 4-byte multiple (keeps the config blocks padding-free)");

    /** @brief A network address as text: hostname or dotted-decimal IPv4 literal,
     *  NUL-terminated. Empty ([0]==0) means "unset". */
    typedef char HostStr_t[Config::PLC_HOST_STR_LEN];

    /** @brief Network-interface configuration (PLC -> host). */
    struct PlcNetConfig_t {
        uint32_t  serial;        ///< bumped by the PLC on any change to this block
        uint8_t   dhcp;          ///< 1 = DHCP (ignore static fields below), 0 = static
        uint8_t   _pad[3];
        HostStr_t ip;            ///< static IPv4 literal, used when dhcp == 0
        HostStr_t netmask;
        HostStr_t gateway;
        HostStr_t dns1;
        HostStr_t dns2;          ///< empty = none
    };

    /** @brief NTP-client configuration (PLC -> host). */
    struct PlcNtpConfig_t {
        uint32_t  serial;        ///< bumped by the PLC on any change to this block
        uint8_t   enabled;
        uint8_t   _pad;
        uint16_t  poll_s;        ///< poll interval, seconds (0 = firmware default)
        HostStr_t server1;
        HostStr_t server2;       ///< empty = none
    };

    /** @brief MQTT-client configuration (PLC -> host). Client id is derived from
     *  Config::SITE_NAME at compile time, so it is not carried on the wire. */
    struct PlcMqttConfig_t {
        uint32_t  serial;        ///< bumped by the PLC on any change to this block
        uint8_t   enabled;
        uint8_t   _pad;
        uint16_t  port;          ///< broker TCP port (0 = firmware default, 1883)
        uint16_t  keepalive_s;   ///< seconds (0 = firmware default)
        uint8_t   _pad2[2];
        HostStr_t broker;
    };

    /** @brief Site location (PLC -> host); feeds the sunrise/sunset math. Integer-scaled,
     *  no floats on the wire.
     *  @note No timezone field: the device works in UTC throughout (RTC is UTC from NTP,
     *  the solar math is timezone-independent). Local-time presentation belongs off-device. */
    struct PlcGeoConfig_t {
        uint32_t serial;        ///< bumped by the PLC on any change to this block
        int32_t  latitude_e7;   ///< degrees * 1e7 (north +, south -); -90e7..90e7
        int32_t  longitude_e7;  ///< degrees * 1e7 (east +, west -); -180e7..180e7
    };

    /** @brief All generic PLC->host config, spliced into PlcOutput ahead of the site
     *  payload. Same layout on every site / target. */
    struct PlcOutputConfig_t {
        PlcNetConfig_t  net;
        PlcNtpConfig_t  ntp;
        PlcMqttConfig_t mqtt;
        PlcGeoConfig_t  geo;
    };

    // Each block's serial leads it, so a consumer can poll it through a word-aligned
    // uint32_t*; and each block is a 4-byte multiple, so the combined PlcOutput<> has
    // no implicit padding (the byte-for-byte contract in vars.h). The PLC IDE's
    // generated config type must mirror this exactly.
    static_assert(offsetof(PlcNetConfig_t,  serial) == 0, "serial must lead PlcNetConfig_t");
    static_assert(offsetof(PlcNtpConfig_t,  serial) == 0, "serial must lead PlcNtpConfig_t");
    static_assert(offsetof(PlcMqttConfig_t, serial) == 0, "serial must lead PlcMqttConfig_t");
    static_assert(offsetof(PlcGeoConfig_t,  serial) == 0, "serial must lead PlcGeoConfig_t");
    static_assert(sizeof(PlcNetConfig_t)    % 4 == 0, "PlcNetConfig_t must be 4-byte aligned");
    static_assert(sizeof(PlcNtpConfig_t)    % 4 == 0, "PlcNtpConfig_t must be 4-byte aligned");
    static_assert(sizeof(PlcMqttConfig_t)   % 4 == 0, "PlcMqttConfig_t must be 4-byte aligned");
    static_assert(sizeof(PlcGeoConfig_t)    % 4 == 0, "PlcGeoConfig_t must be 4-byte aligned");
    static_assert(sizeof(PlcOutputConfig_t) % 4 == 0, "PlcOutputConfig_t must be 4-byte aligned");

    /** @} */

}}

#endif // PLCIO_CONFIG_H
