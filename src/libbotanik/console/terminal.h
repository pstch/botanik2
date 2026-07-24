// REVIEW_35: TODO
#ifndef CONSOLE_TERMINAL_H
#define CONSOLE_TERMINAL_H

/**
 * @file terminal.h
 * @brief AnsiTerminal - a compositing screen over a VT device.
 *
 * The low-level half of the console. Renderers (pages, editor, bars) draw
 * immediate-mode through the usual ops (move_to / write_chars / set_inverted /
 * clear_to_eol / scroll_region ...); those ops paint into an in-memory FRONT
 * cell grid rather than the wire. Once per tick the Console calls present(),
 * which diffs FRONT against BACK (what is on the wire), emits the minimal ANSI
 * to reconcile them - paced under the tick's byte budget - and copies the
 * emitted cells into BACK. Renderers therefore keep no "what did I draw" shadow
 * of their own: redrawing an unchanged cell costs nothing because the diff drops
 * it. The Console (console.h) composes one of these and drives page
 * multiplexing, the status/alarm bars, and the per-tick step().
 */

#include <cstdint>

#include "libbotanik_config.h"
#include "libbotanik/utils/serial.h"
#include "libbotanik/console/ansi.h"

namespace Botanik { namespace Terminal {

    /** @class AnsiTerminal
     *  @brief Owns the serial link and a screen back-buffer; renderers paint into
     *  its FRONT grid and present() emits the minimal diff to the wire. See the
     *  file header. */
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
         *  @name Screen back-buffer
         *  Two cell grids, indexed [row*_cols + col]. A cell is a 7-bit glyph in
         *  the low bits with CELL_INV (reverse video) in the high bit; a blank is
         *  ' '. _front is what renderers have drawn (the desired screen); _back is
         *  what present() has put on the wire. Sized for the largest supported
         *  terminal; _cols/_rows are clamped to it at construction.
         *  @{ */
        static constexpr uint16_t GRID_COLS  = 132;
        static constexpr uint16_t GRID_ROWS  = 32;
        static constexpr uint16_t GRID_CELLS = GRID_COLS * GRID_ROWS;
        static constexpr uint8_t  CELL_INV   = 0x80;
        static constexpr uint8_t  CELL_CH    = 0x7F;
        static constexpr uint8_t  CELL_BLANK = ' ';
        uint8_t _front[GRID_CELLS];
        uint8_t _back [GRID_CELLS];
        inline uint16_t _idx(uint16_t row, uint16_t col) const {
            return (uint16_t)(row * _cols + col);
        }

        /** @brief Draw cursor: where the next write_chars stamps. _draw_col may
         *  equal _cols (parked past the last column after a row-filling write, so a
         *  following clear_to_eol erases nothing). */
        uint16_t _draw_row = 0, _draw_col = 0;
        /** @brief Draw pen: reverse-video applied to stamped cells. */
        bool     _draw_inv = false;
        /** @brief Desired cursor visibility (only the shell editor asks for on). */
        bool     _want_cursor_visible = false;
        /** @brief Where a visible cursor parks once present() has reconciled the
         *  screen. Set only by set_cursor_pos(), i.e. by the renderer that owns
         *  the cursor - NOT by move_to, or the last renderer to paint in the tick
         *  (a status/alarm bar) would drag the cursor off the edit point. */
        CursorPos _live_cursor_pos = {0, 0};

        /** @brief Band scrolls recorded by scroll_region this tick, emitted as
         *  DL/IL by present() before the cell diff (both grids were already
         *  shifted, so the diff then only repaints the revealed rows). */
        struct PendingScroll { uint16_t top, bottom, n; bool up; };
        static constexpr uint8_t MAX_SCROLLS = 8;
        PendingScroll _scrolls[MAX_SCROLLS];
        uint8_t       _nscrolls = 0;

        /** @}
         *  @name Wire (on-the-wire) terminal state, tracked by present()
         *  @{ */

        CursorPos           _hw_pos         = {0, 0};   ///< real cursor position
        bool                _hw_inverted    = false;    ///< real reverse-video
        bool                _hw_auto_wrap   = true;      ///< DECAWM (kept off UI-wide)
        bool                _hw_at_eol      = false;    ///< parked at last col (pending wrap)
        bool                _hw_cursor_visible = false; ///< DECTCEM on the wire
        bool                _screen_ready = false;      ///< screen established once

        /** @}
         *  @name Pacing
         *  @{ */

        /** @brief Per-tick emit budget floor in bytes, derived from the link baud
         *  at construction; present() emits at most this many bytes above what is
         *  already queued in the ring, so the felt latency target holds across
         *  links and the ring never overflows into a dropped (torn) write. */
        uint16_t            _tx_cold_depth = 0;

        /** @brief RX ring depth snapshotted at the top of the loop's step()
         *  (pre-parse), surfaced by rx_used() for the serial gauge. */
        uint16_t            _rx_cycle_used = 0;

        /** @brief Ring headroom the budget floor always leaves free, so a single
         *  oversized run (see present()'s escape) still has somewhere to land
         *  rather than being deferred forever. */
        static constexpr uint16_t TX_HEADROOM_BYTES = 256;

        /** @brief Bytes that drain in @p target_ms at @p baud (8N1: baud/10 B/s),
         *  clamped to a small floor and to leave TX_HEADROOM_BYTES of ring free.
         *  The latency target turned into a byte depth; dilation-invariant. */
        static uint16_t depth_for_baud(uint32_t baud, uint32_t target_ms) {
            static_assert(Utils::BufferedSerial::tx_capacity() > TX_HEADROOM_BYTES + 8,
                          "TX ring too small to keep the headroom above the floor");
            uint32_t d = (uint32_t)(((uint64_t)baud * target_ms) / 10000u);
            if (d < 8) d = 8;
            uint16_t cap = (uint16_t)(tx_capacity() - TX_HEADROOM_BYTES);
            if (d > cap) d = cap;
            return (uint16_t)d;
        }

        /** @}
         *  @name present() internals
         *  @{ */

        /** @brief Position the real cursor at @p target with the minimal sequence
         *  from _hw_pos (relative where cheaper, absolute for diagonals), tracking
         *  _hw_pos. Emits to the ring. @return false if the ring could not hold it. */
        bool _go(const CursorPos& target);
        /** @brief Emit cells [@p a, @p b) of grid @p row from _front (positioning
         *  the cursor, toggling reverse-video per cell), copy them into _back, and
         *  return the byte cost. */
        uint16_t _put_run(uint16_t row, uint16_t a, uint16_t b);
        /** @brief Estimated worst-case bytes to emit run [@p a, @p b) of @p row
         *  (leading move + SGR toggles + glyphs), for the budget gate. */
        uint16_t _run_cost(uint16_t row, uint16_t a, uint16_t b) const;
        /** @brief Shift grid @p g for a band scroll (content moves, freed rows
         *  blanked). Called for FRONT by scroll_region and for BACK by present()
         *  once the matching DL/IL has reached the wire. */
        void _shift_grid(uint8_t* g, uint16_t top, uint16_t bottom, bool up, uint16_t n);
        /** @brief Blank the whole back-buffer (both grids) - after a physical
         *  clear, the wire is blank and nothing is drawn. */
        void _reset_grids();
        /** @name present() phases
         *  @{ */
        /** @brief Emit each pending band DL/IL, shifting BACK on success. */
        void _emit_scrolls();
        /** @brief Emit the paced FRONT-vs-BACK cell diff. @return true when the
         *  screen is fully reconciled; false when work is left for the next tick. */
        bool _emit_diff();
        /** @brief Reconcile cursor visibility and park it: at the edit point when
         *  a renderer wants it visible, else idle in the bottom-right corner. */
        void _park_cursor();
        /** @} */

        /** @brief Wire-side DL/IL, used only by present() to emit a band scroll. */
        void delete_rows_wire(uint16_t count);
        void insert_rows_wire(uint16_t count);

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        AnsiTerminal(BufferedSerial& serial, uint16_t cols, uint16_t rows, uint32_t baud);

        void setup_minitel();
        void setup_vt102();

        /** @}
         *  @name Cursor & geometry
         *  @{ */

        /** @brief Move the draw cursor to an absolute position (never emits; the
         *  wire cursor is present()'s business). */
        void move_to(const CursorPos& position);

        inline CursorPos cursor_pos() const { return { _draw_row, _draw_col }; }
        inline bool inverted() const { return _draw_inv; }
        inline bool screen_ready() const { return _screen_ready; }

        inline uint16_t cols() const { return _cols; }
        inline uint16_t rows_total() const { return _rows; }

        /** @}
         *  @name Text output & character attributes
         *  @{ */

        /** @brief Stamp characters at the draw cursor into the FRONT grid.
         *  @return @p len (a grid stamp never drops - pacing happens in
         *  present()). */
        uint16_t write_chars(const char *buf, uint16_t len);

        void set_inverted(bool enable);
        void set_auto_wrap(bool enable);
        void set_cursor_visible(bool show);

        /** @brief Declare where the visible cursor belongs. The renderer owning
         *  the cursor (the editor) calls this whenever it moves; present() parks
         *  there after reconciling. */
        void set_cursor_pos(const CursorPos& p) { _live_cursor_pos = p; }

        void fill_spaces(uint16_t to_col);

        /** @brief Paint the whole fixed-width field @p cf at (@p row, @p col),
         *  split into reverse-video segments per the @p hi column mask. The
         *  back-buffer diff drops whatever did not actually change. */
        void paint_field_runs(uint16_t row, uint16_t col, const char* cf,
                              uint16_t width, uint32_t hi);

        /** @}
         *  @name Content manipulation
         *  @{ */

        void clear_to_eol();
        void clear_to_eos();
        void clear_screen();
        void clear_screen_now();
        void scroll_region(uint16_t top, uint16_t bottom, bool up, uint16_t n = 1);

        void clear_rows(uint16_t top, uint16_t bottom) {
            if (top > bottom) return;
            scroll_region(top, bottom, /*up=*/true, (uint16_t)(bottom - top + 1));
        }

        void newline(bool carriage_return);

        /** @}
         *  @name Screen bring-up / low-level ops (for the Console)
         *  @{ */

        void establish_screen();
        void snapshot_rx() { _rx_cycle_used = _serial.available(); }

        /** @brief Abort a half-sent control sequence on the wire, returning the
         *  terminal parser to ground state. */
        void cancel_sequence();

        /** @}
         *  @name Serial / TX
         *  @{ */

        const BufferedSerial& serial() const { return _serial; }
        uint16_t rx_used() const { return _rx_cycle_used; }
        uint16_t tx_used() const {
            uint16_t cap = Utils::BufferedSerial::tx_capacity();
            uint16_t freed = _serial.availableForWrite();
            return (freed <= cap) ? (uint16_t)(cap - freed) : 0;
        }
        static constexpr uint16_t tx_capacity() {
            return Utils::BufferedSerial::tx_capacity();
        }

        /** @}
         *  @name Frame reconciliation
         *  @{ */

        /** @brief Reconcile the FRONT grid to the wire: emit any pending band
         *  scrolls, then the minimal cell diff, paced under this tick's byte
         *  budget. Called once per loop by the Console after renderers have drawn. */
        void present();

        /** @} */
    };

}}

#endif // CONSOLE_TERMINAL_H
