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
     * @name Performance Monitoring
     * @{
     */

    /** @brief Main task execution period in milliseconds */
    static constexpr uint32_t TASK_PERIOD_MS = 10;

    /** @brief Maximum address range to scan for stack usage monitoring */
    static constexpr uint32_t STACK_SCAN_LIMIT = 1048576;

    /** @brief Byte pattern used for stack "painting" to detect high-water mark */
    static constexpr uint8_t  STACK_PAINT_BYTE = 0xCD;

    /** @brief Number of ticks over which the peak CPU load is tracked.
     *  Also controls how often the terminal status row is refreshed. */
    static constexpr uint32_t CPU_LOAD_WINDOW = 200;

    /** @brief Exponential Moving Average attack (rise) factor for CPU load */
    static constexpr float CPU_LOAD_EMA_ATTACK = 1.00f;

    /** @brief Exponential Moving Average decay (fall) factor for CPU load */
    static constexpr float CPU_LOAD_EMA_DECAY  = 0.01f;

    /** @brief EMA attack factor for stack usage monitoring */
    static constexpr float STACK_USAGE_EMA_ATTACK = 0.95f;

    /** @brief EMA decay factor for stack usage monitoring */
    static constexpr float STACK_USAGE_EMA_DECAY  = 0.01f;

    /** @} */


    /**
     * @name PLC Synchronization
     * @{
     */

    /** @brief Number of entries in the circular PLC log buffer. Must match PLC-side code. */
    static constexpr size_t LOG_PLC_BUFFER_SIZE = 32;

    /** @brief Maximum length of a single log message from the PLC. Must match PLC-side code. */
    static constexpr size_t LOG_PLC_MESSAGE_LEN = 255;

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
     *  @note Each topic consumes memory for its rate-limiting bucket. */
    static constexpr uint16_t LOG_TOPIC_MAX = 255;

    /** @brief Maximum number of pointers to registered log writers (Serial, SD, Ethernet, etc.) */
    static constexpr uint8_t LOG_WRITERS_MAX = 8;

    /** @brief Per-writer in-flight message queue depth */
    static constexpr size_t LOG_QUEUE_SIZE = 64;

    /** @brief Maximum length of a single formatted log line including metadata (bytes) */
    static constexpr size_t LOG_FORMATTED_LEN = 255;

    /** @} */


    /**
     * @name ANSI Terminal Parameters
     * @{
     */

    /** @brief Number of input characters to process in a single batch */
    static constexpr size_t   ANSI_INPUT_BATCH_SIZE = 32;

    /** @brief Inter-byte idle gap (ms) that marks the end of a text stream.
     *         Bytes arriving within the same scheduler tick (idle_ms == 0) are always
     *         accumulated; the batch is flushed on the first tick after input goes quiet.
     *         2 ms ≈ 2 byte-times at 9600 baud. */
    static constexpr uint32_t ANSI_STREAM_IDLE_MS = 2;

    /** @brief Safety timeout: flush any stale batch regardless of stream activity */
    static constexpr uint32_t ANSI_INPUT_BATCH_TIMEOUT_MS = 1000;

    /** @brief Time after the last byte before a stalled ESC or CSI sequence is resolved:
     *         bare ESC → Command::ESC, partial CSI → discarded */
    static constexpr uint32_t ANSI_ESC_TIMEOUT_MS = 100;

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