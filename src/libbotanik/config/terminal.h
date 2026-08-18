#ifndef LIBBOTANIK_CONFIG_TERMINAL_H
#define LIBBOTANIK_CONFIG_TERMINAL_H

/** @file terminal.h
 *  @brief Everything the operator sees: the screen, its input decoding,
 *  the per-tick paint budgets, and the pages that render through them. */

#include "libbotanik/config/base.h"

// Defined and undefined per file, not in base.h: see base.h.
#define BOTANIK_CFG(type, name) static constexpr type name = BOTANIK_##name

namespace Botanik { namespace Config {

    /** @name Terminal and console
     *
     *  What is on the console wire and how much of it the UI may use. There is
     *  no resize handshake: what is declared here is what the pages lay out for.
     *  @{ */

    /** @brief The smallest screen the UI is laid out for. Nothing reflows, so
     *  this is a floor: the `term_` parameters are banded at it and the console
     *  clamps to it.
     *  @note What the tables READ well at, not the structural minimum - the
     *  ALARMS page's rows() - 8 is satisfied by about 11 rows. */
#ifndef BOTANIK_SCREEN_MIN_COLS
#  define BOTANIK_SCREEN_MIN_COLS 80
#endif
    BOTANIK_CFG(uint16_t, SCREEN_MIN_COLS);
#ifndef BOTANIK_SCREEN_MIN_ROWS
#  define BOTANIK_SCREEN_MIN_ROWS 24
#endif
    BOTANIK_CFG(uint16_t, SCREEN_MIN_ROWS);

    // The geometry the console actually runs at is Terminal::ConsoleConfig, not
    // a macro: one number, next to the device, checked against these bounds in
    // terminal.h.

    /** @brief Largest terminal the UI supports. Sizes the screen back-buffer
     *  (two COLS x ROWS cell grids), clamps the console geometry, and bounds
     *  full-width fills so a bogus column count cannot drive an unbounded
     *  paint. */
#ifndef BOTANIK_TERMINAL_MAX_COLS
#  define BOTANIK_TERMINAL_MAX_COLS 132
#endif
    BOTANIK_CFG(uint16_t, TERMINAL_MAX_COLS);
#ifndef BOTANIK_TERMINAL_MAX_ROWS
#  define BOTANIK_TERMINAL_MAX_ROWS 32
#endif
    BOTANIK_CFG(uint16_t, TERMINAL_MAX_ROWS);

    static_assert(TERMINAL_MAX_COLS >= SCREEN_MIN_COLS &&
                  TERMINAL_MAX_ROWS >= SCREEN_MIN_ROWS,
                  "the screen buffer cannot hold the minimum screen");

    /** @brief Pages the Console multiplexes over. Registration past it is
     *  refused SILENTLY, so the tab order simply stops gaining pages.
     *  @note Cannot exceed 10: a page is also reachable by Alt+<digit>. The full
     *  set is 8, leaving room for a site's own. */
#ifndef BOTANIK_CONSOLE_PAGES_MAX
#  define BOTANIK_CONSOLE_PAGES_MAX 10
#endif
    BOTANIK_CFG(uint8_t, CONSOLE_PAGES_MAX);

    /** @} */

    /** @name Terminal input decoding
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

    /** @brief Max decoded input events handled per tick.
     *  @note Too low: typing/paste feels sluggish and the RX ring backs up.
     *  @note Too high: a big paste echoes a large burst in one tick. */
#ifndef BOTANIK_INPUT_EVENTS_PER_TICK
#  define BOTANIK_INPUT_EVENTS_PER_TICK 4
#endif
    BOTANIK_CFG(uint16_t, INPUT_EVENTS_PER_TICK);

    /** @} */

    /** @name Page refresh cadences
     *
     *  Time-based, so a refresh rate is independent of the tick interval: what
     *  changes every second, what changes slowly, and TASKS in between.
     *  @{ */

    /** @brief Refresh interval (ms) for a page's hot fields, and for the status
     *  bar's clock and metrics. */
#ifndef BOTANIK_PAGE_HOT_MS
#  define BOTANIK_PAGE_HOT_MS 1000
#endif
    BOTANIK_CFG(uint32_t, PAGE_HOT_MS);

    /** @brief Cold-field refresh interval (ms) for a metric page. */
#ifndef BOTANIK_PAGE_COLD_MS
#  define BOTANIK_PAGE_COLD_MS 5000
#endif
    BOTANIK_CFG(uint32_t, PAGE_COLD_MS);

    /** @brief TASKS table repaint cadence. Slower than PAGE_HOT_MS: the costs it
     *  shows are smoothed aggregates, not a live trace. */
#ifndef BOTANIK_PAGE_TASKS_MS
#  define BOTANIK_PAGE_TASKS_MS 2000
#endif
    BOTANIK_CFG(uint32_t, PAGE_TASKS_MS);

    /** @brief Buffer one rendered table row is assembled in, before the terminal
     *  clips it to the screen width. Shared by every scrolling table: wide
     *  enough for a full-width row plus its escapes and separators. */
#ifndef BOTANIK_TABLE_LINE_LEN
#  define BOTANIK_TABLE_LINE_LEN 200
#endif
    BOTANIK_CFG(uint16_t, TABLE_LINE_LEN);

    /** @} */

    /** @name TX pacing
     *
     *  Serial TX is one FIFO with no jumping ahead, so bulk producers progress
     *  only while the buffer is shallow. A depth floor gates that; per-tick caps
     *  bound the burst. See docs/dev/screen.md.
     *  @{ */

    /** @brief Single paint depth floor, in ms of wire time at the console rate.
     *  @warning Must stay above GROUP_SLOW_MS (static_asserted): the render runs
     *  on that group, so a lower floor lets the wire drain empty between paints.
     *  Nothing measures an underrun, so raise rather than lower it. */
#ifndef BOTANIK_TX_COLD_TARGET_MS
#  define BOTANIK_TX_COLD_TARGET_MS 128
#endif
    BOTANIK_CFG(uint32_t, TX_COLD_TARGET_MS);

    /** @brief TX ring headroom the depth floor always leaves free, so a single
     *  oversized run still has somewhere to land rather than being deferred
     *  forever. Must stay comfortably under SERIAL_TX_BUF. */
#ifndef BOTANIK_TX_HEADROOM_BYTES
#  define BOTANIK_TX_HEADROOM_BYTES 256
#endif
    BOTANIK_CFG(uint16_t, TX_HEADROOM_BYTES);

    /** @brief Max log/command rows painted per tick per page.
     *  @note At low baud the wire is the real ceiling (4800 baud ~= 4.4 lines/s), so a
     *        larger value only deepens floor overshoot and worsens echo latency. */
#ifndef BOTANIK_LOG_ROWS_PER_TICK
#  define BOTANIK_LOG_ROWS_PER_TICK 2
#endif
    BOTANIK_CFG(uint16_t, LOG_ROWS_PER_TICK);

    /** @} */

    /** @name Shell and scrollback
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

    /** @brief Maximum length of a single history entry (bytes).
     *  @note Below SHELL_INPUT_LEN, so a longer command is stored truncated and
     *        recalling it yields a different command. Raise to SHELL_INPUT_LEN
     *        (at SHELL_HISTORY_SIZE x the RAM) if that matters. */
#ifndef BOTANIK_SHELL_HISTORY_ENTRY
#  define BOTANIK_SHELL_HISTORY_ENTRY 128
#endif
    BOTANIK_CFG(uint16_t, SHELL_HISTORY_ENTRY);

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

    /** @brief Log lines the Home page keeps in its bottom log tail.
     *  @note Small on purpose: a live "last few entries" widget,
     *        not a full scrollback like the Logs page. */
#ifndef BOTANIK_HOME_SCROLLBACK_LINES
#  define BOTANIK_HOME_SCROLLBACK_LINES 6
#endif
    BOTANIK_CFG(uint16_t, HOME_SCROLLBACK_LINES);

    /** @brief Maximum length of a single retained scrollback line (bytes).
     *  @note Shared by Shell, Logs and Home, so the cost is this times the three
     *        line counts above. What a page STORES, not what the logger
     *        formats. */
#ifndef BOTANIK_SCROLLBACK_LINE_LEN
#  define BOTANIK_SCROLLBACK_LINE_LEN 256
#endif
    BOTANIK_CFG(uint16_t, SCROLLBACK_LINE_LEN);

    /** @} */

    /** @name Text editor
     *  @{ */

    /** @brief Maximum number of screen rows the TextEditor can lay out */
#ifndef BOTANIK_EDITOR_MAX_ROWS
#  define BOTANIK_EDITOR_MAX_ROWS 32
#endif
    BOTANIK_CFG(uint16_t, EDITOR_MAX_ROWS);

    /** @brief Size of the kill ring (bytes).
     *  @note One buffer for the whole console, so a cut at the shell prompt
     *        yanks into NOTES. A longer kill takes only what fits and LEAVES the
     *        rest, so a yank always restores exactly what went. */
#ifndef BOTANIK_KILL_RING_SIZE
#  define BOTANIK_KILL_RING_SIZE 256
#endif
    BOTANIK_CFG(uint16_t, KILL_RING_SIZE);

    /** @brief Maximum length (bytes) of a prompt prefix (PS1/PS2/PS3) */
#ifndef BOTANIK_EDITOR_PROMPT_MAX
#  define BOTANIK_EDITOR_PROMPT_MAX 16
#endif
    BOTANIK_CFG(uint16_t, EDITOR_PROMPT_MAX);

    /** @brief Narrowest terminal width the TextEditor can support, with margin
     *  for the row prefixes (PS1/PS2/PS3).
     *  @note Read only by the two layout static_asserts, which is why it lives
     *        beside the lengths they relate. */
#ifndef BOTANIK_EDITOR_MIN_COLS
#  define BOTANIK_EDITOR_MIN_COLS 32
#endif
    BOTANIK_CFG(uint16_t, EDITOR_MIN_COLS);

    // The editor's minimum size must hold the whole input buffer. Exact at the
    // defaults, passing only because the buffer reserves its NUL. Does not model
    // TextEditor's _indent, which is safe only while the one indented editor is
    // single-line.
    static_assert(EDITOR_MAX_ROWS * (EDITOR_MIN_COLS - EDITOR_PROMPT_MAX) >= SHELL_INPUT_LEN,
                  "EDITOR_MAX_ROWS too small to lay out SHELL_INPUT_LEN at EDITOR_MIN_COLS");

    /** @} */


    /** @} */

} }

#undef BOTANIK_CFG

#endif // LIBBOTANIK_CONFIG_TERMINAL_H
