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
     * @name Performance Monitoring
     * @{
     */

    /** @brief Main task execution period in milliseconds */
    static constexpr uint32_t TASK_PERIOD_MS = 10;

    /** @brief Minimum interval between scheduler-overrun warnings (ms) */
    static constexpr uint32_t SCHEDULER_OVERRUN_LOG_INTERVAL_MS = 5000;

    /** @brief Maximum address range to scan for stack usage monitoring */
    static constexpr uint32_t STACK_SCAN_LIMIT = 1048576;

    /** @brief Byte pattern used for stack "painting" to detect high-water mark */
    static constexpr uint8_t  STACK_PAINT_BYTE = 0xCD;

    /** @brief Sliding window for the "recent peak" CPU/stack metrics, and the number
     *  of rotating buckets used to approximate it (O(1) update, O(buckets) read; the
     *  effective window wobbles by up to one bucket width as buckets rotate). */
    static constexpr uint32_t LOAD_METRIC_WINDOW_MS = 5000;
    static constexpr uint32_t LOAD_METRIC_BUCKETS   = 10;

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

    /** @brief Number of entries in the circular PLC log buffer. Must match PLC-side code. */
    static constexpr size_t LOG_PLC_BUFFER_SIZE = 32;

    /** @brief Maximum length of a single log message from the PLC. Must match PLC-side
     *  code. A multiple of 4 so LogEntry_t stays padding-free (see plcio/vars.h). */
    static constexpr size_t LOG_PLC_MESSAGE_LEN = 256;

    /** @brief Interval between unsolicited TX frames sent to keep the PLC link alive */
    static constexpr uint32_t PLC_HEARTBEAT_MS = 1000;

    /** @brief Time without a TX acknowledgement before the link is considered timed out */
    static constexpr uint32_t PLC_TIMEOUT_MS = 5000;

    /** @brief Time without any PLC activity before a hardware watchdog fault is declared */
    static constexpr uint32_t PLC_WATCHDOG_MS = 30000;

    /** @} */


    /**
     * @name Logging Parameters
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
     *  stay <= 255 — see the static_assert in plcio/vars.h. */
    static constexpr uint16_t LOG_TOPIC_MAX = 255;

    /** @brief Maximum number of pointers to registered log writers */
    static constexpr uint8_t LOG_WRITERS_MAX = 8;

    /** @brief Per-writer in-flight message queue depth */
    static constexpr size_t LOG_QUEUE_SIZE = 64;

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

    /** @brief Time after the last byte before a stalled ESC or CSI sequence is resolved:
     *         bare ESC → Command::ESC, partial CSI → discarded */
    static constexpr uint32_t ANSI_ESC_TIMEOUT_MS = 100;

    /** @brief Interval (ms) between status-bar metric refreshes. Time-based so the
     *         refresh rate is independent of the scheduler tick interval. */
    static constexpr uint32_t STATUS_REFRESH_MS = 2000;

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

    /** @brief Number of scrollback lines retained by ShellPage for redraw/page-switch replay */
    static constexpr uint16_t SHELL_SCROLLBACK_LINES   = 128;

    /** @brief Maximum length of a single retained ShellPage scrollback line (bytes) */
    static constexpr uint16_t SHELL_SCROLLBACK_LINE_LEN        = 128;

    /** @brief Max scrollback rows redrawn per tick() during deferred replay */
    static constexpr uint16_t SCROLLBACK_REDRAW_ROWS_PER_TICK  = 1;

    /** @brief Continuation prefix for wrapped command-output rows. */
    static constexpr const char* SHELL_CONT_PREFIX = "> ";

    /** @brief Continuation prefix for wrapped log rows. The leading indent is
     *  hand-tuned to the formatted log-line prefix from logs/format.h
     *  ("YY/MM/DD HH:MM:SS.mmm [x] TOPIC: ") so wrapped log rows line up under
     *  the message column. It's an approximation (the TOPIC label varies in
     *  width); if the log line layout changes, revisit this width. */
    static constexpr const char* SHELL_LOG_CONT_PREFIX = "                        > ";

    /** @} */


    /**
     * @name TextEditor (editor2) Parameters
     * @{
     */

    /** @brief Maximum number of screen rows the TextEditor can lay out */
    static constexpr uint16_t EDITOR_MAX_ROWS = 32;

    /** @brief Maximum length (bytes) of a prompt prefix (PS1/PS2/PS3) */
    static constexpr uint16_t EDITOR_PROMPT_MAX = 16;

    /** @} */

} }

#endif // LIBBOTANIK_CONFIG_H