#ifndef PLCIO_CONFIG_H
#define PLCIO_CONFIG_H

/**
 * @file config.h
 * @brief Generic PLC->host configuration shared-memory layout: the network, NTP,
 *        and MQTT settings the PLC publishes for the host's (future) network stack,
 *        NTP client, and MQTT client.
 *
 * Site-independent: this lives in libbotanik, not a site payload. The combined
 * PlcOutput<> (plcio/vars.h) splices this block in ahead of the site `data`, so it
 * is identical on every site and target. Layout must match the PLC IDE's generated
 * config type byte-for-byte (see the PLC-side setup in @ref md_docs_2PLC__CONFIG).
 *
 * Like the other shared structs it is explicitly padded so its layout is identical
 * packed or not, and each block's `serial` leads it (word-aligned). A consumer latches
 * the last serial it applied and re-applies only when the PLC bumps it, so it never
 * re-resolves / re-binds every tick. The shared base Botanik::ConfigHandler
 * (config_handler.h) implements that watch-and-reset for the handlers
 * (Network::NetworkHandler, Network::TimeSyncHandler, the sun-position tracker).
 *
 * Addresses are text (HostStr_t): a hostname or a dotted-decimal IPv4 literal,
 * resolved-or-parsed by the host (a hostname needs the DNS resolver; a literal is
 * parsed directly). The client must not block on this - see the resolver component.
 */

#include <cstdint>
#include <cstddef>   // offsetof

#include "libbotanik_config.h"

namespace Botanik { namespace PlcIO {

    /** @name PLC configuration
     *  @{ */

    static_assert(Config::PLC_HOST_STR_LEN % 4 == 0,
                  "PLC_HOST_STR_LEN must be a 4-byte multiple (keeps the config blocks padding-free)");

    /** @brief A network address as text: a hostname or a dotted-decimal IPv4 literal,
     *  NUL-terminated. Empty ([0]==0) means "unset". */
    typedef char HostStr_t[Config::PLC_HOST_STR_LEN];

    /** @brief Network-interface configuration (PLC -> host). */
    struct PlcNetConfig_t {
        uint32_t  serial;        ///< bumped by the PLC on any change to this block
        uint8_t   dhcp;          ///< 1 = DHCP (ignore the static fields below), 0 = static
        uint8_t   _pad[3];       ///< pad the flag up to the address buffers
        HostStr_t ip;            ///< static IPv4 literal, used when dhcp == 0
        HostStr_t netmask;       ///< static netmask literal (dotted-decimal)
        HostStr_t gateway;       ///< default-gateway literal
        HostStr_t dns1;          ///< primary DNS server (the resolver's upstream)
        HostStr_t dns2;          ///< secondary DNS server, empty = none
    };

    /** @brief NTP-client configuration (PLC -> host). */
    struct PlcNtpConfig_t {
        uint32_t  serial;        ///< bumped by the PLC on any change to this block
        uint8_t   enabled;       ///< 1 = run the NTP client and discipline the RTC
        uint8_t   _pad;          ///< pad to the 16-bit field's alignment
        uint16_t  poll_s;        ///< poll interval, seconds (0 = firmware default)
        HostStr_t server1;       ///< hostname or IPv4 literal
        HostStr_t server2;       ///< empty = none
    };

    /** @brief MQTT-client configuration (PLC -> host). The client id is derived from
     *  Config::SITE_NAME at compile time, so it is not carried on the wire. */
    struct PlcMqttConfig_t {
        uint32_t  serial;        ///< bumped by the PLC on any change to this block
        uint8_t   enabled;       ///< 1 = connect to the broker
        uint8_t   _pad;          ///< pad to the 16-bit fields' alignment
        uint16_t  port;          ///< broker TCP port (0 = firmware default, 1883)
        uint16_t  keepalive_s;   ///< MQTT keepalive, seconds (0 = firmware default)
        uint8_t   _pad2[2];      ///< pad the block up to the address buffer
        HostStr_t broker;        ///< hostname or IPv4 literal
    };

    /** @brief Site geographical location (PLC -> host). Feeds the base library's
     *  sunrise/sunset computation. Integer-scaled - no floats on the wire.
     *  @note No timezone field: the device works in UTC throughout (the RTC is UTC from
     *  NTP, the solar math is timezone-independent). Any local-time presentation belongs
     *  off-device, where a real tz database is available. */
    struct PlcGeoConfig_t {
        uint32_t serial;        ///< bumped by the PLC on any change to this block
        int32_t  latitude_e7;   ///< latitude, degrees * 1e7 (north +, south -); -90e7..90e7
        int32_t  longitude_e7;  ///< longitude, degrees * 1e7 (east +, west -); -180e7..180e7
    };

    /** @brief All generic PLC->host configuration, spliced into PlcOutput ahead of
     *  the site payload. Site-independent: the same layout on every site / target. */
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
