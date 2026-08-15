#ifndef LIBBOTANIK_CONFIG_NET_H
#define LIBBOTANIK_CONFIG_NET_H

/** @file net.h
 *  @brief The transport, name resolution, NTP and MQTT.
 *
 *  One concern of the library's configuration. Every value defaults from
 *  an #ifndef-guarded BOTANIK_* macro a site may override; see
 *  libbotanik_config.h, which aggregates these and holds the rules. */

#include "libbotanik/config/base.h"

// Defined and undefined per file, not in base.h: see base.h.
#define BOTANIK_CFG(type, name) static constexpr type name = BOTANIK_##name

namespace Botanik { namespace Config {

    /** @name Network transport
     *  @{ */

    /** @brief Char-buffer length for a network address (hostname or IPv4).
     *  @note Matches the site's STR_CAP for the mqtt_host / ntp_srv* rows. */
#ifndef BOTANIK_HOST_STR_LEN
#  define BOTANIK_HOST_STR_LEN 64
#endif
    BOTANIK_CFG(size_t, HOST_STR_LEN);

    /** @brief Sockets HAL::Network keeps, per kind. These are the hard ceiling
     *  on how many network clients a site may run at once: NTP takes one UDP,
     *  MQTT one TCP, so the defaults leave nothing spare - a second publisher or
     *  a syslog client needs this raised, and gets a refused socket otherwise. */
#ifndef BOTANIK_NET_MAX_UDP
#  define BOTANIK_NET_MAX_UDP 2
#endif
    BOTANIK_CFG(int, NET_MAX_UDP);
#ifndef BOTANIK_NET_MAX_TCP
#  define BOTANIK_NET_MAX_TCP 2
#endif
    BOTANIK_CFG(int, NET_MAX_TCP);

    /** @brief Max time spent waiting for the link to come up. */
#ifndef BOTANIK_NET_SETUP_TIMEOUT_MS
#  define BOTANIK_NET_SETUP_TIMEOUT_MS 30000
#endif
    BOTANIK_CFG(uint32_t, NET_SETUP_TIMEOUT_MS);

    /** @brief Backoff after a failed (or lost) network bring-up before a retry. */
#ifndef BOTANIK_NET_RETRY_MS
#  define BOTANIK_NET_RETRY_MS 10000
#endif
    BOTANIK_CFG(uint32_t, NET_RETRY_MS);

    /** @} */

    /** @name Name resolution
     *  @{ */

    /** @brief Size of the DNS resolver cache size.
     *  @note Also determines the maximum concurrent requests supported. */
#ifndef BOTANIK_DNS_CACHE_SIZE
#  define BOTANIK_DNS_CACHE_SIZE 8
#endif
    BOTANIK_CFG(size_t, DNS_CACHE_SIZE);

    /** @brief Async DNS lookups the HAL can have in flight. Defaults to
     *  DNS_CACHE_SIZE because that is how many rows NameResolver can leave
     *  PENDING at once (_alloc() handles every row being in flight), and a
     *  shortfall shows up as lookups that fail only once enough are concurrent.
     *  hal/net.cpp static_asserts the relation rather than trusting it.
     *
     *  Separate from DNS_CACHE_SIZE because they sit on opposite sides of the
     *  HAL boundary: a build with a second resolver needs more HAL slots than
     *  the library keeps cache rows. */
#ifndef BOTANIK_NET_MAX_DNS
#  define BOTANIK_NET_MAX_DNS BOTANIK_DNS_CACHE_SIZE
#endif
    BOTANIK_CFG(int, NET_MAX_DNS);

    /** @brief Max age (ms) a resolved entry is served from the cache before it is
     *  re-resolved. The HAL DNS API exposes no record TTL, so this bounds
     *  staleness (e.g. a server whose address changes) without it. 0 disables
     *  expiry (cache until evicted / cleared on reconfig). Default 1 h. */
#ifndef BOTANIK_DNS_CACHE_TTL_MS
#  define BOTANIK_DNS_CACHE_TTL_MS 3600000u
#endif
    BOTANIK_CFG(uint32_t, DNS_CACHE_TTL_MS);

    /** @brief Max time (ms) a row stays in flight before _alloc() may cancel and
     *  reclaim it. A caller that abandons a lookup (a group serial bumps mid
     *  resolve) never frees its row, so without this the table leaks a slot per
     *  abandonment until every row is PENDING and resolution fails for good. */
#ifndef BOTANIK_DNS_PENDING_MAX_MS
#  define BOTANIK_DNS_PENDING_MAX_MS 30000u
#endif
    BOTANIK_CFG(uint32_t, DNS_PENDING_MAX_MS);

    /** @} */

    /** @name Time sync (NTP)
     *
     *  @note Only the timings are compile-time. Which servers to ask, whether
     *        to ask at all, and how often are runtime parameters in the site's
     *        variable table (ntp_srv1 / ntp_enable / ntp_poll_sec). The server
     *        port is fixed at the well-known 123 in network/ntp.h.
     *  @{ */

    /** @brief How long the SNTP client waits before giving up on a server. */
#ifndef BOTANIK_NTP_QUERY_TIMEOUT_MS
#  define BOTANIK_NTP_QUERY_TIMEOUT_MS 5000
#endif
    BOTANIK_CFG(uint32_t, NTP_QUERY_TIMEOUT_MS);

    /** @brief Backoff after a failed NTP sync before retrying. */
#ifndef BOTANIK_NTP_RETRY_MS
#  define BOTANIK_NTP_RETRY_MS 60000
#endif
    BOTANIK_CFG(uint32_t, NTP_RETRY_MS);

    /** @} */

    /** @name MQTT client
     *
     *  @note The broker host and port are runtime parameters (mqtt_host /
     *        mqtt_port); what is here is the client's own sizing and cadence,
     *        plus the topic schema hanging off MQTT_TOPIC_PREFIX.
     *  @{ */

    /** @brief Max time to establish the TCP/MQTT session. */
#ifndef BOTANIK_MQTT_CONNECT_TIMEOUT_MS
#  define BOTANIK_MQTT_CONNECT_TIMEOUT_MS 10000
#endif
    BOTANIK_CFG(uint32_t, MQTT_CONNECT_TIMEOUT_MS);

    /** @brief Backoff after a failed connection before the MQTT client retries. */
#ifndef BOTANIK_MQTT_RETRY_MS
#  define BOTANIK_MQTT_RETRY_MS 30000
#endif
    BOTANIK_CFG(uint32_t, MQTT_RETRY_MS);

    /** @brief Scratch buffer (bytes) MqttClient builds an outgoing packet in.
     *  Bounds the whole PUBLISH: fixed header + topic + payload. A publish that
     *  would exceed it is refused outright, so it must clear MQTT_TOPIC_LEN plus
     *  the largest payload - which is a formatted log line, not a telemetry
     *  reading. The static_assert below holds that. */
#ifndef BOTANIK_MQTT_TX_BUF
#  define BOTANIK_MQTT_TX_BUF 384
#endif
    BOTANIK_CFG(size_t, MQTT_TX_BUF);

    /** @brief Buffer every publisher formats its topic in. One bound for the
     *  three of them (telemetry, logs, alarms), because all three build
     *  "<MQTT_TOPIC_PREFIX>/<segment>/<name>" from the same configurable prefix
     *  - sized apart, whichever was smallest truncated first. */
#ifndef BOTANIK_MQTT_TOPIC_LEN
#  define BOTANIK_MQTT_TOPIC_LEN 96
#endif
    BOTANIK_CFG(size_t, MQTT_TOPIC_LEN);

    // The check that MQTT_TX_BUF can carry a whole log line lives in
    // network/mqtt_log.h: it relates this buffer to LOG_MESSAGE_LEN, and the
    // MQTT log writer is the one thing that puts the two together.

    /** @brief Buffer a single telemetry value is formatted into; caps the width
     *  of any published reading. */
#ifndef BOTANIK_MQTT_PAYLOAD_LEN
#  define BOTANIK_MQTT_PAYLOAD_LEN 64
#endif
    BOTANIK_CFG(size_t, MQTT_PAYLOAD_LEN);

    /** @brief Socket reads MqttClient drains per activation. The per-beat bound
     *  on its RX work, like MQTT_TELEMETRY_PER_STEP on the TX side. */
#ifndef BOTANIK_MQTT_RX_PER_STEP
#  define BOTANIK_MQTT_RX_PER_STEP 8
#endif
    BOTANIK_CFG(uint8_t, MQTT_RX_PER_STEP);

    /** @brief Period between MQTT telemetry publish cycles (ms).
     *  @note 0 disables telemetry.
     *  @note Each cycle publishes every registered field once. */
#ifndef BOTANIK_MQTT_TELEMETRY_PERIOD_MS
#  define BOTANIK_MQTT_TELEMETRY_PERIOD_MS 30000
#endif
    BOTANIK_CFG(uint32_t, MQTT_TELEMETRY_PERIOD_MS);

    /** @brief Topics published per activation while a telemetry sweep is in
     *  progress. 0 publishes the whole table in one go (the old behaviour).
     *
     *  Each topic is its own snprintf and its own tcp_send (MQTT 3.1.1 has no
     *  multi-topic PUBLISH), so a full sweep in one beat is 38-80 of them,
     *  0.8-1.9 ms against a 10 ms budget. 8 clears a typical table in well under
     *  a second, against a cadence measured in tens of seconds. */
#ifndef BOTANIK_MQTT_TELEMETRY_PER_STEP
#  define BOTANIK_MQTT_TELEMETRY_PER_STEP 8
#endif
    BOTANIK_CFG(uint16_t, MQTT_TELEMETRY_PER_STEP);

    /** @brief Publish telemetry with the MQTT retain flag set, so a late
     *  subscriber gets the last value of each field immediately.
     *
     *  @warning An invalid reading is skipped rather than published, and there
     *  is no last will, so with retain on the broker keeps serving the last good
     *  value of a sensor that has since died. Turn it off for an integration
     *  that must be able to tell "current" from "last seen". */
#ifndef BOTANIK_MQTT_TELEMETRY_RETAIN
#  define BOTANIK_MQTT_TELEMETRY_RETAIN 1
#endif
    BOTANIK_CFG(bool, MQTT_TELEMETRY_RETAIN);

    /** @brief Topic segment between the prefix and the field name, for each of
     *  the three publishers. They are the path schema every subscriber and
     *  dashboard agrees on; MQTT_TOPIC_PREFIX above is what separates one
     *  installation from another.
     *    telemetry: "<prefix>/<TELEMETRY>/<group>/<name>"
     *    logs:      "<prefix>/<LOG>/<level>/<topic>"
     *    alarms:    "<prefix>/<ALARM>/<CODE>" */
#ifndef BOTANIK_MQTT_TELEMETRY_TOPIC
#  define BOTANIK_MQTT_TELEMETRY_TOPIC "status"
#endif
    BOTANIK_CFG(const char*, MQTT_TELEMETRY_TOPIC);
#ifndef BOTANIK_MQTT_LOG_TOPIC
#  define BOTANIK_MQTT_LOG_TOPIC "logs"
#endif
    BOTANIK_CFG(const char*, MQTT_LOG_TOPIC);
#ifndef BOTANIK_MQTT_ALARM_TOPIC
#  define BOTANIK_MQTT_ALARM_TOPIC "alarms"
#endif
    BOTANIK_CFG(const char*, MQTT_ALARM_TOPIC);

    /** @brief Minimum level the MQTT log sink publishes (its own gate, on top of
     *  the logger's).
     *
     *  DEBUG, i.e. everything the logger lets through: the broker is where a
     *  unit's log is read from off-site, and the entries worth having there are
     *  the ones nobody is at the console for. The console sinks keep their own
     *  higher floors, so this does not make the screen noisier.
     *
     *  Raise it per site if a link is too thin: every entry is one QoS-0
     *  PUBLISH, dropped rather than queued while the broker is away. Criticals
     *  always pass. */
#ifndef BOTANIK_MQTT_LOG_MIN_LEVEL
#  define BOTANIK_MQTT_LOG_MIN_LEVEL 1   /* Logs::LOG_DEBUG */
#endif
    BOTANIK_CFG(uint8_t, MQTT_LOG_MIN_LEVEL);

    /** @} */

} }

/** @name Parameter factory defaults
 *
 *  Unlike everything above, these are NOT `Config::` constants: they are the
 *  factory values of the `net_` / `ntp_` / `mqtt_` rows the library declares in
 *  library_vars.h, and the live value lives in the variable table where an
 *  operator can edit and persist it. They sit here so a site pins what its
 *  installation ships with (in `sites/<name>/config.h`) without touching the
 *  table, exactly as BOTANIK_CONSOLE_BAUD does for the console rate.
 *  @{ */

#ifndef BOTANIK_NET_DHCP
#  define BOTANIK_NET_DHCP      0
#endif
#ifndef BOTANIK_NET_IP
#  define BOTANIK_NET_IP        "192.168.1.10"
#endif
#ifndef BOTANIK_NET_MASK
#  define BOTANIK_NET_MASK      "255.255.255.0"
#endif
#ifndef BOTANIK_NET_GW
#  define BOTANIK_NET_GW        "192.168.1.1"
#endif
#ifndef BOTANIK_NET_DNS1
#  define BOTANIK_NET_DNS1      "8.8.8.8"
#endif
#ifndef BOTANIK_NET_DNS2
#  define BOTANIK_NET_DNS2      "4.4.4.4"
#endif

#ifndef BOTANIK_NTP_ENABLE
#  define BOTANIK_NTP_ENABLE    1
#endif
#ifndef BOTANIK_NTP_POLL_SEC
#  define BOTANIK_NTP_POLL_SEC  3600
#endif
#ifndef BOTANIK_NTP_PORT
#  define BOTANIK_NTP_PORT      123
#endif
#ifndef BOTANIK_NTP_SRV1
#  define BOTANIK_NTP_SRV1      "pool.ntp.org"
#endif
#ifndef BOTANIK_NTP_SRV2
#  define BOTANIK_NTP_SRV2      ""
#endif

#ifndef BOTANIK_MQTT_ENABLE
#  define BOTANIK_MQTT_ENABLE   1
#endif
#ifndef BOTANIK_MQTT_HOST
#  define BOTANIK_MQTT_HOST     "192.168.1.11"
#endif
#ifndef BOTANIK_MQTT_PORT
#  define BOTANIK_MQTT_PORT     1883
#endif
#ifndef BOTANIK_MQTT_KA_SEC
#  define BOTANIK_MQTT_KA_SEC   20
#endif

/** @} */

#undef BOTANIK_CFG

#endif // LIBBOTANIK_CONFIG_NET_H
