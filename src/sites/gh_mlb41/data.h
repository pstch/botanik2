#ifndef SITE_PARAMS_IMAGE_H
#define SITE_PARAMS_IMAGE_H

/**
 * @file data.h
 * @brief GENERATED - the gh_mlb41 parameter image: the struct the config
 *        parameters are laid out in, and its asserts.
 *
 * Generated from the Arduino PLC IDE project. Regenerating overwrites this
 * file: hand-written site code belongs in site.h / commands.h / sim.h.
 *
 * This is all that is left of the PLC payload. Status variables, actuators and
 * alarm conditions are variables now (io.h / status.h), addressed by table and
 * index; only the config parameters still live in a byte image, and this is its
 * layout. Field ORDER is therefore load-bearing for params.h alone.
 *
 * Real-valued quantities are floats in native units, and members are ordered
 * widest-first and explicitly padded so the struct carries no implicit padding.
 */

#include <cstdint>

#include "botanik.h"

/** @name Parameter image
 *  @{ */

/** @brief Config parameters (network / NTP / MQTT / geo), embedded verbatim in
 *  both the input payload (host stages edits at in_offset) and the output
 *  payload (PLC reflects the live values at out_offset), so a parameter's
 *  in_offset and out_offset are genuinely distinct. Each group's commit serial
 *  (@c net / @c ntp / @c mqtt / @c geo) is an ordinary u32 field the user bumps
 *  on the params page to commit that group's staged edits; the PLC latches the
 *  group and reflects the serial back, which the consuming Component watches.
 *  Ordered widest-first (and _pad'd) so the sub-struct - and both payloads that
 *  embed it - stay 4-byte-aligned and padding-free. */
struct GreenhouseConfig_t {
    static const int STRLEN = (int)Botanik::Config::PLC_HOST_STR_LEN;
    // 4-byte: geo values + the per-group commit serials
    float    geo_latitude;       ///< site latitude, decimal degrees
    float    geo_longitude;      ///< site longitude, decimal degrees
    uint32_t net;                ///< network group commit serial (bump to apply)
    uint32_t ntp;                ///< NTP group commit serial
    uint32_t mqtt;               ///< MQTT group commit serial
    uint32_t geo;                ///< geo group commit serial
    // 2-byte
    uint16_t ntp_poll_s;         ///< NTP poll interval, s (0 = firmware default)
    uint16_t mqtt_port;          ///< broker TCP port (0 = firmware default)
    uint16_t mqtt_keepalive_s;   ///< MQTT keepalive, s (0 = firmware default)
    // 1-byte flags
    uint8_t  net_dhcp;           ///< DHCP (1) or static IP (0)
    uint8_t  ntp_enabled;        ///< enable NTP time sync
    uint8_t  mqtt_enabled;       ///< enable MQTT publishing
    uint8_t  _pad[3];            ///< align the string block (and total) to 4
    // strings (each PLC_HOST_STR_LEN, a 4-byte multiple)
    char     net_ip[STRLEN];     ///< static IPv4, used when net_dhcp == 0
    char     net_netmask[STRLEN];
    char     net_gateway[STRLEN];
    char     net_dns1[STRLEN];
    char     net_dns2[STRLEN];   ///< empty = none
    char     ntp_server1[STRLEN];
    char     ntp_server2[STRLEN];///< empty = none
    char     mqtt_broker[STRLEN];
};

/** @} */

/** @name Layout asserts
 *  The parameter image's contract, checked at compile time: offsets are 16-bit,
 *  and the local parameter store's buffer must cover the whole block.
 *  @{ */

static_assert(sizeof(GreenhouseConfig_t) % 4 == 0,
              "GreenhouseConfig_t must be 4-byte aligned");
static_assert(sizeof(GreenhouseConfig_t) <= 32767,
              "GreenhouseConfig_t exceeds the parameter offset range");

/** @} */

#endif // SITE_PARAMS_IMAGE_H
