// REVIEW_35: TODO
#ifndef PLCIO_CONFIG_H
#define PLCIO_CONFIG_H

/**
 * @file config.h
 * @brief Generic PLC->host config shared-memory layout (network/NTP/MQTT/geo).
 *
 * Site-independent: lives in libbotanik, spliced into PlcOutput<> ahead of the
 * site `data`. Layout must match the PLC IDE's generated config type
 * byte-for-byte (see @ref md_docs_2PLC__CONFIG). Explicitly padded (layout
 * identical packed or not).
 *
 * The block is a flat namespace of parameters: every field is named
 * `<group>_<param>` so the parameter name is the same on the C side and the PLC
 * side (the ConfigParam table in core/config.h maps a name to its offset). The
 * only grouping is a per-group `<group>_serial` word the PLC bumps on any change
 * within that group, so a consumer can poll it through a uint32_t* and re-apply
 * only on a real change (watch-and-reset in the config-driven Components). The
 * byte layout is identical to the previous nested (net/ntp/mqtt/geo sub-struct)
 * form - this is a source-level flattening, not a wire change.
 */

#include <cstdint>
#include <cstddef>   // offsetof

#include "libbotanik_config.h"

namespace Botanik { namespace PlcIO {

    /** @name PLC configuration
     *  @{ */

    static_assert(Config::PLC_HOST_STR_LEN % 4 == 0,
                  "PLC_HOST_STR_LEN must be a 4-byte multiple "
                  "(keeps the config blocks padding-free)");

    /** @brief A network address as text: hostname or dotted-decimal IPv4
     *  literal, NUL-terminated. Empty ([0]==0) means "unset". */
    typedef char HostStr_t[Config::PLC_HOST_STR_LEN];

    /** @brief All generic PLC->host config as a flat parameter namespace,
     *  spliced into PlcOutput ahead of the site payload. Same layout on every
     *  site / target. Fields are grouped only by their `<group>_` name prefix
     *  and a shared `<group>_serial`; the ConfigParam table (core/config.h)
     *  addresses each by name + offset. Widest-first within each group, then
     *  explicitly padded to keep the layout packing-invariant. */
    struct PlcOutputConfig_t {
        // -- network --
        uint32_t  net_serial;      ///< bumped by the PLC on any net_* change
        uint8_t   net_dhcp;        ///< 1 = DHCP (ignore static fields), 0 = static
        uint8_t   _net_pad[3];
        HostStr_t net_ip;          ///< static IPv4 literal, used when net_dhcp == 0
        HostStr_t net_netmask;
        HostStr_t net_gateway;
        HostStr_t net_dns1;
        HostStr_t net_dns2;        ///< empty = none

        // -- NTP --
        uint32_t  ntp_serial;      ///< bumped by the PLC on any ntp_* change
        uint8_t   ntp_enabled;
        uint8_t   _ntp_pad;
        uint16_t  ntp_poll_s;      ///< poll interval, seconds (0 = firmware default)
        HostStr_t ntp_server1;
        HostStr_t ntp_server2;     ///< empty = none

        // -- MQTT (client id derived from Config::SITE_NAME, not on the wire) --
        uint32_t  mqtt_serial;     ///< bumped by the PLC on any mqtt_* change
        uint8_t   mqtt_enabled;
        uint8_t   _mqtt_pad;
        uint16_t  mqtt_port;       ///< broker TCP port (0 = firmware default, 1883)
        uint16_t  mqtt_keepalive_s;///< seconds (0 = firmware default)
        uint8_t   _mqtt_pad2[2];
        HostStr_t mqtt_broker;

        // -- geo (feeds the sunrise/sunset math; UTC device, no timezone) --
        uint32_t  geo_serial;      ///< bumped by the PLC on any geo_* change
        int32_t   geo_latitude_e7; ///< degrees * 1e7 (north +, south -); -90e7..90e7
        int32_t   geo_longitude_e7;///< degrees * 1e7 (east +, west -); -180e7..180e7
    };

    // Each group's serial is word-aligned so a consumer can poll it through a
    // uint32_t*, and the whole block is a 4-byte multiple, so the combined
    // PlcOutput<> has no implicit padding (the byte-for-byte contract in
    // vars.h). The PLC IDE's generated config type must mirror this exactly.
    static_assert(offsetof(PlcOutputConfig_t, net_serial)  % 4 == 0, "net_serial must be word-aligned");
    static_assert(offsetof(PlcOutputConfig_t, ntp_serial)  % 4 == 0, "ntp_serial must be word-aligned");
    static_assert(offsetof(PlcOutputConfig_t, mqtt_serial) % 4 == 0, "mqtt_serial must be word-aligned");
    static_assert(offsetof(PlcOutputConfig_t, geo_serial)  % 4 == 0, "geo_serial must be word-aligned");
    static_assert(sizeof(PlcOutputConfig_t) % 4 == 0, "PlcOutputConfig_t must be 4-byte aligned");

    /** @brief Host->PLC config write-back mirror. Same flat layout as the output
     *  block, so the ConfigParam offsets address both. The host writes an edited
     *  parameter's group fields here and bumps that group's `<group>_serial`
     *  (host-owned in this instance); the PLC applies the group to its NVM and
     *  reflects it on the output block's serial (which the consumers watch). */
    typedef PlcOutputConfig_t PlcInputConfig_t;

    /** @} */

}}

#endif // PLCIO_CONFIG_H
