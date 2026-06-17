#ifndef LIBBOTANIK_CONFIG_H
#define LIBBOTANIK_CONFIG_H

#include <cstdint>
#include <cstring>

/**
 * @file libbotanik_config.h
 * @brief Global configuration parameters for the Botanik library.
 */

namespace Botanik { namespace Config {

    /**
     * @name Project Identity
     * @{
     */

    /** @brief Firmware version string, surfaced in the boot banner.
     *  Platform-independent; the hardware half of the banner comes from
     *  HAL::hardware_description(). */
    static constexpr const char* FIRMWARE_VERSION = "0.1.0-dev";

    /** @} */


    /**
     * @name Scheduling & performance monitoring
     * @{
     */

    /** @brief Main task execution period in milliseconds */
    static constexpr uint32_t TASK_PERIOD_MS = 10;

    /** @brief Hardware watchdog timeout */
    static constexpr uint32_t HW_WATCHDOG_MS = 1000;

    /** @brief Minimum interval between scheduler-overrun warnings (ms) */
    static constexpr uint32_t SCHEDULER_XRUN_LOG_MS = 5000;

    /** @brief Maximum address range to scan for stack usage monitoring */
    static constexpr uint32_t STACK_SCAN_LIMIT = 1048576;

    /** @brief Byte pattern used for stack "painting" to detect high-water mark */
    static constexpr uint8_t  STACK_PAINT_BYTE = 0xCD;

    /** @brief Sliding window for the "recent peak" CPU/stack metrics,
     *  and the number of rotating buckets used to approximate it. */
    static constexpr uint32_t LOAD_METRIC_WINDOW_MS = 5000;
    static constexpr uint32_t LOAD_METRIC_BUCKETS   = 10;

    /** @brief LOAD_METRIC_WINDOW_MS expressed in per-tick samples (window length the
     *  rolling-max filters are configured with). */
    static constexpr uint32_t LOAD_METRIC_WINDOW = LOAD_METRIC_WINDOW_MS / TASK_PERIOD_MS;

    /** @brief Stack high-water mark is scanned (and the free stack re-painted) once
     *  per slice rather than every tick. One slice per metric bucket spans the whole
     *  LOAD_METRIC_WINDOW, so the rolling max keeps its reach for a fraction of the
     *  per-tick paint/scan cost. */
    static constexpr uint32_t STACK_SLICE_TICKS =
        (LOAD_METRIC_WINDOW >= LOAD_METRIC_BUCKETS)
            ? LOAD_METRIC_WINDOW / LOAD_METRIC_BUCKETS : 1;
    static constexpr uint32_t STACK_SLICE_MS = STACK_SLICE_TICKS * TASK_PERIOD_MS;

    /** @brief CPU-load EMA response time constants (ms) */
    static constexpr uint32_t CPU_LOAD_EMA_ATTACK_MS = 0;
    static constexpr uint32_t CPU_LOAD_EMA_DECAY_MS  = 5000;

    /** @brief CPU-jitter EMA response time constants (ms) */
    static constexpr uint32_t CPU_JITTER_EMA_ATTACK_MS = 0;
    static constexpr uint32_t CPU_JITTER_EMA_DECAY_MS  = 5000;

    /** @brief Stack-usage EMA response time constants (ms) */
    static constexpr uint32_t STACK_USAGE_EMA_ATTACK_MS = 0;
    static constexpr uint32_t STACK_USAGE_EMA_DECAY_MS  = 1000;

    /** @} */


    /**
     * @name PLC Synchronization
     * @{
     */

    /** @brief Number of entries in the circular PLC log buffer.
     *  @note Must match PLC-side code ! */
    static constexpr size_t LOG_PLC_BUFFER_SIZE = 32;

    /** @brief Maximum length of a single log message from the PLC.
     *  @note Must match PLC-side code ! */
    static constexpr size_t LOG_PLC_MESSAGE_LEN = 256;

    /** @brief Interval between unsolicited TX frames sent to keep the PLC link alive */
    static constexpr uint32_t PLC_HEARTBEAT_MS = 250;

    /** @brief Time without a TX acknowledgement before the link is considered timed out */
    static constexpr uint32_t PLC_TIMEOUT_MS = 1000;

    /** @brief Time without any PLC activity before we stop kicking the hardware watchdog */
    static constexpr uint32_t PLC_WATCHDOG_MS = 3000;

    /** @} */

    /**
     * @name Logging parameters
     * @{
     */

    /** @brief Maximum burst size (number of logs allowed in a quick succession) */
    static constexpr uint16_t LOG_GLOBAL_BUCKET_BURST = 20;

    /** @brief Refill period in milliseconds (determines the steady-state log rate) */
    static constexpr uint32_t LOG_GLOBAL_BUCKET_PERIOD_MS = 1000;

    /** @brief Maximum burst size per individual topic */
    static constexpr uint16_t LOG_PERTOPIC_BUCKET_BURST = 20;

    /** @brief Refill period in milliseconds for per-topic buckets */
    static constexpr uint32_t LOG_PERTOPIC_BUCKET_PERIOD_MS = 5000;

    /** @brief How often (ms) a "N msgs suppressed" summary may be emitted.
     *  @note Decoupled from the bucket refill periods so a sustained log storm
     *  produces an occasional summary instead of one per refill tick. */
    static constexpr uint32_t LOG_SUPPRESS_REPORT_PERIOD_MS = 10000;

    /** @brief Maximum number of unique log topics.
     *  @note Each topic consumes memory for its rate-limiting bucket. Bounded by
     *  the uint8_t LogTopic type (and the uint8_t bucket-loop counters), so it must
     *  stay <= 255 - see the static_assert in plcio/vars.h. */
    static constexpr uint16_t LOG_TOPIC_MAX = 255;

    /** @brief Maximum number of pointers to registered log writers */
    static constexpr uint8_t LOG_WRITERS_MAX = 8;

    /** @brief Per-writer in-flight message queue depth */
    static constexpr size_t LOG_QUEUE_SIZE = 32;

    /** @brief Maximum length of a single formatted log line including metadata (bytes) */
    static constexpr size_t LOG_FORMATTED_LEN = 256;

    /** @} */


    /**
     * @name ANSI Terminal Parameters
     * @{
     */

    /** @brief Number of input characters to process in a single batch */
    static constexpr size_t   ANSI_INPUT_BATCH_SIZE = 32;

    /** @brief Safety timeout: flush any stale batch regardless of stream activity */
    static constexpr uint32_t ANSI_INPUT_BATCH_TIMEOUT_MS = 1000;

    /** @brief Time after the last byte before a stalled command sequence is resolved.
     *  bare ESC : Command::ESC, partial CSI or lone SEP : discarded */
    static constexpr uint32_t ANSI_CMD_TIMEOUT_MS = 100;

    /** @brief Interval (ms) between status-bar metric refreshes. Time-based so the
     *  refresh rate is independent of the scheduler tick interval. */
    static constexpr uint32_t STATUS_REFRESH_MS = 2500;

    /** @brief Interval (ms) between full-page metric refreshes (Home/Status pages).
     *  Deliberately slower than the status bar: pages redraw many more cells, so a
     *  relaxed cadence (with per-value diffing) keeps the low-baud TX buffer from
     *  saturating and starving the status bar. */
    static constexpr uint32_t PAGE_REFRESH_MS = 10000;

    /** @brief Cold-field refresh interval (ms) for the Home page. Faster than the
     *  shared cadence but slower than its hot tick (see PAGE_HOT_MS). */
    static constexpr uint32_t HOME_REFRESH_MS = 5000;

    /** @brief Refresh interval (ms) for a metric page's hot fields (clocks/uptime),
     *  so they tick every second between the slower cold refreshes. Each field is
     *  diffed, so a tick usually emits just the changed digit; the tick is also
     *  best-effort (skipped when the TX buffer is busy), see MetricPage::tick(). */
    static constexpr uint32_t PAGE_HOT_MS = 1000;

    /** @brief Log lines the Home page keeps in its bottom log tail. Small on
     *  purpose: it is a live "last few entries" widget with no scroll-up history,
     *  not a full scrollback like the Logs page. Line length is the shared
     *  SCROLLBACK_LINE_LEN. */
    static constexpr uint16_t HOME_SCROLLBACK_LINES = 6;

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
    static constexpr uint32_t TX_COLD_TARGET_MS = 128;

    /** @brief Max live log / command rows painted per tick per page, in one batched
     *  drain_tail emit once the floor is open. The effective ceiling is min(this, wire
     *  capacity), and at low baud the wire is far below this (4800 baud ~= 4.4 lines/s),
     *  so a larger value buys no throughput - it only deepens the per-batch overshoot
     *  of the floor and worsens echo latency. Keep it at 1 so a batch overshoots by a
     *  single line; raise only on a fast link if log catch-up feels slow. */
    static constexpr uint16_t LOG_ROWS_PER_TICK = 1;

    /** @brief Per-tick field budgets for the three cooperative paint classes. Each
     *  class has its own counter (reset each tick), so they progress in parallel at
     *  independent rates and one never starves another; they share the depth floor and
     *  a fixed paint order (status bar, then page hot, then page cold). draw_static()
     *  uses the cold budget (same slow page-content class; it runs before the cold
     *  values and a value needs its label first, so they never compete in a tick).
     *  Unchanged fields are free (the diff emits nothing and spends no budget). */
    static constexpr uint16_t TX_STATUS_FIELDS_PER_TICK = 2;   // status bar (clock, metrics)
    static constexpr uint16_t TX_HOT_FIELDS_PER_TICK    = 4;   // page hot (clock/uptime/serial)
    static constexpr uint16_t TX_COLD_FIELDS_PER_TICK   = 4;   // page cold values + draw_static

    /** @brief Max decoded input events handled per tick. Too low: paste / fast typing
     *  / held keys feel sluggish and the RX ring can back up. Too high: a big paste
     *  echoes a large burst in one tick (fills the ring, competes with itself) plus
     *  more loop time. Each event may emit direct echo, so this indirectly bounds
     *  per-tick interactive TX; heavy intercepts (Ctrl-L / page switch) break the loop
     *  regardless. */
    static constexpr uint16_t INPUT_EVENTS_PER_TICK = 4;

    /** @} */


    /**
     * @name Shell Parameters
     * @{
     */

    /** @brief Maximum length of the shell input buffer (bytes) */
    static constexpr uint16_t SHELL_INPUT_LEN = 512;

    /** @brief Number of command history entries to retain */
    static constexpr uint8_t  SHELL_HISTORY_SIZE  = 8;

    /** @brief Maximum length of a single history entry (bytes) */
    static constexpr uint16_t SHELL_HISTORY_ENTRY = 128;

    /** @brief Size of the kill ring buffer (bytes) */
    static constexpr uint16_t SHELL_KILL_SIZE     = 128;

    /** @brief Scrollback lines retained by ShellPage for redraw/page-switch replay */
    static constexpr uint16_t SHELL_SCROLLBACK_LINES   = 32;

    /** @brief Scrollback lines retained by LogsPage for page-switch replay */
    static constexpr uint16_t LOGS_SCROLLBACK_LINES    = 64;

    /** @brief Maximum length of a single retained scrollback line (bytes). Shared
     *  by every scrollback / log buffer (Shell, Logs, Home). */
    static constexpr uint16_t SCROLLBACK_LINE_LEN      = 256;

    /** @brief Max scrollback rows redrawn per tick() during deferred replay */
    static constexpr uint16_t SCROLLBACK_ROWS_PER_TICK  = 1;

    /** @brief Continuation prefix for wrapped command-output rows. */
    static constexpr const char* SHELL_CONT_PREFIX = "> ";

    /** @brief Continuation prefix for wrapped log rows. 
     *  @note The leading indent is tuned to the formatted log-line prefix from 
     *  logs/format.h, so wrapped log rows line up under the message's start. */
    static constexpr const char* SHELL_LOG_CONT_PREFIX = "                        > ";

    /** @} */

    /**
     * @name TextEditor parameters
     * @{
     */

    /** @brief Maximum number of screen rows the TextEditor can lay out */
    static constexpr uint16_t EDITOR_MAX_ROWS = 32;

    /** @brief Maximum length (bytes) of a prompt prefix (PS1/PS2/PS3) */
    static constexpr uint16_t EDITOR_PROMPT_MAX = 16;

    /** @brief Narrowest terminal width the TextEditor can support.
     *  @note We need to keep some additional margin because the actual width
     *  is reduced by the row prefixes (PS1/PS2/PS3). */
    static constexpr uint16_t EDITOR_MIN_COLS = 32;

    // Ensure that the specified editor minimum size can hold the whole input buffer.
    static_assert(EDITOR_MAX_ROWS * (EDITOR_MIN_COLS - EDITOR_PROMPT_MAX) >= SHELL_INPUT_LEN,
                  "EDITOR_MAX_ROWS too small to lay out SHELL_INPUT_LEN at EDITOR_MIN_COLS");

    /** @} */

} }

#endif // LIBBOTANIK_CONFIG_H