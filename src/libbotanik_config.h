// REVIEW_35: DONE
#ifndef LIBBOTANIK_CONFIG_H
#define LIBBOTANIK_CONFIG_H

/** @file libbotanik_config.h
 *  @brief Global configuration parameters for the Botanik library.
 * 
 *  @note Every parameter in this file is overridable per site. The site selected
 *        by the build tools may ship `src/sites/<name>/config.h` and `#define` 
 *        overrides. The path of the selected site is passed by CMake in
 *        BOTANIK_SITE_CONFIG. */

#include <cstdint>
#include <cstring>

#define BOTANIK_CFG(type, name) static constexpr type name = BOTANIK_##name

// Site configuration
// ===========================================================================

#include "libbotanik_site.h"     // BOTANIK_SITE -> BOTANIK_SITE_CONFIG/HEADER

#if defined(BOTANIK_SITE_CONFIG)
#  include BOTANIK_SITE_CONFIG   // #define BOTANIK_* before the defaults
#endif

// Feature flags
// ===========================================================================

#include "libbotanik_features.h"

// Configuration variables
// ===========================================================================

namespace Botanik { namespace Config {

    /** @name Project Identity
      * @{ */

    /** @brief Firmware version string, surfaced in the boot banner (hardware
     *  half comes from HAL::hardware_description()). */
#ifndef BOTANIK_FIRMWARE_VERSION
#  define BOTANIK_FIRMWARE_VERSION "0.13.12-dev"
#endif
    BOTANIK_CFG(const char*, FIRMWARE_VERSION);

    /** @brief MQTT topic prefix for alarm notifications. Site identity; pinned
     *  per site. */
#ifndef BOTANIK_MQTT_TOPIC_PREFIX
#  define BOTANIK_MQTT_TOPIC_PREFIX "botanik/default"
#endif
    BOTANIK_CFG(const char*, MQTT_TOPIC_PREFIX);

    /** @brief Human-readable site (greenhouse) name, surfaced in the version
     *  command, home screen, and POST line. Pinned per site (matches the
     *  -DBOTANIK_SITE dir). */
#ifndef BOTANIK_SITE_NAME
#  define BOTANIK_SITE_NAME "generic"
#endif
    BOTANIK_CFG(const char*, SITE_NAME);

    /** @}
     *  @name Interface parameters
     *  @{ */

    /** @brief Console UART baud.
     *  @note Sizes the cooperative TX gates.  */
#ifndef BOTANIK_CONSOLE_BAUD
#  define BOTANIK_CONSOLE_BAUD 4800
#endif
    BOTANIK_CFG(uint32_t, CONSOLE_BAUD);

    /** @brief Monitor UART baud.  */
#ifndef BOTANIK_MONITOR_BAUD
#  define BOTANIK_MONITOR_BAUD 115200
#endif
    BOTANIK_CFG(uint32_t, MONITOR_BAUD);

    /** @}
     *  @name Scheduling & performance monitoring
     *  @{ */

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

    /** @brief Maximum number of alarm notifiers the model fans out to. */
#ifndef BOTANIK_ALARM_NOTIFIERS_MAX
#  define BOTANIK_ALARM_NOTIFIERS_MAX 4
#endif
    BOTANIK_CFG(uint8_t, ALARM_NOTIFIERS_MAX);

    /** @brief CPU-load EMA response time attack constant (ms). */
#ifndef BOTANIK_CPU_LOAD_EMA_ATTACK_MS
#  define BOTANIK_CPU_LOAD_EMA_ATTACK_MS 0
#endif
    BOTANIK_CFG(uint32_t, CPU_LOAD_EMA_ATTACK_MS);

    /** @brief CPU-load EMA response time decay constant (ms). */
#ifndef BOTANIK_CPU_LOAD_EMA_DECAY_MS
#  define BOTANIK_CPU_LOAD_EMA_DECAY_MS 2000
#endif
    BOTANIK_CFG(uint32_t, CPU_LOAD_EMA_DECAY_MS);

    /** @brief CPU-jitter EMA response time attack constant (ms). */
#ifndef BOTANIK_CPU_JITTER_EMA_ATTACK_MS
#  define BOTANIK_CPU_JITTER_EMA_ATTACK_MS 0
#endif
    BOTANIK_CFG(uint32_t, CPU_JITTER_EMA_ATTACK_MS);

    /** @brief CPU-jitter EMA response time decay constant (ms). */
#ifndef BOTANIK_CPU_JITTER_EMA_DECAY_MS
#  define BOTANIK_CPU_JITTER_EMA_DECAY_MS 5000
#endif
    BOTANIK_CFG(uint32_t, CPU_JITTER_EMA_DECAY_MS);

    /** @}
     *  @name PLC Synchronization
     *  @{ */

    /** @brief Number of entries in the circular PLC log buffer.
     *  @note Must match PLC-side code ! */
#ifndef BOTANIK_LOG_PLC_BUFFER_SIZE
#  define BOTANIK_LOG_PLC_BUFFER_SIZE 32
#endif
    BOTANIK_CFG(size_t, LOG_PLC_BUFFER_SIZE);

    /** @brief Maximum length of a single log message from the PLC.
     *  @note Must match PLC-side code ! */
#ifndef BOTANIK_LOG_PLC_MESSAGE_LEN
#  define BOTANIK_LOG_PLC_MESSAGE_LEN 256
#endif
    BOTANIK_CFG(size_t, LOG_PLC_MESSAGE_LEN);

    /** @brief Interval between unsolicited TX frames sent to keep the PLC link alive */
#ifndef BOTANIK_PLC_HEARTBEAT_MS
#  define BOTANIK_PLC_HEARTBEAT_MS 250
#endif
    BOTANIK_CFG(uint32_t, PLC_HEARTBEAT_MS);

    /** @brief Time without a TX acknowledgement before the link is considered timed out */
#ifndef BOTANIK_PLC_TIMEOUT_MS
#  define BOTANIK_PLC_TIMEOUT_MS 2500
#endif
    BOTANIK_CFG(uint32_t, PLC_TIMEOUT_MS);

    /** @brief Time without any PLC activity before the link is considered faulted */
#ifndef BOTANIK_PLC_WATCHDOG_MS
#  define BOTANIK_PLC_WATCHDOG_MS 5000
#endif
    BOTANIK_CFG(uint32_t, PLC_WATCHDOG_MS);

    /** @brief Grace period after a PLC fault during which the HW watchdog is still kicked.
     *         A transient fault rides through (logged + alarmed), but a fault that persists 
     *         this long stops the kick, resetting both cores (the deliberate recovery that 
     *         gives the PLC image a fresh start). */
#ifndef BOTANIK_PLC_FAULT_GRACE_MS
#  define BOTANIK_PLC_FAULT_GRACE_MS 15000
#endif
    BOTANIK_CFG(uint32_t, PLC_FAULT_GRACE_MS);

    /** @}
     *  @name PLC configuration variables
     *  @{
     */

    /** @brief Char-buffer length for a network address (hostname or IPv4).
     *  @note Must be a 4-byte multiple so the config structs stay padding-free. */
#ifndef BOTANIK_PLC_HOST_STR_LEN
#  define BOTANIK_PLC_HOST_STR_LEN 64
#endif
    BOTANIK_CFG(size_t, PLC_HOST_STR_LEN);

    /** @}
     *  @name Name resolution
     *  @{ */

    /** @brief Size of the DNS resolver cache size.
     *  @note Also determines the maximum concurrent requests supported. */
#ifndef BOTANIK_DNS_CACHE_SIZE
#  define BOTANIK_DNS_CACHE_SIZE 8
#endif
    BOTANIK_CFG(size_t, DNS_CACHE_SIZE);

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

    /** @}
     *  @name Time sync (NTP)
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

    /** @brief Default NTP poll interval. */
#ifndef BOTANIK_NTP_POLL_DEFAULT_S
#  define BOTANIK_NTP_POLL_DEFAULT_S 3600
#endif
    BOTANIK_CFG(uint32_t, NTP_POLL_DEFAULT_S);

    /** @}
     *  @name MQTT client
     *  @{ */

    /** @brief Default broker TCP port. */
#ifndef BOTANIK_MQTT_PORT_DEFAULT
#  define BOTANIK_MQTT_PORT_DEFAULT 1883
#endif
    BOTANIK_CFG(uint16_t, MQTT_PORT_DEFAULT);

    /** @brief Default MQTT keepalive. */
#ifndef BOTANIK_MQTT_KEEPALIVE_DEFAULT_S
#  define BOTANIK_MQTT_KEEPALIVE_DEFAULT_S 60
#endif
    BOTANIK_CFG(uint16_t, MQTT_KEEPALIVE_DEFAULT_S);

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
     *  @note Bounds the topic + payload of a single publish. */
#ifndef BOTANIK_MQTT_TX_BUF
#  define BOTANIK_MQTT_TX_BUF 256
#endif
    BOTANIK_CFG(size_t, MQTT_TX_BUF);

    /** @brief Period between MQTT telemetry publish cycles (ms). 
     *  @note 0 disables telemetry. 
     *  @note Each cycle publishes every registered field once. */
#ifndef BOTANIK_MQTT_TELEMETRY_PERIOD_MS
#  define BOTANIK_MQTT_TELEMETRY_PERIOD_MS 30000
#endif
    BOTANIK_CFG(uint32_t, MQTT_TELEMETRY_PERIOD_MS);

    /** @brief MQTT topic segment between the prefix and the field name. */
#ifndef BOTANIK_MQTT_TELEMETRY_TOPIC
#  define BOTANIK_MQTT_TELEMETRY_TOPIC "botanik/default"
#endif
    BOTANIK_CFG(const char*, MQTT_TELEMETRY_TOPIC);

    /** @brief Publish telemetry with the MQTT retain flag set, so a late
     *  subscriber gets the last value of each field immediately. */
#ifndef BOTANIK_MQTT_TELEMETRY_RETAIN
#  define BOTANIK_MQTT_TELEMETRY_RETAIN 1
#endif
    BOTANIK_CFG(bool, MQTT_TELEMETRY_RETAIN);

    /** @}
     *  @name Logging parameters
     *  @{ */

    /** @brief Maximum burst size (number of logs allowed in a quick succession).
     *  Sized to absorb the boot burst: every Component announces on its first
     *  step, all in the first beat, on top of the setup() banner + POST. */
#ifndef BOTANIK_LOG_GLOBAL_BUCKET_BURST
#  define BOTANIK_LOG_GLOBAL_BUCKET_BURST 32
#endif
    BOTANIK_CFG(uint16_t, LOG_GLOBAL_BUCKET_BURST);

    /** @brief Refill period in milliseconds (determines the steady-state log rate) */
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

    /** @brief How often (ms) a suppressed-entries-summaries summary may be emitted.
     *  @note Decoupled from the bucket refill periods so a sustained log storm
     *  produces an occasional summary instead of one per refill tick. */
#ifndef BOTANIK_LOG_SUPPRESS_REPORT_PERIOD_MS
#  define BOTANIK_LOG_SUPPRESS_REPORT_PERIOD_MS 10000
#endif
    BOTANIK_CFG(uint32_t, LOG_SUPPRESS_REPORT_PERIOD_MS);

    /** @brief Minimum interval (ms) between dropped-entries-reports.
     *  @note First drop reports instantly; further reports wait out this holdoff. */
#ifndef BOTANIK_LOG_SINK_DROP_REPORT_PERIOD_MS
#  define BOTANIK_LOG_SINK_DROP_REPORT_PERIOD_MS 5000
#endif
    BOTANIK_CFG(uint32_t, LOG_SINK_DROP_REPORT_PERIOD_MS);

    /** @brief Minimum interval (ms) between dropped-bytes-reports.
     *  @note First drop reports instantly; further reports wait this holdoff. */
#ifndef BOTANIK_LOG_TX_DROP_REPORT_PERIOD_MS
#  define BOTANIK_LOG_TX_DROP_REPORT_PERIOD_MS 10000
#endif
    BOTANIK_CFG(uint32_t, LOG_TX_DROP_REPORT_PERIOD_MS);

    /** @brief Max unique log topics; sizes the per-topic bucket array (fixed footprint).
     *  @note Bounded by the uint8_t LogTopic type and bucket-loop counters, so
     *        must stay <= 255 - see the static_assert in plcio/vars.h. */
#ifndef BOTANIK_LOG_TOPIC_MAX
#  define BOTANIK_LOG_TOPIC_MAX 255
#endif
    BOTANIK_CFG(uint16_t, LOG_TOPIC_MAX);

    /** @brief Maximum number of pointers to registered log writers */
#ifndef BOTANIK_LOG_WRITERS_MAX
#  define BOTANIK_LOG_WRITERS_MAX 8
#endif
    BOTANIK_CFG(uint8_t, LOG_WRITERS_MAX);

    /** @brief Per-writer in-flight message queue depth. */
#ifndef BOTANIK_LOG_QUEUE_SIZE
#  define BOTANIK_LOG_QUEUE_SIZE 32
#endif
    BOTANIK_CFG(size_t, LOG_QUEUE_SIZE);

    /** @brief Log writer queue slots reserved for CRITICAL entries. */
#ifndef BOTANIK_LOG_QUEUE_CRITICAL_RESERVE
#  define BOTANIK_LOG_QUEUE_CRITICAL_RESERVE 4
#endif
    BOTANIK_CFG(size_t, LOG_QUEUE_CRITICAL_RESERVE);

    /** @brief Maximum length of a single formatted log line including metadata (bytes). */
#ifndef BOTANIK_LOG_FORMATTED_LEN
#  define BOTANIK_LOG_FORMATTED_LEN 256
#endif
    BOTANIK_CFG(size_t, LOG_FORMATTED_LEN);

    /** @brief Default minimum forwarded log level */
#ifndef BOTANIK_LOG_MIN_LEVEL
#  define BOTANIK_LOG_MIN_LEVEL 2   /* Logs::LOG_INFO */
#endif
    BOTANIK_CFG(uint8_t, LOG_MIN_LEVEL);

    /** @}
     *  @name ANSI Terminal Parameters
     *  @{ */

    /** @brief Number of input characters to process in a single batch. */
#ifndef BOTANIK_ANSI_INPUT_BATCH_SIZE
#  define BOTANIK_ANSI_INPUT_BATCH_SIZE 32
#endif
    BOTANIK_CFG(size_t, ANSI_INPUT_BATCH_SIZE);

    /** @brief Safety timeout: flush any stale batch regardless of stream activity. */
#ifndef BOTANIK_ANSI_INPUT_BATCH_TIMEOUT_MS
#  define BOTANIK_ANSI_INPUT_BATCH_TIMEOUT_MS 1000
#endif
    BOTANIK_CFG(uint32_t, ANSI_INPUT_BATCH_TIMEOUT_MS);

    /** @brief Time after the last byte before a stalled command sequence is resolved */
#ifndef BOTANIK_ANSI_CMD_TIMEOUT_MS
#  define BOTANIK_ANSI_CMD_TIMEOUT_MS 100
#endif
    BOTANIK_CFG(uint32_t, ANSI_CMD_TIMEOUT_MS);

    /** @brief Max time a coalesced command (held backspace / Ctrl+D) is held
     *         under TX backpressure before it flushes. 
     *  @note Shorter than the text-batch timeout: an edit wants lower 
     *        latency than a paste batch. */
#ifndef BOTANIK_ANSI_CMD_FLUSH_TIMEOUT_MS
#  define BOTANIK_ANSI_CMD_FLUSH_TIMEOUT_MS 250
#endif
    BOTANIK_CFG(uint32_t, ANSI_CMD_FLUSH_TIMEOUT_MS);

    /** @brief Interval (ms) between status-bar refreshes.
     *  @note Time-based (refresh rate independent of the tick interval). */
#ifndef BOTANIK_STATUS_REFRESH_MS
#  define BOTANIK_STATUS_REFRESH_MS 1000
#endif
    BOTANIK_CFG(uint32_t, STATUS_REFRESH_MS);

    /** @brief Interval (ms) between full-page metric refreshes (Home/Status).
     *  @note Time-based (refresh rate independent of the tick interval) */
#ifndef BOTANIK_PAGE_REFRESH_MS
#  define BOTANIK_PAGE_REFRESH_MS 10000
#endif
    BOTANIK_CFG(uint32_t, PAGE_REFRESH_MS);

    /** @brief Cold-field refresh interval (ms) for the home page.
     *  @note Time-based (refresh rate independent of the tick interval) */
#ifndef BOTANIK_HOME_REFRESH_MS
#  define BOTANIK_HOME_REFRESH_MS 5000
#endif
    BOTANIK_CFG(uint32_t, HOME_REFRESH_MS);

    /** @brief Refresh interval (ms) for a metric page's hot fields.
     *  @note Time-based (refresh rate independent of the tick interval) */
#ifndef BOTANIK_PAGE_HOT_MS
#  define BOTANIK_PAGE_HOT_MS 1000
#endif
    BOTANIK_CFG(uint32_t, PAGE_HOT_MS);

    /** @brief Log lines the Home page keeps in its bottom log tail. 
     *  @note Small on purpose: a live "last few entries" widget, 
     *        not a full scrollback like the Logs page. */
#ifndef BOTANIK_HOME_SCROLLBACK_LINES
#  define BOTANIK_HOME_SCROLLBACK_LINES 6
#endif
    BOTANIK_CFG(uint16_t, HOME_SCROLLBACK_LINES);

    /** @}
     *  @name TX pacing parameters
     * 
     *  We only have one FIFO queue for serial TX and cannot jump ahead, so we need
     *  bulk producers to only progress while the TX buffer is shallow to keep the
     *  the interactive path responsive.
     * 
     *  A depth floor gates that; per-tick caps bound the burst once open. 
     *  See docs/ for the full model.
     * 
     *  @{ */

    /** @brief Single paint depth floor */
#ifndef BOTANIK_TX_COLD_TARGET_MS
#  define BOTANIK_TX_COLD_TARGET_MS 128
#endif
    BOTANIK_CFG(uint32_t, TX_COLD_TARGET_MS);

    /** @brief Max log/command rows painted per tick per page.
     *  @note At low baud the wire is the real ceiling (4800 baud ~= 4.4 lines/s), so a
     *        larger value only deepens floor overshoot and worsens echo latency. */
#ifndef BOTANIK_LOG_ROWS_PER_TICK
#  define BOTANIK_LOG_ROWS_PER_TICK 2
#endif
    BOTANIK_CFG(uint16_t, LOG_ROWS_PER_TICK);

    /** @brief Per-tick field budget for the status bar. */
#ifndef BOTANIK_TX_STATUS_FIELDS_PER_TICK
#  define BOTANIK_TX_STATUS_FIELDS_PER_TICK 2
#endif
    BOTANIK_CFG(uint16_t, TX_STATUS_FIELDS_PER_TICK);

    /** @brief Per-tick field budget for "hot" metrics field values. */
#ifndef BOTANIK_TX_HOT_FIELDS_PER_TICK
#  define BOTANIK_TX_HOT_FIELDS_PER_TICK 4
#endif
    BOTANIK_CFG(uint16_t, TX_HOT_FIELDS_PER_TICK);

    /** @brief Per-tick field budget for "cold" metrics field values and
     *         for static contents. */
#ifndef BOTANIK_TX_COLD_FIELDS_PER_TICK
#  define BOTANIK_TX_COLD_FIELDS_PER_TICK 4
#endif
    BOTANIK_CFG(uint16_t, TX_COLD_FIELDS_PER_TICK);

    /** @brief Max decoded input events handled per tick. 
     *  @note Too low: typing/paste feels sluggish and the RX ring backs up. 
     *  @note Too high: a big paste echoes a large burst in one tick. */
#ifndef BOTANIK_INPUT_EVENTS_PER_TICK
#  define BOTANIK_INPUT_EVENTS_PER_TICK 4
#endif
    BOTANIK_CFG(uint16_t, INPUT_EVENTS_PER_TICK);

    /** @}
     *  @name Shell parameters
     *  @{ */

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

    /** @brief Max size of the site commands table */
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
     *  @note Shared by every scrollback / log buffer (Shell, Logs, Home). */
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
     *        logs/format.h, so wrapped log rows line up under the message's start. */
#ifndef BOTANIK_SHELL_LOG_CONT_PREFIX
#  define BOTANIK_SHELL_LOG_CONT_PREFIX "                        > "
#endif
    BOTANIK_CFG(const char*, SHELL_LOG_CONT_PREFIX);

    /** @}
     *  @name Text editor parameters
     *  @{ */


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
     *        is reduced by the row prefixes (PS1/PS2/PS3). */
#ifndef BOTANIK_EDITOR_MIN_COLS
#  define BOTANIK_EDITOR_MIN_COLS 32
#endif
    BOTANIK_CFG(uint16_t, EDITOR_MIN_COLS);

    /** @brief Widest terminal width the UI supports, a sanity bound on
     *         full-width fills (e.g. the Home log-header bar) so a bogus column count
     *         can't drive an unbounded paint. */
#ifndef BOTANIK_TERMINAL_MAX_COLS
#  define BOTANIK_TERMINAL_MAX_COLS 256
#endif
    BOTANIK_CFG(uint16_t, TERMINAL_MAX_COLS);

    // Ensure that the specified editor minimum size can hold the whole input
    // buffer.
    static_assert(EDITOR_MAX_ROWS * (EDITOR_MIN_COLS - EDITOR_PROMPT_MAX) >= SHELL_INPUT_LEN,
                  "EDITOR_MAX_ROWS too small to lay out SHELL_INPUT_LEN at EDITOR_MIN_COLS");

    /** @}
     *  @name Simulator settings
     *  @{ */

#ifdef TARGET_SIMULATOR

    /** @brief CPU core the simulator pins the main thread to. 
     *  @note Mirros the main loop running on one core of the OPTA. */
#ifndef BOTANIK_SIM_MAIN_CPU_CORE
#  define BOTANIK_SIM_MAIN_CPU_CORE 0
#endif
    BOTANIK_CFG(int, SIM_MAIN_CPU_CORE);

    /** @brief CPU core the simulator pins the emulator thread to.
     *  @note Mirrors the PLC runtime running on the other core of the OPTA */
#ifndef BOTANIK_SIM_EMULATOR_CPU_CORE
#  define BOTANIK_SIM_EMULATOR_CPU_CORE 1
#endif
    BOTANIK_CFG(int, SIM_EMULATOR_CPU_CORE);

    /** @brief Max bytes the simulator paints/scans the scan below the loop frame.
     *  @note  The OPTA reads the RTOS watermark instead, so this is sim-only. */
#ifndef BOTANIK_STACK_SCAN_LIMIT
#  define BOTANIK_STACK_SCAN_LIMIT 65536
#endif
    BOTANIK_CFG(uint32_t, STACK_SCAN_LIMIT);

    /** @brief Byte pattern for stack "painting" (high-water detection)
     *  @note  The OPTA reads the RTOS watermark instead, so this is sim-only. */
#ifndef BOTANIK_STACK_PAINT_BYTE
#  define BOTANIK_STACK_PAINT_BYTE 0xCD
#endif
    BOTANIK_CFG(uint8_t, STACK_PAINT_BYTE);

    /** @brief Stack high-water is measured (and, on the sim, the free stack
     *  re-painted) once per slice rather than every tick, to amortise the
     *  paint/scan cost. */
#ifndef BOTANIK_STACK_SLICE_TICKS
#  define BOTANIK_STACK_SLICE_TICKS \
       ((BOTANIK_LOAD_METRIC_WINDOW >= BOTANIK_LOAD_METRIC_BUCKETS) \
            ? (BOTANIK_LOAD_METRIC_WINDOW / BOTANIK_LOAD_METRIC_BUCKETS) : 1u)
#endif
    BOTANIK_CFG(uint32_t, STACK_SLICE_TICKS);


#endif // TARGET_SIMULATOR

    // @}

} }


// internal helper; don't leak it to includers
#undef BOTANIK_CFG

#endif // LIBBOTANIK_CONFIG_H
