#ifndef LIBBOTANIK_CONFIG_H
#define LIBBOTANIK_CONFIG_H

#include <cstdint>
#include <cstring>

/**
 * @file libbotanik_config.h
 * @brief Global configuration parameters for the Botanik library.
 *
 * Every parameter below is overridable per site. Each value defaults from a
 * `BOTANIK_<NAME>` macro guarded by `#ifndef`, so a site can pin a different
 * value by `#define`-ing that macro before the default is seen. The selected
 * site may ship `src/sites/<name>/config.h` doing exactly that; CMake passes
 * its path as BOTANIK_SITE_CONFIG and includes it below, *PUBLIC on the botanik
 * library* so the override is build-wide - libbotanik's own TUs and every
 * consumer compile against the same values (anything else is an ABI mismatch,
 * e.g. a torn PLC log layout).
 */

#if defined(BOTANIK_SITE_CONFIG)
#  include BOTANIK_SITE_CONFIG   // #define BOTANIK_* before the defaults
#endif

// Feature flags (BOTANIK_FEATURE_*). Included after the site config (so a site
// may pin a flag) and before the value defaults below, so any TU that sees
// Config also sees Features.
#include "libbotanik_features.h"

// Declare a Config value from its #ifndef-guarded BOTANIK_<NAME> default. The
// guard triplet above each call can't be folded in - the preprocessor never
// re-reads a macro's output as a directive, so a macro can't emit #ifndef -
// this only keeps the declaration DRY and the name impossible to mistype.
// #undef'd at end of file.
#define BOTANIK_CFG(type, name) static constexpr type name = BOTANIK_##name

namespace Botanik { namespace Config {

    /**
     * @name Project Identity
     * @{
     */

    /** @brief Firmware version string, surfaced in the boot banner (hardware
     *  half comes from HAL::hardware_description()). */
#ifndef BOTANIK_FIRMWARE_VERSION
#  define BOTANIK_FIRMWARE_VERSION "0.13.12-dev"
#endif
    BOTANIK_CFG(const char*, FIRMWARE_VERSION);

    /** @brief MQTT topic prefix for alarm notifications. Site identity; pinned
     *  per site. */
#ifndef BOTANIK_MQTT_TOPIC_PREFIX
#  define BOTANIK_MQTT_TOPIC_PREFIX "botanik"
#endif
    BOTANIK_CFG(const char*, MQTT_TOPIC_PREFIX);

    /** @brief Human-readable site (greenhouse) name, surfaced in the version
     *  command, home screen, and POST line. Pinned per site (matches the
     *  -DBOTANIK_SITE dir). */
#ifndef BOTANIK_SITE_NAME
#  define BOTANIK_SITE_NAME "generic"
#endif
    BOTANIK_CFG(const char*, SITE_NAME);

    /** @brief Console UART baud (also sizes the cooperative-TX gates). Default
     *  is the OPTA rate; the sim overrides it low to exercise TX pacing. */
#ifndef BOTANIK_CONSOLE_BAUD
#  define BOTANIK_CONSOLE_BAUD 115200
#endif
    BOTANIK_CFG(uint32_t, CONSOLE_BAUD);

    /** @} */


    /**
     * @name Scheduling & performance monitoring
     * @{
     */

    /** @brief Main task execution period (ms) */
#ifndef BOTANIK_TASK_PERIOD_MS
#  define BOTANIK_TASK_PERIOD_MS 10
#endif
    BOTANIK_CFG(uint32_t, TASK_PERIOD_MS);

    /** @brief Hardware watchdog timeout */
#ifndef BOTANIK_HW_WATCHDOG_MS
#  define BOTANIK_HW_WATCHDOG_MS 10000
#endif
    BOTANIK_CFG(uint32_t, HW_WATCHDOG_MS);

#ifdef TARGET_SIMULATOR
    /** @brief CPU cores the simulator pins its two realtime threads to,
     *  mirroring the OPTA's independent cores: the main loop on one, the PLC
     *  emulator on another so they run concurrently instead of contending for a
     *  core. */
#ifndef BOTANIK_SIM_MAIN_CPU_CORE
#  define BOTANIK_SIM_MAIN_CPU_CORE 0
#endif
    BOTANIK_CFG(int, SIM_MAIN_CPU_CORE);
#ifndef BOTANIK_SIM_EMULATOR_CPU_CORE
#  define BOTANIK_SIM_EMULATOR_CPU_CORE 1
#endif
    BOTANIK_CFG(int, SIM_EMULATOR_CPU_CORE);
#endif

    /** @brief Max bytes the simulator paints/scans below the loop frame for the
     *  stack high-water (OPTA reads the RTOS watermark instead, so this is
     *  sim-only). 64 KiB comfortably covers any real firmware stack while
     *  bounding the per-slice paint. */
#ifndef BOTANIK_STACK_SCAN_LIMIT
#  define BOTANIK_STACK_SCAN_LIMIT 65536
#endif
    BOTANIK_CFG(uint32_t, STACK_SCAN_LIMIT);

    /** @brief Byte pattern for stack "painting" (high-water detection) */
#ifndef BOTANIK_STACK_PAINT_BYTE
#  define BOTANIK_STACK_PAINT_BYTE 0xCD
#endif
    BOTANIK_CFG(uint8_t, STACK_PAINT_BYTE);

    /** @brief Sliding window for the "recent peak" CPU/stack metrics, and the
     *  number of rotating buckets approximating it. */
#ifndef BOTANIK_LOAD_METRIC_WINDOW_MS
#  define BOTANIK_LOAD_METRIC_WINDOW_MS 5000
#endif
    BOTANIK_CFG(uint32_t, LOAD_METRIC_WINDOW_MS);
#ifndef BOTANIK_LOAD_METRIC_BUCKETS
#  define BOTANIK_LOAD_METRIC_BUCKETS 10
#endif
    BOTANIK_CFG(uint32_t, LOAD_METRIC_BUCKETS);

    /** @brief LOAD_METRIC_WINDOW_MS expressed in per-tick samples (window
     *  length the rolling-max filters are configured with). */
#ifndef BOTANIK_LOAD_METRIC_WINDOW
#  define BOTANIK_LOAD_METRIC_WINDOW (BOTANIK_LOAD_METRIC_WINDOW_MS / BOTANIK_TASK_PERIOD_MS)
#endif
    BOTANIK_CFG(uint32_t, LOAD_METRIC_WINDOW);

    /** @brief Platform-health alarm thresholds: CPU_LOAD / CPU_JITT fire when
     *  the rolling-peak load / jitter exceeds this percent of the task period;
     *  STACK fires above this percent stack usage. Per-alarm latch is in the
     *  site table (off_delay_s). */
#ifndef BOTANIK_ALARM_CPU_LOAD_PCT
#  define BOTANIK_ALARM_CPU_LOAD_PCT 50
#endif
    BOTANIK_CFG(uint8_t, ALARM_CPU_LOAD_PCT);
#ifndef BOTANIK_ALARM_CPU_JITTER_PCT
#  define BOTANIK_ALARM_CPU_JITTER_PCT 50
#endif
    BOTANIK_CFG(uint8_t, ALARM_CPU_JITTER_PCT);
#ifndef BOTANIK_ALARM_STACK_PCT
#  define BOTANIK_ALARM_STACK_PCT 80
#endif
    BOTANIK_CFG(uint8_t, ALARM_STACK_PCT);
    /** @brief How long a momentary-event alarm (CPU_XRUN, RX_TORN) stays held
     *  active after its counter ticks. HealthMonitor latches this in source (a
     *  proper held state, not a one-beat pulse), independent of the table's
     *  off_delay_s. */
#ifndef BOTANIK_ALARM_EVENT_HOLD_MS
#  define BOTANIK_ALARM_EVENT_HOLD_MS 300000
#endif
    BOTANIK_CFG(uint32_t, ALARM_EVENT_HOLD_MS);

    /** @brief Stack high-water is measured (and, on the sim, the free stack
     *  re-painted) once per slice rather than every tick, to amortise the
     *  paint/scan cost. */
#ifndef BOTANIK_STACK_SLICE_TICKS
#  define BOTANIK_STACK_SLICE_TICKS \
       ((BOTANIK_LOAD_METRIC_WINDOW >= BOTANIK_LOAD_METRIC_BUCKETS) \
            ? (BOTANIK_LOAD_METRIC_WINDOW / BOTANIK_LOAD_METRIC_BUCKETS) : 1u)
#endif
    BOTANIK_CFG(uint32_t, STACK_SLICE_TICKS);

    /** @brief CPU-load EMA response time constants (ms) */
#ifndef BOTANIK_CPU_LOAD_EMA_ATTACK_MS
#  define BOTANIK_CPU_LOAD_EMA_ATTACK_MS 0
#endif
    BOTANIK_CFG(uint32_t, CPU_LOAD_EMA_ATTACK_MS);
#ifndef BOTANIK_CPU_LOAD_EMA_DECAY_MS
#  define BOTANIK_CPU_LOAD_EMA_DECAY_MS 2000
#endif
    BOTANIK_CFG(uint32_t, CPU_LOAD_EMA_DECAY_MS);

    /** @brief CPU-jitter EMA response time constants (ms) */
#ifndef BOTANIK_CPU_JITTER_EMA_ATTACK_MS
#  define BOTANIK_CPU_JITTER_EMA_ATTACK_MS 0
#endif
    BOTANIK_CFG(uint32_t, CPU_JITTER_EMA_ATTACK_MS);
#ifndef BOTANIK_CPU_JITTER_EMA_DECAY_MS
#  define BOTANIK_CPU_JITTER_EMA_DECAY_MS 5000
#endif
    BOTANIK_CFG(uint32_t, CPU_JITTER_EMA_DECAY_MS);


    /** @} */


    /**
     * @name PLC Synchronization
     * @{
     */

    /** @brief Number of entries in the circular PLC log buffer.
     *  @note Must match PLC-side code ! Site-tunable: per the PLC program's
     *  layout. */
#ifndef BOTANIK_LOG_PLC_BUFFER_SIZE
#  define BOTANIK_LOG_PLC_BUFFER_SIZE 32
#endif
    BOTANIK_CFG(size_t, LOG_PLC_BUFFER_SIZE);

    /** @brief Maximum length of a single log message from the PLC.
     *  @note Must match PLC-side code ! Site-tunable: per the PLC program's
     *  layout. */
#ifndef BOTANIK_LOG_PLC_MESSAGE_LEN
#  define BOTANIK_LOG_PLC_MESSAGE_LEN 256
#endif
    BOTANIK_CFG(size_t, LOG_PLC_MESSAGE_LEN);

    /** @brief Interval between unsolicited TX frames sent to keep the PLC link
     *  alive */
#ifndef BOTANIK_PLC_HEARTBEAT_MS
#  define BOTANIK_PLC_HEARTBEAT_MS 250
#endif
    BOTANIK_CFG(uint32_t, PLC_HEARTBEAT_MS);

    /** @brief Time without a TX acknowledgement before the link is considered
     *  timed out */
#ifndef BOTANIK_PLC_TIMEOUT_MS
#  define BOTANIK_PLC_TIMEOUT_MS 2500
#endif
    BOTANIK_CFG(uint32_t, PLC_TIMEOUT_MS);

    /** @brief Time without any PLC activity before the link is considered
     *  faulted */
#ifndef BOTANIK_PLC_WATCHDOG_MS
#  define BOTANIK_PLC_WATCHDOG_MS 5000
#endif
    BOTANIK_CFG(uint32_t, PLC_WATCHDOG_MS);

    /** @brief Grace period after a PLC fault during which the HW watchdog is
     *  still kicked: a transient fault rides through (logged + alarmed), but a
     *  fault that persists this long stops the kick, resetting both cores (the
     *  deliberate recovery that gives the PLC image a fresh start). Owned by
     *  HealthMonitor. */
#ifndef BOTANIK_PLC_FAULT_GRACE_MS
#  define BOTANIK_PLC_FAULT_GRACE_MS 15000
#endif
    BOTANIK_CFG(uint32_t, PLC_FAULT_GRACE_MS);

    /** @} */

    /**
     * @name PLC configuration variables
     *
     * Sizes for the generic PLC->host configuration block (plcio/config.h): the
     * network / NTP / MQTT settings the PLC publishes for the host's network
     * stack, NTP client, and MQTT client. Library-fixed and site-independent,
     * but tunable here because the value MUST match the PLC program's generated
     * config type.
     * @{
     */

    /** @brief Char-buffer length for a network address (hostname or IPv4
     *  literal, NUL-terminated; empty [0]==0 means "unset"). Must be a 4-byte
     *  multiple so the config structs stay padding-free.
     *  @note Must match PLC-side code ! See the PLC-side setup in docs. */
#ifndef BOTANIK_PLC_HOST_STR_LEN
#  define BOTANIK_PLC_HOST_STR_LEN 64
#endif
    BOTANIK_CFG(size_t, PLC_HOST_STR_LEN);

    /** @} */

    /**
     * @name Name resolution
     * @{
     */

    /** @brief Rows in the NameResolver table; bounds both in-flight DNS lookups
     *  and cached results (a completed row stays as a cache entry). Size to the
     *  distinct hostnames the components resolve (NTP servers + MQTT broker +
     *  slack). */
#ifndef BOTANIK_DNS_CACHE_SIZE
#  define BOTANIK_DNS_CACHE_SIZE 8
#endif
    BOTANIK_CFG(size_t, DNS_CACHE_SIZE);

    /** @brief Max time NetworkHandler waits in SETUP for the link to come up
     *  with an address before declaring an error and backing off. Generous:
     *  covers DHCP. */
#ifndef BOTANIK_NET_SETUP_TIMEOUT_MS
#  define BOTANIK_NET_SETUP_TIMEOUT_MS 30000
#endif
    BOTANIK_CFG(uint32_t, NET_SETUP_TIMEOUT_MS);

    /** @brief Backoff after a failed (or lost) bring-up before NetworkHandler
     *  retries. */
#ifndef BOTANIK_NET_RETRY_MS
#  define BOTANIK_NET_RETRY_MS 10000
#endif
    BOTANIK_CFG(uint32_t, NET_RETRY_MS);

    /** @} */

    /**
     * @name Time sync (NTP)
     * @{
     */

    /** @brief How long NtpClient waits for an NTP reply before giving up on a
     *  server (then it tries the next one / backs off). */
#ifndef BOTANIK_NTP_QUERY_TIMEOUT_MS
#  define BOTANIK_NTP_QUERY_TIMEOUT_MS 5000
#endif
    BOTANIK_CFG(uint32_t, NTP_QUERY_TIMEOUT_MS);

    /** @brief Backoff after a failed NTP sync before retrying from the first
     *  server. */
#ifndef BOTANIK_NTP_RETRY_MS
#  define BOTANIK_NTP_RETRY_MS 60000
#endif
    BOTANIK_CFG(uint32_t, NTP_RETRY_MS);

    /** @brief Poll interval used when the PLC `ntp` block leaves poll_s at
     *  0. */
#ifndef BOTANIK_NTP_POLL_DEFAULT_S
#  define BOTANIK_NTP_POLL_DEFAULT_S 3600
#endif
    BOTANIK_CFG(uint32_t, NTP_POLL_DEFAULT_S);

    /** @} */

    /**
     * @name MQTT client
     * @{
     */

    /** @brief Broker TCP port used when the PLC `mqtt` block leaves port at
     *  0. */
#ifndef BOTANIK_MQTT_PORT_DEFAULT
#  define BOTANIK_MQTT_PORT_DEFAULT 1883
#endif
    BOTANIK_CFG(uint16_t, MQTT_PORT_DEFAULT);

    /** @brief Keepalive used when the PLC `mqtt` block leaves keepalive_s at
     *  0. */
#ifndef BOTANIK_MQTT_KEEPALIVE_DEFAULT_S
#  define BOTANIK_MQTT_KEEPALIVE_DEFAULT_S 60
#endif
    BOTANIK_CFG(uint16_t, MQTT_KEEPALIVE_DEFAULT_S);

    /** @brief Max time to establish the TCP + MQTT session before failing /
     *  backing off. */
#ifndef BOTANIK_MQTT_CONNECT_TIMEOUT_MS
#  define BOTANIK_MQTT_CONNECT_TIMEOUT_MS 10000
#endif
    BOTANIK_CFG(uint32_t, MQTT_CONNECT_TIMEOUT_MS);

    /** @brief Backoff after a failed / dropped connection before MqttClient
     *  retries. */
#ifndef BOTANIK_MQTT_RETRY_MS
#  define BOTANIK_MQTT_RETRY_MS 30000
#endif
    BOTANIK_CFG(uint32_t, MQTT_RETRY_MS);

    /** @brief Scratch buffer (bytes) MqttClient builds an outgoing packet in;
     *  bounds the topic + payload of a single publish. */
#ifndef BOTANIK_MQTT_TX_BUF
#  define BOTANIK_MQTT_TX_BUF 256
#endif
    BOTANIK_CFG(size_t, MQTT_TX_BUF);

    /** @brief Period between MQTT telemetry publish cycles (ms). 0 disables
     *  telemetry. Each cycle publishes every registered field once. */
#ifndef BOTANIK_MQTT_TELEMETRY_PERIOD_MS
#  define BOTANIK_MQTT_TELEMETRY_PERIOD_MS 30000
#endif
    BOTANIK_CFG(uint32_t, MQTT_TELEMETRY_PERIOD_MS);

    /** @brief Topic segment between the prefix and the field name:
     *  "<MQTT_TOPIC_PREFIX>/<MQTT_TELEMETRY_TOPIC>/<field>". */
#ifndef BOTANIK_MQTT_TELEMETRY_TOPIC
#  define BOTANIK_MQTT_TELEMETRY_TOPIC "telemetry"
#endif
    BOTANIK_CFG(const char*, MQTT_TELEMETRY_TOPIC);

    /** @brief Publish telemetry with the MQTT retain flag set, so a late
     *  subscriber gets the last value of each field immediately. */
#ifndef BOTANIK_MQTT_TELEMETRY_RETAIN
#  define BOTANIK_MQTT_TELEMETRY_RETAIN 1
#endif
    BOTANIK_CFG(bool, MQTT_TELEMETRY_RETAIN);

    /** @} */

    /**
     * @name Logging parameters
     * @{
     */

    /** @brief Maximum burst size (number of logs allowed in a quick
     *  succession) */
#ifndef BOTANIK_LOG_GLOBAL_BUCKET_BURST
#  define BOTANIK_LOG_GLOBAL_BUCKET_BURST 20
#endif
    BOTANIK_CFG(uint16_t, LOG_GLOBAL_BUCKET_BURST);

    /** @brief Refill period in milliseconds (determines the steady-state log
     *  rate) */
#ifndef BOTANIK_LOG_GLOBAL_BUCKET_PERIOD_MS
#  define BOTANIK_LOG_GLOBAL_BUCKET_PERIOD_MS 1000
#endif
    BOTANIK_CFG(uint32_t, LOG_GLOBAL_BUCKET_PERIOD_MS);

    /** @brief Maximum burst size per individual topic */
#ifndef BOTANIK_LOG_PERTOPIC_BUCKET_BURST
#  define BOTANIK_LOG_PERTOPIC_BUCKET_BURST 20
#endif
    BOTANIK_CFG(uint16_t, LOG_PERTOPIC_BUCKET_BURST);

    /** @brief Refill period in milliseconds for per-topic buckets */
#ifndef BOTANIK_LOG_PERTOPIC_BUCKET_PERIOD_MS
#  define BOTANIK_LOG_PERTOPIC_BUCKET_PERIOD_MS 5000
#endif
    BOTANIK_CFG(uint32_t, LOG_PERTOPIC_BUCKET_PERIOD_MS);

    /** @brief How often (ms) a "N msgs suppressed" summary may be emitted.
     *  @note Decoupled from the bucket refill periods so a sustained log storm
     *  produces an occasional summary instead of one per refill tick. */
#ifndef BOTANIK_LOG_SUPPRESS_REPORT_PERIOD_MS
#  define BOTANIK_LOG_SUPPRESS_REPORT_PERIOD_MS 10000
#endif
    BOTANIK_CFG(uint32_t, LOG_SUPPRESS_REPORT_PERIOD_MS);

    /** @brief Minimum interval (ms) between "N entries dropped at sink"
     *  reports. First drop reports instantly; further reports wait out this
     *  holdoff. */
#ifndef BOTANIK_LOG_SINK_DROP_REPORT_PERIOD_MS
#  define BOTANIK_LOG_SINK_DROP_REPORT_PERIOD_MS 5000
#endif
    BOTANIK_CFG(uint32_t, LOG_SINK_DROP_REPORT_PERIOD_MS);

    /** @brief Minimum interval (ms) between "N console bytes dropped (TX full)"
     *  reports. First drop reports instantly; further reports wait this
     *  holdoff. */
#ifndef BOTANIK_LOG_TX_DROP_REPORT_PERIOD_MS
#  define BOTANIK_LOG_TX_DROP_REPORT_PERIOD_MS 10000
#endif
    BOTANIK_CFG(uint32_t, LOG_TX_DROP_REPORT_PERIOD_MS);

    /** @brief Max unique log topics; sizes the per-topic bucket array (fixed
     *  footprint).
     *  @note Bounded by the uint8_t LogTopic type and bucket-loop counters, so
     *  must stay <= 255 - see the static_assert in plcio/vars.h. */
#ifndef BOTANIK_LOG_TOPIC_MAX
#  define BOTANIK_LOG_TOPIC_MAX 255
#endif
    BOTANIK_CFG(uint16_t, LOG_TOPIC_MAX);

    /** @brief Maximum number of pointers to registered log writers */
#ifndef BOTANIK_LOG_WRITERS_MAX
#  define BOTANIK_LOG_WRITERS_MAX 8
#endif
    BOTANIK_CFG(uint8_t, LOG_WRITERS_MAX);

    /** @brief Per-writer in-flight message queue depth */
#ifndef BOTANIK_LOG_QUEUE_SIZE
#  define BOTANIK_LOG_QUEUE_SIZE 32
#endif
    BOTANIK_CFG(size_t, LOG_QUEUE_SIZE);

    /** @brief Writer-FIFO tail slots reserved for LOG_CRITICAL: a normal entry
     *  that would intrude is dropped, so a critical (e.g. the sink-loss report)
     *  can always seat without evicting - else that report would manufacture
     *  the drop it reports. */
#ifndef BOTANIK_LOG_QUEUE_CRITICAL_RESERVE
#  define BOTANIK_LOG_QUEUE_CRITICAL_RESERVE 4
#endif
    BOTANIK_CFG(size_t, LOG_QUEUE_CRITICAL_RESERVE);

    /** @brief Maximum length of a single formatted log line including metadata
     *  (bytes) */
#ifndef BOTANIK_LOG_FORMATTED_LEN
#  define BOTANIK_LOG_FORMATTED_LEN 256
#endif
    BOTANIK_CFG(size_t, LOG_FORMATTED_LEN);

    /** @} */


    /**
     * @name ANSI Terminal Parameters
     * @{
     */

    /** @brief Number of input characters to process in a single batch */
#ifndef BOTANIK_ANSI_INPUT_BATCH_SIZE
#  define BOTANIK_ANSI_INPUT_BATCH_SIZE 32
#endif
    BOTANIK_CFG(size_t, ANSI_INPUT_BATCH_SIZE);

    /** @brief Safety timeout: flush any stale batch regardless of stream
     *  activity */
#ifndef BOTANIK_ANSI_INPUT_BATCH_TIMEOUT_MS
#  define BOTANIK_ANSI_INPUT_BATCH_TIMEOUT_MS 1000
#endif
    BOTANIK_CFG(uint32_t, ANSI_INPUT_BATCH_TIMEOUT_MS);

    /** @brief Time after the last byte before a stalled command sequence is
     *  resolved. bare ESC : Command::ESC, partial CSI or lone SEP :
     *  discarded */
#ifndef BOTANIK_ANSI_CMD_TIMEOUT_MS
#  define BOTANIK_ANSI_CMD_TIMEOUT_MS 100
#endif
    BOTANIK_CFG(uint32_t, ANSI_CMD_TIMEOUT_MS);

    /** @brief Max time a coalesced command (held backspace / Ctrl+D) is held
     *  under TX backpressure before it flushes. Shorter than the text-batch
     *  timeout: an edit wants lower latency than a paste batch. */
#ifndef BOTANIK_ANSI_CMD_FLUSH_TIMEOUT_MS
#  define BOTANIK_ANSI_CMD_FLUSH_TIMEOUT_MS 250
#endif
    BOTANIK_CFG(uint32_t, ANSI_CMD_FLUSH_TIMEOUT_MS);

    /** @brief Interval (ms) between status-bar metric refreshes. Time-based so
     *  the refresh rate is independent of the scheduler tick interval. */
#ifndef BOTANIK_STATUS_REFRESH_MS
#  define BOTANIK_STATUS_REFRESH_MS 1000
#endif
    BOTANIK_CFG(uint32_t, STATUS_REFRESH_MS);

    /** @brief Interval (ms) between full-page metric refreshes (Home/Status).
     *  Slower than the status bar on purpose: pages redraw far more cells, so a
     *  relaxed cadence (with per-value diffing) keeps the low-baud TX buffer
     *  from starving the status bar. */
#ifndef BOTANIK_PAGE_REFRESH_MS
#  define BOTANIK_PAGE_REFRESH_MS 10000
#endif
    BOTANIK_CFG(uint32_t, PAGE_REFRESH_MS);

    /** @brief Cold-field refresh interval (ms) for the Home page. Faster than
     *  the shared cadence but slower than its hot tick (see PAGE_HOT_MS). */
#ifndef BOTANIK_HOME_REFRESH_MS
#  define BOTANIK_HOME_REFRESH_MS 5000
#endif
    BOTANIK_CFG(uint32_t, HOME_REFRESH_MS);

    /** @brief Refresh interval (ms) for a metric page's hot fields
     *  (clocks/uptime), ticking between the slower cold refreshes. Each field
     *  is diffed (usually emits just the changed digit) and best-effort
     *  (skipped when TX is busy). */
#ifndef BOTANIK_PAGE_HOT_MS
#  define BOTANIK_PAGE_HOT_MS 1000
#endif
    BOTANIK_CFG(uint32_t, PAGE_HOT_MS);

    /** @brief Log lines the Home page keeps in its bottom log tail. Small on
     *  purpose: a live "last few entries" widget, not a full scrollback like
     *  the Logs page. */
#ifndef BOTANIK_HOME_SCROLLBACK_LINES
#  define BOTANIK_HOME_SCROLLBACK_LINES 6
#endif
    BOTANIK_CFG(uint16_t, HOME_SCROLLBACK_LINES);

    /** @} */


    /**
     * @name TX output pacing (cooperative buffer sharing)
     *
     * The TX ring drains at the link baud, far slower than the loop ticks.
     * Interactive output shares that one FIFO and cannot jump ahead, so echo
     * latency is roughly (ring occupancy / wire rate). Bulk producers therefore
     * only progress while the ring is shallow, keeping the interactive path
     * responsive. A depth floor gates that; per-tick caps bound the burst once
     * open. See docs/ for the full model.
     * @{
     */

    /** @brief Single paint depth floor, sized in BYTES from the link baud at
     *  construction: depth = baud/10 * target_ms/1000 = bytes draining in
     *  target_ms (8N1). A time target keeps felt echo latency constant across
     *  links. Every cooperative paint path shares this one floor; class
     *  fairness comes from the per-class field budgets and paint order. ~61 B
     *  at 4800 baud. */
#ifndef BOTANIK_TX_COLD_TARGET_MS
#  define BOTANIK_TX_COLD_TARGET_MS 128
#endif
    BOTANIK_CFG(uint32_t, TX_COLD_TARGET_MS);

    /** @brief Max live log / command rows painted per tick per page. At low
     *  baud the wire is the real ceiling (4800 baud ~= 4.4 lines/s), so a
     *  larger value only deepens floor overshoot and worsens echo latency. Keep
     *  at 1; raise only on a fast link if log catch-up feels slow. */
#ifndef BOTANIK_LOG_ROWS_PER_TICK
#  define BOTANIK_LOG_ROWS_PER_TICK 1
#endif
    BOTANIK_CFG(uint16_t, LOG_ROWS_PER_TICK);

    /** @brief Per-tick field budgets for the three cooperative paint classes.
     *  Each has its own counter (reset each tick), so they progress in parallel
     *  and none starves another; they share the depth floor and a fixed paint
     *  order (status, hot, cold). draw_static() uses the cold budget. Unchanged
     *  fields are free (diff emits nothing). */
#ifndef BOTANIK_TX_STATUS_FIELDS_PER_TICK
#  define BOTANIK_TX_STATUS_FIELDS_PER_TICK 2
#endif
    // status bar (clock, metrics)
    BOTANIK_CFG(uint16_t, TX_STATUS_FIELDS_PER_TICK);
#ifndef BOTANIK_TX_HOT_FIELDS_PER_TICK
#  define BOTANIK_TX_HOT_FIELDS_PER_TICK 4
#endif
    // page hot (clock/uptime/serial)
    BOTANIK_CFG(uint16_t, TX_HOT_FIELDS_PER_TICK);
#ifndef BOTANIK_TX_COLD_FIELDS_PER_TICK
#  define BOTANIK_TX_COLD_FIELDS_PER_TICK 4
#endif
    // page cold values + draw_static
    BOTANIK_CFG(uint16_t, TX_COLD_FIELDS_PER_TICK);

    /** @brief Max decoded input events handled per tick. Too low: typing/paste
     *  feels sluggish and the RX ring backs up. Too high: a big paste echoes a
     *  large burst in one tick. Each event may emit echo, so this indirectly
     *  bounds per-tick interactive TX. */
#ifndef BOTANIK_INPUT_EVENTS_PER_TICK
#  define BOTANIK_INPUT_EVENTS_PER_TICK 4
#endif
    BOTANIK_CFG(uint16_t, INPUT_EVENTS_PER_TICK);

    /** @} */


    /**
     * @name Shell Parameters
     * @{
     */

    /** @brief Maximum length of the shell input buffer (bytes) */
#ifndef BOTANIK_SHELL_INPUT_LEN
#  define BOTANIK_SHELL_INPUT_LEN 512
#endif
    BOTANIK_CFG(uint16_t, SHELL_INPUT_LEN);

    /** @brief Number of command history entries to retain */
#ifndef BOTANIK_SHELL_HISTORY_SIZE
#  define BOTANIK_SHELL_HISTORY_SIZE 8
#endif
    BOTANIK_CFG(uint8_t, SHELL_HISTORY_SIZE);

    /** @brief Maximum length of a single history entry (bytes) */
#ifndef BOTANIK_SHELL_HISTORY_ENTRY
#  define BOTANIK_SHELL_HISTORY_ENTRY 128
#endif
    BOTANIK_CFG(uint16_t, SHELL_HISTORY_ENTRY);

    /** @brief Size of the kill ring buffer (bytes) */
#ifndef BOTANIK_SHELL_KILL_SIZE
#  define BOTANIK_SHELL_KILL_SIZE 128
#endif
    BOTANIK_CFG(uint16_t, SHELL_KILL_SIZE);

    /** @brief Max site shell commands accepted by set_commands() (a sanity cap;
     *  a bogus count can't walk off the table). */
#ifndef BOTANIK_SHELL_COMMANDS_MAX
#  define BOTANIK_SHELL_COMMANDS_MAX 64
#endif
    BOTANIK_CFG(size_t, SHELL_COMMANDS_MAX);

    /** @brief Scrollback lines retained by ShellPage for redraw/page-switch
     *  replay */
#ifndef BOTANIK_SHELL_SCROLLBACK_LINES
#  define BOTANIK_SHELL_SCROLLBACK_LINES 32
#endif
    BOTANIK_CFG(uint16_t, SHELL_SCROLLBACK_LINES);

    /** @brief Scrollback lines retained by LogsPage for page-switch replay */
#ifndef BOTANIK_LOGS_SCROLLBACK_LINES
#  define BOTANIK_LOGS_SCROLLBACK_LINES 64
#endif
    BOTANIK_CFG(uint16_t, LOGS_SCROLLBACK_LINES);

    /** @brief Maximum length of a single retained scrollback line (bytes).
     *  Shared by every scrollback / log buffer (Shell, Logs, Home). */
#ifndef BOTANIK_SCROLLBACK_LINE_LEN
#  define BOTANIK_SCROLLBACK_LINE_LEN 256
#endif
    BOTANIK_CFG(uint16_t, SCROLLBACK_LINE_LEN);

    /** @brief Max scrollback rows redrawn per tick() during deferred replay */
#ifndef BOTANIK_SCROLLBACK_ROWS_PER_TICK
#  define BOTANIK_SCROLLBACK_ROWS_PER_TICK 1
#endif
    BOTANIK_CFG(uint16_t, SCROLLBACK_ROWS_PER_TICK);

    /** @brief Continuation prefix for wrapped command-output rows. */
#ifndef BOTANIK_SHELL_CONT_PREFIX
#  define BOTANIK_SHELL_CONT_PREFIX "> "
#endif
    BOTANIK_CFG(const char*, SHELL_CONT_PREFIX);

    /** @brief Continuation prefix for wrapped log rows.
     *  @note The leading indent is tuned to the formatted log-line prefix from
     *  logs/format.h, so wrapped log rows line up under the message's start. */
#ifndef BOTANIK_SHELL_LOG_CONT_PREFIX
#  define BOTANIK_SHELL_LOG_CONT_PREFIX "                        > "
#endif
    BOTANIK_CFG(const char*, SHELL_LOG_CONT_PREFIX);

    /** @} */

    /**
     * @name TextEditor parameters
     * @{
     */

    /** @brief Maximum number of screen rows the TextEditor can lay out */
#ifndef BOTANIK_EDITOR_MAX_ROWS
#  define BOTANIK_EDITOR_MAX_ROWS 32
#endif
    BOTANIK_CFG(uint16_t, EDITOR_MAX_ROWS);

    /** @brief Maximum length (bytes) of a prompt prefix (PS1/PS2/PS3) */
#ifndef BOTANIK_EDITOR_PROMPT_MAX
#  define BOTANIK_EDITOR_PROMPT_MAX 16
#endif
    BOTANIK_CFG(uint16_t, EDITOR_PROMPT_MAX);

    /** @brief Narrowest terminal width the TextEditor can support.
     *  @note We need to keep some additional margin because the actual width
     *  is reduced by the row prefixes (PS1/PS2/PS3). */
#ifndef BOTANIK_EDITOR_MIN_COLS
#  define BOTANIK_EDITOR_MIN_COLS 32
#endif
    BOTANIK_CFG(uint16_t, EDITOR_MIN_COLS);

    /** @brief Widest terminal width the UI supports, a sanity bound on
     *  full-width fills (e.g. the Home log-header bar) so a bogus column count
     *  can't drive an unbounded paint. */
#ifndef BOTANIK_TERMINAL_MAX_COLS
#  define BOTANIK_TERMINAL_MAX_COLS 256
#endif
    BOTANIK_CFG(uint16_t, TERMINAL_MAX_COLS);

    // Ensure that the specified editor minimum size can hold the whole input
    // buffer.
    static_assert(EDITOR_MAX_ROWS * (EDITOR_MIN_COLS - EDITOR_PROMPT_MAX) >= SHELL_INPUT_LEN,
                  "EDITOR_MAX_ROWS too small to lay out SHELL_INPUT_LEN at EDITOR_MIN_COLS");

    /** @} */

} }

#undef BOTANIK_CFG   // internal helper; don't leak it to includers

#endif // LIBBOTANIK_CONFIG_H
