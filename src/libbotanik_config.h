#ifndef LIBBOTANIK_CONFIG_H
#define LIBBOTANIK_CONFIG_H

#include <cstdint>
#include <cstring>

/**
 * @file libbotanik_config.h
 * @brief Global configuration parameters for the Botanik library.
 *
 * Every parameter below is overridable per site. Each value defaults from a
 * `BOTANIK_<NAME>` macro guarded by `#ifndef`, so a site can pin a different value
 * by `#define`-ing that macro before the default is seen. The selected site may ship
 * `src/sites/<name>/config.h` doing exactly that; CMake passes its path as
 * BOTANIK_SITE_CONFIG and includes it below, *PUBLIC on the botanik library* so the
 * override is build-wide - libbotanik's own TUs and every consumer compile against
 * the same values (anything else is an ABI mismatch, e.g. a torn PLC log layout).
 */

#if defined(BOTANIK_SITE_CONFIG)
#  include BOTANIK_SITE_CONFIG   // site overrides: #define BOTANIK_* before the defaults
#endif

// Declare a Config value from its #ifndef-guarded BOTANIK_<NAME> default. The guard
// triplet above each call can't be folded in - the preprocessor never re-reads a
// macro's output as a directive, so a macro can't emit #ifndef - this only keeps the
// declaration DRY and the name impossible to mistype. #undef'd at end of file.
#define BOTANIK_CFG(type, name) static constexpr type name = BOTANIK_##name

namespace Botanik { namespace Config {

    /**
     * @name Project Identity
     * @{
     */

    /** @brief Firmware version string, surfaced in the boot banner.
     *  Platform-independent; the hardware half of the banner comes from
     *  HAL::hardware_description(). */
#ifndef BOTANIK_FIRMWARE_VERSION
#  define BOTANIK_FIRMWARE_VERSION "0.13.12-dev"
#endif
    BOTANIK_CFG(const char*, FIRMWARE_VERSION);

    /** @brief MQTT topic prefix for alarm notifications. Site identity - the default
     *  is generic; a site pins its own in `sites/<name>/config.h`. */
#ifndef BOTANIK_MQTT_TOPIC_PREFIX
#  define BOTANIK_MQTT_TOPIC_PREFIX "botanik"
#endif
    BOTANIK_CFG(const char*, MQTT_TOPIC_PREFIX);

    /** @brief Human-readable site (greenhouse) name. Surfaced in the version command,
     *  the home screen (beside the logo), and the boot self-test (POST) line. The
     *  default is generic; a site pins its own in `sites/<name>/config.h` (matching
     *  the `-DBOTANIK_SITE` directory name, e.g. "gh_mlb41"). */
#ifndef BOTANIK_SITE_NAME
#  define BOTANIK_SITE_NAME "generic"
#endif
    BOTANIK_CFG(const char*, SITE_NAME);

    /** @brief Console UART baud (also sizes the terminal's cooperative-TX gates). The
     *  default is the OPTA hardware rate; the simulator deliberately overrides it low (see
     *  botanik_sim/main.cpp) to exercise the TX pacing. A site may pin its own. */
#ifndef BOTANIK_CONSOLE_BAUD
#  define BOTANIK_CONSOLE_BAUD 115200
#endif
    BOTANIK_CFG(uint32_t, CONSOLE_BAUD);

    /** @} */


    /**
     * @name Scheduling & performance monitoring
     * @{
     */

    /** @brief Main task execution period in milliseconds */
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
    /** @brief CPU cores the simulator pins its two realtime threads to, mirroring
     *  the OPTA's independent cores: the main loop on one, the PLC emulator on
     *  another so they run concurrently instead of contending for a core. */
#ifndef BOTANIK_SIM_MAIN_CPU_CORE
#  define BOTANIK_SIM_MAIN_CPU_CORE 0
#endif
    BOTANIK_CFG(int, SIM_MAIN_CPU_CORE);
#ifndef BOTANIK_SIM_EMULATOR_CPU_CORE
#  define BOTANIK_SIM_EMULATOR_CPU_CORE 1
#endif
    BOTANIK_CFG(int, SIM_EMULATOR_CPU_CORE);
#endif

    /** @brief Max bytes the simulator paints/scans below the loop frame for the stack
     *  high-water (OPTA reads the RTOS watermark instead, so this is sim-only). 64 KiB
     *  comfortably covers any real firmware stack while bounding the per-slice paint. */
#ifndef BOTANIK_STACK_SCAN_LIMIT
#  define BOTANIK_STACK_SCAN_LIMIT 65536
#endif
    BOTANIK_CFG(uint32_t, STACK_SCAN_LIMIT);

    /** @brief Byte pattern used for stack "painting" to detect high-water mark */
#ifndef BOTANIK_STACK_PAINT_BYTE
#  define BOTANIK_STACK_PAINT_BYTE 0xCD
#endif
    BOTANIK_CFG(uint8_t, STACK_PAINT_BYTE);

    /** @brief Sliding window for the "recent peak" CPU/stack metrics,
     *  and the number of rotating buckets used to approximate it. */
#ifndef BOTANIK_LOAD_METRIC_WINDOW_MS
#  define BOTANIK_LOAD_METRIC_WINDOW_MS 5000
#endif
    BOTANIK_CFG(uint32_t, LOAD_METRIC_WINDOW_MS);
#ifndef BOTANIK_LOAD_METRIC_BUCKETS
#  define BOTANIK_LOAD_METRIC_BUCKETS 10
#endif
    BOTANIK_CFG(uint32_t, LOAD_METRIC_BUCKETS);

    /** @brief LOAD_METRIC_WINDOW_MS expressed in per-tick samples (window length the
     *  rolling-max filters are configured with). */
#ifndef BOTANIK_LOAD_METRIC_WINDOW
#  define BOTANIK_LOAD_METRIC_WINDOW (BOTANIK_LOAD_METRIC_WINDOW_MS / BOTANIK_TASK_PERIOD_MS)
#endif
    BOTANIK_CFG(uint32_t, LOAD_METRIC_WINDOW);

    /** @brief Local platform-health alarm thresholds (see BotanikFirmware::loop()):
     *  CPU_LOAD / CPU_JITT fire when the rolling-peak load / jitter exceeds this
     *  percent of the task period; STACK fires above this percent stack usage. How
     *  long each stays latched after it clears is per-alarm in the site table (latch_s). */
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
    /** @brief How long a momentary-event alarm (CPU_XRUN, RX_TORN) stays held active
     *  after its counter ticks. HealthMonitor latches this window in the source (a
     *  proper held state, not a one-beat pulse), so the alarm stays visible without
     *  depending on the table's latch_s. */
#ifndef BOTANIK_ALARM_EVENT_HOLD_MS
#  define BOTANIK_ALARM_EVENT_HOLD_MS 300000
#endif
    BOTANIK_CFG(uint32_t, ALARM_EVENT_HOLD_MS);

    /** @brief Stack high-water is measured (and, on the sim, the free stack re-painted)
     *  once per slice rather than every tick, to amortise the paint/scan cost. */
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
     *  @note Must match PLC-side code ! Site-tunable: per the PLC program's layout. */
#ifndef BOTANIK_LOG_PLC_BUFFER_SIZE
#  define BOTANIK_LOG_PLC_BUFFER_SIZE 32
#endif
    BOTANIK_CFG(size_t, LOG_PLC_BUFFER_SIZE);

    /** @brief Maximum length of a single log message from the PLC.
     *  @note Must match PLC-side code ! Site-tunable: per the PLC program's layout. */
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

    /** @brief Grace period after a PLC fault during which the HW watchdog is still
     *  kicked: a transient fault rides through (logged + alarmed), but a fault that
     *  persists this long stops the kick, resetting both cores (the deliberate
     *  recovery that gives the PLC image a fresh start). Owned by HealthMonitor. */
#ifndef BOTANIK_PLC_FAULT_GRACE_MS
#  define BOTANIK_PLC_FAULT_GRACE_MS 15000
#endif
    BOTANIK_CFG(uint32_t, PLC_FAULT_GRACE_MS);

    /** @} */

    /**
     * @name PLC configuration variables
     *
     * Sizes for the generic PLC->host configuration block (plcio/config.h): the
     * network / NTP / MQTT settings the PLC publishes for the host's network stack,
     * NTP client, and MQTT client. Library-fixed and site-independent, but tunable
     * here because the value MUST match the PLC program's generated config type.
     * @{
     */

    /** @brief Char-buffer length for a network address in the PLC config block: a
     *  hostname or a dotted-decimal IPv4 literal (e.g. "pool.ntp.org" or
     *  "192.168.1.10"), NUL-terminated; empty ([0]==0) means "unset". Must be a
     *  4-byte multiple so the config structs stay padding-free.
     *  @note Must match PLC-side code ! See the PLC-side setup in docs. */
#ifndef BOTANIK_PLC_HOST_STR_LEN
#  define BOTANIK_PLC_HOST_STR_LEN 64
#endif
    BOTANIK_CFG(size_t, PLC_HOST_STR_LEN);

    /** @} */

    /**
     * @name Logging parameters
     * @{
     */

    /** @brief Maximum burst size (number of logs allowed in a quick succession) */
#ifndef BOTANIK_LOG_GLOBAL_BUCKET_BURST
#  define BOTANIK_LOG_GLOBAL_BUCKET_BURST 20
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

    /** @brief How often (ms) a "N msgs suppressed" summary may be emitted.
     *  @note Decoupled from the bucket refill periods so a sustained log storm
     *  produces an occasional summary instead of one per refill tick. */
#ifndef BOTANIK_LOG_SUPPRESS_REPORT_PERIOD_MS
#  define BOTANIK_LOG_SUPPRESS_REPORT_PERIOD_MS 10000
#endif
    BOTANIK_CFG(uint32_t, LOG_SUPPRESS_REPORT_PERIOD_MS);

    /** @brief Minimum interval (ms) between "N entries dropped at sink" reports.
     *  First drop reports instantly; further reports wait out this holdoff. */
#ifndef BOTANIK_LOG_SINK_DROP_REPORT_PERIOD_MS
#  define BOTANIK_LOG_SINK_DROP_REPORT_PERIOD_MS 5000
#endif
    BOTANIK_CFG(uint32_t, LOG_SINK_DROP_REPORT_PERIOD_MS);

    /** @brief Minimum interval (ms) between "N console bytes dropped (TX full)"
     *  reports. First drop reports instantly; further reports wait this holdoff. */
#ifndef BOTANIK_LOG_TX_DROP_REPORT_PERIOD_MS
#  define BOTANIK_LOG_TX_DROP_REPORT_PERIOD_MS 10000
#endif
    BOTANIK_CFG(uint32_t, LOG_TX_DROP_REPORT_PERIOD_MS);

    /** @brief Maximum number of unique log topics; sizes the logger's per-topic
     *  bucket array, so the footprint stays fixed regardless of how many topics the
     *  LogTopic enum currently defines.
     *  @note Each topic consumes memory for its rate-limiting bucket. Bounded by
     *  the uint8_t LogTopic type (and the uint8_t bucket-loop counters), so it must
     *  stay <= 255 - see the static_assert in plcio/vars.h. */
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

    /** @brief Slots at the tail of the writer FIFO reserved for LOG_CRITICAL: a normal
     *  entry that would intrude on them is dropped, so a critical (e.g. the sink-loss
     *  report itself) can always seat without evicting - which stops that report from
     *  manufacturing the very drop it reports. A few suffice (criticals are rare). */
#ifndef BOTANIK_LOG_QUEUE_CRITICAL_RESERVE
#  define BOTANIK_LOG_QUEUE_CRITICAL_RESERVE 4
#endif
    BOTANIK_CFG(size_t, LOG_QUEUE_CRITICAL_RESERVE);

    /** @brief Maximum length of a single formatted log line including metadata (bytes) */
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

    /** @brief Safety timeout: flush any stale batch regardless of stream activity */
#ifndef BOTANIK_ANSI_INPUT_BATCH_TIMEOUT_MS
#  define BOTANIK_ANSI_INPUT_BATCH_TIMEOUT_MS 1000
#endif
    BOTANIK_CFG(uint32_t, ANSI_INPUT_BATCH_TIMEOUT_MS);

    /** @brief Time after the last byte before a stalled command sequence is resolved.
     *  bare ESC : Command::ESC, partial CSI or lone SEP : discarded */
#ifndef BOTANIK_ANSI_CMD_TIMEOUT_MS
#  define BOTANIK_ANSI_CMD_TIMEOUT_MS 100
#endif
    BOTANIK_CFG(uint32_t, ANSI_CMD_TIMEOUT_MS);

    /** @brief Max time a coalesced command (held backspace / Ctrl+D) is held under TX
     *  backpressure before it flushes. Shorter than the text-batch timeout: an edit wants
     *  lower latency than a paste batch. */
#ifndef BOTANIK_ANSI_CMD_FLUSH_TIMEOUT_MS
#  define BOTANIK_ANSI_CMD_FLUSH_TIMEOUT_MS 250
#endif
    BOTANIK_CFG(uint32_t, ANSI_CMD_FLUSH_TIMEOUT_MS);

    /** @brief Interval (ms) between status-bar metric refreshes. Time-based so the
     *  refresh rate is independent of the scheduler tick interval. */
#ifndef BOTANIK_STATUS_REFRESH_MS
#  define BOTANIK_STATUS_REFRESH_MS 1000
#endif
    BOTANIK_CFG(uint32_t, STATUS_REFRESH_MS);

    /** @brief Interval (ms) between full-page metric refreshes (Home/Status pages).
     *  Deliberately slower than the status bar: pages redraw many more cells, so a
     *  relaxed cadence (with per-value diffing) keeps the low-baud TX buffer from
     *  saturating and starving the status bar. */
#ifndef BOTANIK_PAGE_REFRESH_MS
#  define BOTANIK_PAGE_REFRESH_MS 10000
#endif
    BOTANIK_CFG(uint32_t, PAGE_REFRESH_MS);

    /** @brief Cold-field refresh interval (ms) for the Home page. Faster than the
     *  shared cadence but slower than its hot tick (see PAGE_HOT_MS). */
#ifndef BOTANIK_HOME_REFRESH_MS
#  define BOTANIK_HOME_REFRESH_MS 5000
#endif
    BOTANIK_CFG(uint32_t, HOME_REFRESH_MS);

    /** @brief Refresh interval (ms) for a metric page's hot fields (clocks/uptime),
     *  so they tick every second between the slower cold refreshes. Each field is
     *  diffed, so a tick usually emits just the changed digit; the tick is also
     *  best-effort (skipped when the TX buffer is busy), see MetricPage::tick(). */
#ifndef BOTANIK_PAGE_HOT_MS
#  define BOTANIK_PAGE_HOT_MS 1000
#endif
    BOTANIK_CFG(uint32_t, PAGE_HOT_MS);

    /** @brief Log lines the Home page keeps in its bottom log tail. Small on
     *  purpose: it is a live "last few entries" widget with no scroll-up history,
     *  not a full scrollback like the Logs page. Line length is the shared
     *  SCROLLBACK_LINE_LEN. */
#ifndef BOTANIK_HOME_SCROLLBACK_LINES
#  define BOTANIK_HOME_SCROLLBACK_LINES 6
#endif
    BOTANIK_CFG(uint16_t, HOME_SCROLLBACK_LINES);

    /** @} */


    /**
     * @name TX output pacing (cooperative buffer sharing)
     *
     * The TX ring drains to the wire at the link baud (~1 byte per baud/10 us), far
     * slower than the main loop ticks. Interactive output (key echo, cursor moves)
     * shares that one FIFO and cannot jump ahead of queued bytes, so echo latency is
     * roughly (ring occupancy / wire rate). Bulk producers therefore only make
     * progress while the ring is shallow, which keeps the interactive path and the
     * status bar responsive. Two depth classes gate that; per-tick caps bound the
     * burst once a gate is open. See docs/ for the full model.
     * @{
     */

    /** @brief Single paint depth floor, sized in BYTES from the link baud at
     *  construction (AnsiTerminal): depth = baud/10 * target_ms/1000 = the bytes that
     *  drain in target_ms at the link rate (8N1). A time target keeps the felt echo
     *  latency constant across links and is dilation-invariant (a byte count). EVERY
     *  cooperative paint path (status bar, page hot, page cold, draw_static, replays)
     *  shares this one floor; fairness between the classes comes from the per-class
     *  field budgets below and the paint order, not from separate depth gates.
     *  ~61 B at 4800 baud. See AnsiTerminal::tx_below_cold(). */
#ifndef BOTANIK_TX_COLD_TARGET_MS
#  define BOTANIK_TX_COLD_TARGET_MS 128
#endif
    BOTANIK_CFG(uint32_t, TX_COLD_TARGET_MS);

    /** @brief Max live log / command rows painted per tick per page, in one batched
     *  drain_tail emit once the floor is open. The effective ceiling is min(this, wire
     *  capacity), and at low baud the wire is far below this (4800 baud ~= 4.4 lines/s),
     *  so a larger value buys no throughput - it only deepens the per-batch overshoot
     *  of the floor and worsens echo latency. Keep it at 1 so a batch overshoots by a
     *  single line; raise only on a fast link if log catch-up feels slow. */
#ifndef BOTANIK_LOG_ROWS_PER_TICK
#  define BOTANIK_LOG_ROWS_PER_TICK 1
#endif
    BOTANIK_CFG(uint16_t, LOG_ROWS_PER_TICK);

    /** @brief Per-tick field budgets for the three cooperative paint classes. Each
     *  class has its own counter (reset each tick), so they progress in parallel at
     *  independent rates and one never starves another; they share the depth floor and
     *  a fixed paint order (status bar, then page hot, then page cold). draw_static()
     *  uses the cold budget (same slow page-content class; it runs before the cold
     *  values and a value needs its label first, so they never compete in a tick).
     *  Unchanged fields are free (the diff emits nothing and spends no budget). */
#ifndef BOTANIK_TX_STATUS_FIELDS_PER_TICK
#  define BOTANIK_TX_STATUS_FIELDS_PER_TICK 2
#endif
    BOTANIK_CFG(uint16_t, TX_STATUS_FIELDS_PER_TICK);   // status bar (clock, metrics)
#ifndef BOTANIK_TX_HOT_FIELDS_PER_TICK
#  define BOTANIK_TX_HOT_FIELDS_PER_TICK 4
#endif
    BOTANIK_CFG(uint16_t, TX_HOT_FIELDS_PER_TICK);       // page hot (clock/uptime/serial)
#ifndef BOTANIK_TX_COLD_FIELDS_PER_TICK
#  define BOTANIK_TX_COLD_FIELDS_PER_TICK 4
#endif
    BOTANIK_CFG(uint16_t, TX_COLD_FIELDS_PER_TICK);       // page cold values + draw_static

    /** @brief Max decoded input events handled per tick. Too low: paste / fast typing
     *  / held keys feel sluggish and the RX ring can back up. Too high: a big paste
     *  echoes a large burst in one tick (fills the ring, competes with itself) plus
     *  more loop time. Each event may emit direct echo, so this indirectly bounds
     *  per-tick interactive TX; heavy intercepts (Ctrl-L / page switch) break the loop
     *  regardless. */
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

    /** @brief Max site shell commands accepted by set_commands() (a sanity cap; a
     *  bogus count can't walk off the table). */
#ifndef BOTANIK_SHELL_COMMANDS_MAX
#  define BOTANIK_SHELL_COMMANDS_MAX 64
#endif
    BOTANIK_CFG(size_t, SHELL_COMMANDS_MAX);

    /** @brief Scrollback lines retained by ShellPage for redraw/page-switch replay */
#ifndef BOTANIK_SHELL_SCROLLBACK_LINES
#  define BOTANIK_SHELL_SCROLLBACK_LINES 32
#endif
    BOTANIK_CFG(uint16_t, SHELL_SCROLLBACK_LINES);

    /** @brief Scrollback lines retained by LogsPage for page-switch replay */
#ifndef BOTANIK_LOGS_SCROLLBACK_LINES
#  define BOTANIK_LOGS_SCROLLBACK_LINES 64
#endif
    BOTANIK_CFG(uint16_t, LOGS_SCROLLBACK_LINES);

    /** @brief Maximum length of a single retained scrollback line (bytes). Shared
     *  by every scrollback / log buffer (Shell, Logs, Home). */
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

    /** @brief Widest terminal width the UI supports, a sanity bound on full-width
     *  fills (e.g. the Home log-header bar) so a bogus column count can't drive an
     *  unbounded paint. */
#ifndef BOTANIK_TERMINAL_MAX_COLS
#  define BOTANIK_TERMINAL_MAX_COLS 256
#endif
    BOTANIK_CFG(uint16_t, TERMINAL_MAX_COLS);

    // Ensure that the specified editor minimum size can hold the whole input buffer.
    static_assert(EDITOR_MAX_ROWS * (EDITOR_MIN_COLS - EDITOR_PROMPT_MAX) >= SHELL_INPUT_LEN,
                  "EDITOR_MAX_ROWS too small to lay out SHELL_INPUT_LEN at EDITOR_MIN_COLS");

    /** @} */

} }

#undef BOTANIK_CFG   // internal helper; don't leak it to includers

#endif // LIBBOTANIK_CONFIG_H
