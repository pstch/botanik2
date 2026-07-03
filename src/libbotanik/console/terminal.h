// REVIEW_35: TODO
#ifndef CONSOLE_TERMINAL_H
#define CONSOLE_TERMINAL_H

/**
 * @file terminal.h
 * @brief AnsiTerminal - the VT device.
 *
 * The low-level half of the console: it owns the serial link, emits ANSI escape
 * sequences, and tracks the cursor / attribute / physical-geometry shadow state.
 * Cursor moves, character output, content scrolling, the shared field painter,
 * and cooperative-TX gating live here. The Console (console.h) composes one of
 * these and drives page multiplexing, the status/alarm bars, and the per-tick
 * step() on top of it. Renderers (pages, editor, bars) hold an AnsiTerminal&
 * (obtained via Console::terminal()) and paint through it.
 */

#include <cstdint>

#include "libbotanik/utils/serial.h"
#include "libbotanik/console/ansi.h"

namespace Botanik { namespace Terminal {

    /** @class AnsiTerminal
     *  @brief Owns the serial link; emits ANSI and tracks the terminal shadow
     *  state. See the file header. */
    class AnsiTerminal {
        using BufferedSerial = Utils::BufferedSerial;
        using CursorPos = Terminal::Ansi::CursorPos;

        /** @name External reference
         *  @{ */

        BufferedSerial&     _serial;

        /** @}
         *  @name Display geometry
         *  @{ */

        uint16_t            _cols;          /**< Display width in columns. */
        uint16_t            _rows;          /**< Physical display height in rows. */

        /** @}
         *  @name Hardware terminal state
         *  @{ */

        /** @brief Tracked hardware cursor position. */
        CursorPos           _hw_pos         = {0, 0};
        /** @brief Reverse-video attribute state. */
        bool                _hw_inverted    = false;
        /** @brief DECAWM auto-wrap (default: on). */
        bool                _hw_auto_wrap   = true;
        /** @brief Cursor parked at last col after a row-filling write. */
        bool                _hw_at_eol      = false;
        /** @brief SM4/RM4 insert mode (toggled lazily by write_chars). */
        bool                _hw_insert_mode = false;
        /** @brief DECTCEM cursor visibility (default: hidden). Pages that want a
         *  visible cursor (only the shell editor today) opt in via
         *  set_cursor_visible() in focus(); switch_to() hides it otherwise. */
        bool                _hw_cursor_visible = false;
        /** @brief Screen has been established (cleared) at least once. Lets the
         *  first switch_to() skip a redundant clear that would tx_discard a
         *  setup_*()'s freshly-emitted handshake. */
        bool                _screen_ready = false;

        /** @}
         *  @name Cooperative-TX depth gates
         *  @{ */

        /** @brief Single paint depth floor in bytes, derived from the link baud
         *  at construction via depth_for_baud(). Shared by every cooperative
         *  paint path (status bar, page hot/cold, draw_static, replays). */
        uint16_t            _tx_cold_depth = 0;

        /** @brief RX ring depth snapshotted at the top of the loop's step()
         *  (pre-parse), surfaced by rx_used() for the serial gauge. */
        uint16_t            _rx_cycle_used = 0;

        /** @brief Worst-case bytes one paced field/label/clock can emit: a full
         *  CACHE_COLS-wide cell is a cursor move (~8) + per-column glyph/SGR
         *  toggles (~5 each). The cold floor reserves at least this much above
         *  it, so a paced write taken while tx_below_cold() can never truncate. */
        static constexpr uint16_t MAX_FIELD_TX_BYTES = 256;

        /** @brief Bytes that drain in @p target_ms at @p baud (8N1: baud/10
         *  B/s), clamped to a small floor and to leave a whole worst-case field
         *  of ring headroom above it. The byte depth is the latency target
         *  turned into a FIFO depth; it is dilation-invariant. */
        static uint16_t depth_for_baud(uint32_t baud, uint32_t target_ms) {
            static_assert(Utils::BufferedSerial::tx_capacity() > MAX_FIELD_TX_BYTES + 8,
                          "TX ring too small to guarantee a paced field above the floor");
            uint32_t d = (uint32_t)(((uint64_t)baud * target_ms) / 10000u);
            if (d < 8) d = 8;
            uint16_t cap = (uint16_t)(tx_capacity() - MAX_FIELD_TX_BYTES);
            if (d > cap) d = cap;
            return (uint16_t)d;
        }

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Initialize the device. @p baud sizes the cooperative-TX depth
         *  floor (see tx_below_cold()) so the felt echo latency stays constant
         *  across links. */
        AnsiTerminal(BufferedSerial& serial, uint16_t cols, uint16_t rows, uint32_t baud);

        /** @brief Select and initialize the target terminal type. Call once,
         *  before the first render; emits only the type's one-time handshake.
         *  - setup_minitel(): 80-col teleinformatique mode, lowercase keyboard,
         *    page mode (PRO2, sent while the unit is still native).
         *  - setup_vt102(): plain VT100/VT102 (the default). */
        void setup_minitel();
        void setup_vt102();

        /** @}
         *  @name Cursor & geometry
         *  @{ */

        /** @brief Move cursor to an absolute position. @p skip updates the
         *  internal state only, without emitting an ANSI sequence. */
        void move_to(const CursorPos& position, bool skip = false);

        /** @brief Get the current logical cursor position. */
        inline CursorPos cursor_pos() const { return _hw_pos; }

        /** @brief Current reverse-video (inverted) shadow. */
        inline bool inverted() const { return _hw_inverted; }

        /** @brief Current cursor-visibility shadow. */
        inline bool cursor_visible() const { return _hw_cursor_visible; }

        /** @brief Whether the screen has been established (cleared) at least once. */
        inline bool screen_ready() const { return _screen_ready; }

        /** @brief Snapshot of the tracked cursor/attribute shadows, for a paint
         *  that must hand the terminal back exactly as it found it. */
        struct CursorSnapshot {
            CursorPos pos;
            bool inverted;
            bool at_eol;
            bool cursor_visible;
        };

        /** @brief Capture the cursor position, reverse-video, EOL-park, and
         *  cursor-visibility shadows. */
        CursorSnapshot save_cursor() const {
            return { _hw_pos, _hw_inverted, _hw_at_eol, _hw_cursor_visible };
        }

        /** @brief Restore a save_cursor() snapshot: reverse-video and cursor
         *  visibility always, the cursor position (and EOL-park) only when
         *  @p restore_pos - a mid-rebuild caller that repositions absolutely
         *  right after passes false. */
        void restore_cursor(const CursorSnapshot& snap, bool restore_pos = true);

        /** @brief Physical display width in columns. */
        inline uint16_t cols() const { return _cols; }

        /** @brief Physical display height in rows (status row 0 + content + the
         *  reserved alarm-bar row). The alarm bar draws on the physical last
         *  row; pages size off Console::rows(), which excludes the reserved bar. */
        inline uint16_t rows_total() const { return _rows; }

        /** @}
         *  @name Text output & character attributes
         *  @{ */

        /** @brief Write characters at the cursor; @p insert uses insertion mode
         *  (shifts existing text right).
         *  @return Characters that actually reached the wire (< len when the TX
         *          ring filled), so callers can cache only what was drawn. */
        uint16_t write_chars(const char *buf, uint16_t len, bool insert = false);

        /** @brief Set or clear the reverse-video (inverted) attribute. */
        void set_inverted(bool enable);

        /** @brief Enable or disable terminal auto-wrap (DECAWM, CSI ? 7 h/l). */
        void set_auto_wrap(bool enable);

        /** @brief Show or hide the (blinking) cursor (DECTCEM). Lazy: emits only
         *  on a state change. */
        void set_cursor_visible(bool show);

        /** @brief Write spaces from the current column up to (not including)
         *  @p to_col, chunked. Keeps the current reverse-video state, so it fills
         *  a bar background. No-op once the cursor is at/past @p to_col. */
        void fill_spaces(uint16_t to_col);

        /** @brief Force the next set_inverted(@p enable) to emit even if the
         *  shadow already agrees: a preceding tx_discard can leave _hw_inverted
         *  stale, which would make a plain set_inverted() a wrong no-op. */
        void force_inverted(bool enable) { _hw_inverted = !enable; set_inverted(enable); }

        /** @brief Emit columns [@p a, @p b) of the fixed-width field @p cf at
         *  screen (@p row, @p col + a), split into reverse-video segments per
         *  @p hi. The shared run-emit used by the status bar and MetricPage.
         *  @return Leading cells of the run that reached the wire. */
        uint16_t emit_field_run(uint16_t row, uint16_t col, const char* cf,
                                uint16_t a, uint16_t b, uint32_t hi);

        /** @brief Paint the @p emit columns of field @p cf at (@p row, @p col),
         *  drawing the @p hi columns reverse-video, coalescing dirty runs. Leaves
         *  the terminal non-inverted. The shared paced-field painter for the
         *  status bar and MetricPage; the caller owns the floor/budget gate, the
         *  value cache, and the cursor-shadow save/restore. */
        void paint_field_runs(uint16_t row, uint16_t col, const char* cf,
                              uint16_t width, uint32_t emit, uint32_t hi);

        /** @}
         *  @name Content manipulation
         *  @{ */

        /** @brief Delete characters at current position, shifting the line left. */
        void delete_chars(uint16_t len);

        /** @brief Erase content from cursor to the end of the line. */
        void clear_to_eol();

        /** @brief Erase from the cursor to the end of the display. */
        void clear_to_eos();

        /** @brief Erase the entire display and home the cursor (drops queued TX
         *  first, then re-establishes). */
        void clear_screen();

        /** @brief Clear the screen and flush it straight to the wire, for a clean
         *  exit or reset (the main loop won't tick again to drain the ring).
         *  Best-effort, bounded spin on the TX flush. */
        void clear_screen_now();

        /** @brief Insert blank rows at cursor, pushing subsequent rows down. */
        void insert_rows(uint16_t count);

        /** @brief Delete rows at cursor, pulling subsequent rows up. */
        void delete_rows(uint16_t count);

        /** @brief Scroll the inclusive screen-row band [@p top .. @p bottom] by
         *  @p n rows, leaving every row outside the band untouched. @p up drops
         *  the top n rows (freed rows open at the bottom); otherwise the reverse.
         *  Built from plain DL/IL (no DECSTBM): a DL/IL pair, the second op
         *  restoring the rows the first displaced past the band; when the band
         *  runs to the physical last row the compensating op is skipped. */
        void scroll_region(uint16_t top, uint16_t bottom, bool up, uint16_t n = 1);

        /** @brief Clear screen rows [@p top .. @p bottom] inclusive (scrolls the
         *  band off by its full height - the way to blank a row range without an
         *  erase-to-end-of-display that would also wipe the alarm bar). */
        void clear_rows(uint16_t top, uint16_t bottom) {
            if (top > bottom) return;
            scroll_region(top, bottom, /*up=*/true, (uint16_t)(bottom - top + 1));
        }

        /** @brief Emit a line feed (\\n, or \\r\\n when @p carriage_return) and
         *  advance the tracked cursor. No bounds check - the caller confines it
         *  to the content band (see Console::line_feed()). */
        void newline(bool carriage_return);

        /** @}
         *  @name Screen bring-up / low-level ops (for the Console)
         *  @{ */

        /** @brief Clear+home the screen and reset cursor/wrap tracking,
         *  re-asserting DECAWM off on every call. Does not discard the TX buffer
         *  (the caller owns that: clear_screen drops stale paint; the first-step
         *  bootstrap keeps the setup_*() handshake it queued). */
        void establish_screen();

        /** @brief Snapshot the RX ring depth (pre-parse), read back by rx_used().
         *  The Console calls this once at the top of its step(). */
        void snapshot_rx() { _rx_cycle_used = _serial.available(); }

        /** @brief Drop bytes still queued for TX (they'd paint onto a display
         *  we're about to wipe or repaint). */
        void tx_discard();

        /** @brief Abort a half-sent control sequence on the wire, returning the
         *  terminal parser to ground state (emitted after a tx_discard). */
        void cancel_sequence();

        /** @brief Absolute reposition to @p pos, syncing the shadow. Used after a
         *  tx_discard leaves _hw_pos unknown (a relative move would land wrong). */
        void reset_to(const CursorPos& pos);

        /** @}
         *  @name Serial / TX
         *  @{ */

        /** @brief Read-only access to the serial device, for the diagnostic
         *  gauges (tx_dropped / rx_overrun / capacity). */
        const BufferedSerial& serial() const { return _serial; }

        /** @brief The shared paint depth floor: ring shallow enough to preserve
         *  the latency target (Config::TX_COLD_TARGET_MS) at the link baud. Every
         *  cooperative paint path gates on this one floor. */
        bool tx_below_cold() const { return tx_used() <= _tx_cold_depth; }

        /** @brief RX ring depth captured by the last snapshot_rx(), before the
         *  parser drained it (the live depth reads ~0 by the time a page paints
         *  its serial gauge). */
        uint16_t rx_used() const { return _rx_cycle_used; }

        /** @brief Bytes currently queued in the serial TX ring buffer. */
        uint16_t tx_used() const {
            uint16_t cap = Utils::BufferedSerial::tx_capacity();
            uint16_t freed = _serial.availableForWrite();
            return (freed <= cap) ? (uint16_t)(cap - freed) : 0;
        }

        /** @brief Capacity of the serial TX ring buffer, bytes. */
        static constexpr uint16_t tx_capacity() {
            return Utils::BufferedSerial::tx_capacity();
        }

        /** @} */
    };

}}

#endif // CONSOLE_TERMINAL_H
