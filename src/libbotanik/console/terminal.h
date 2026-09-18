#ifndef CONSOLE_TERMINAL_H
#define CONSOLE_TERMINAL_H

/**
 * @file terminal.h
 * @brief AnsiTerminal - a compositing screen over a VT device. Renderers draw
 *        immediate-mode into a FRONT cell grid; once a tick present() diffs it
 *        against BACK (the wire) and emits the minimal ANSI under a byte budget.
 * @warning Renderers must NOT diff by hand or scroll: redrawing an unchanged
 *          cell costs nothing. A renderer that moved rows or rippled a wrapped
 *          line says so with a LayoutChange, and present() moves the wire the
 *          same way before it diffs. See @ref dev_screen.
 */

#include <cstdint>

#include "libbotanik/config/terminal.h"

#include "libbotanik/console/ansi.h"
#include "libbotanik/utils/serial.h"

namespace Botanik { namespace Terminal {

    /** @brief The one physical terminal a Console drives.
     *
     *  These initializers ARE the factory defaults, and the `console_` rows
     *  declare the same numbers. There is no config macro behind them.
     */
    struct ConsoleConfig {
        uint32_t           baud = 4800;
        Ansi::TerminalType type = Ansi::TerminalType::VT102;
        uint16_t           cols = 80;
        uint16_t           rows = 24;
    };

    static_assert(ConsoleConfig().cols >= Config::SCREEN_MIN_COLS &&
                  ConsoleConfig().rows >= Config::SCREEN_MIN_ROWS,
                  "the default geometry is below the minimum the pages lay out for");
    static_assert(ConsoleConfig().cols <= Config::TERMINAL_MAX_COLS &&
                  ConsoleConfig().rows <= Config::TERMINAL_MAX_ROWS,
                  "the default geometry does not fit the screen back-buffer");

    /** @brief How urgently a row's changes want the wire when a tick's byte
     *  budget cannot carry all of them; lower goes first. No queue: a row that
     *  misses a tick still differs from the back-buffer on the next one. */
    enum RowPriority : uint8_t {
        PRIO_INPUT  = 0,  /**< Keystroke echo and the edit point: must feel instant. */
        PRIO_CHROME = 1,  /**< The frame around the page (status bar, alarm strip).
                           *   It outranks the page because a frame arriving after
                           *   what it frames reads as a fault, and the alarm bar is
                           *   the bottom row, so top-down order alone would put it
                           *   behind every page row. */
        PRIO_STRUCT = 2,  /**< Page structure and fresh content: drawn once, and a
                           *   page that only half-arrives looks broken. Also the
                           *   resting class of every row nobody has claimed. */
        PRIO_COUNT  = 3
    };

    /** @brief What a renderer says the screen's structure did: rows that
     *  scrolled, or cells that rippled along a wrapped line. present() moves
     *  the wire the same way before it diffs, so the diff finds the cells the
     *  move carried already in place. A wrong one costs bytes, never a wrong
     *  screen: BACK follows every escape and the diff repairs the rest. */
    struct LayoutChange {
        enum Kind : uint8_t {
            NONE,
            SCROLL,   /**< Rows [top, bottom] show at row y what row y - n showed. */
            RIPPLE,   /**< On `top`, n cells entered at `col` (n > 0) or left it
                       *   (n < 0), and what was right of them followed; on each
                       *   row down to `bottom` the same happened at `lead`, one
                       *   row's overflow being the next row's entry. */
        };
        Kind    kind;
        uint8_t top, bottom; /**< Rows involved, inclusive. */
        uint8_t col;         /**< RIPPLE: the column on `top`. */
        uint8_t lead;        /**< RIPPLE: the column on the rows under `top`. */
        uint8_t tail;        /**< RIPPLE: cells a shift on `bottom` carries. Every
                              *   row above it is full, so its carry is known. */
        int16_t n;           /**< SCROLL: rows, down positive. RIPPLE: cells,
                              *   entering positive. */

        static LayoutChange scroll(uint16_t top, uint16_t bottom, int16_t n) {
            return LayoutChange{ SCROLL, (uint8_t)top, (uint8_t)bottom, 0, 0, 0, n };
        }
        static LayoutChange ripple(uint16_t top, uint16_t bottom, uint16_t col,
                                   uint16_t lead, uint16_t tail, int16_t n) {
            return LayoutChange{ RIPPLE, (uint8_t)top, (uint8_t)bottom, (uint8_t)col,
                                 (uint8_t)lead, (uint8_t)(tail > 255 ? 255 : tail), n };
        }
    };

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
         *  Two grids indexed [row*_cols + col]; a cell is a 7-bit glyph with
         *  CELL_INV in the high bit. _front is what renderers drew, _back what
         *  present() sent. @{ */
        static constexpr uint16_t GRID_COLS  = Config::TERMINAL_MAX_COLS;
        static constexpr uint16_t GRID_ROWS  = Config::TERMINAL_MAX_ROWS;
        static constexpr uint16_t GRID_CELLS = GRID_COLS * GRID_ROWS;
        static constexpr uint8_t  CELL_INV   = 0x80;
        static constexpr uint8_t  CELL_CH    = 0x7F;
        static constexpr uint8_t  CELL_BLANK = ' ';
        static_assert(GRID_ROWS <= 32,  "the dirty set is one word");
        static_assert(GRID_COLS <= 255, "a LayoutChange holds a column in a byte");
        uint8_t _front[GRID_CELLS];
        uint8_t _back [GRID_CELLS];
        inline uint16_t _idx(uint16_t row, uint16_t col) const {
            return (uint16_t)(row * _cols + col);
        }
        /** @brief Blank the active region of both grids - after a physical
         *  clear, the wire is blank and nothing is drawn. */
        void _reset_grids();

        /** @brief Rows whose grids may differ, one bit each. A clear bit is a
         *  promise the walk can skip the row without reading it, so every write
         *  to either grid sets it and only the walk clears it. */
        uint32_t _dirty = 0;
        void _touch(uint16_t row)  { _dirty |= 1u << row; }
        /** @brief Only the rows on screen: a bit past _rows would never be
         *  cleared, and the idle beat would walk for nothing. */
        void _touch_all()          { _dirty = _mask(0, (uint16_t)(_rows - 1)); }
        static uint32_t _mask(uint16_t top, uint16_t bottom) {
            return (bottom >= 31 ? ~0u : ((1u << (bottom + 1)) - 1)) & (~0u << top);
        }
        /** @brief Under DEBUG, that a clear bit really means the two grids agree:
         *  a write path that forgets to touch its row would otherwise show as a
         *  screen that never repairs itself. */
        void _check_dirty() const;

        /** @brief Draw cursor: where the next write_chars stamps. _draw_col may
         *  equal _cols (parked past the last column after a row-filling write, so a
         *  following clear_to_eol erases nothing). */
        uint16_t _draw_row = 0, _draw_col = 0;
        /** @brief Draw pen: reverse-video applied to stamped cells. */
        bool     _draw_inv = false;
        /** @brief Desired cursor visibility (only the shell editor asks for on). */
        bool     _want_cursor_visible = false;
        /** @brief Transient suppression of a wanted cursor, kept apart from the
         *  wish above so releasing it needs nothing put back. Raised while a
         *  region is up: the highlight already says where the edit point is. */
        bool     _cursor_inhibited = false;
        /** @brief Where a visible cursor parks once present() has reconciled the
         *  screen. Set only by set_cursor_pos(), i.e. by the renderer that owns
         *  the cursor - NOT by move_to, or the last renderer to paint in the tick
         *  (a status/alarm bar) would drag the cursor off the edit point. */
        CursorPos _live_cursor_pos = {0, 0};

        /** @brief Per-row priority class, the order present() reconciles rows in.
         *  Filled by the constructor: a brace-initialiser would set only the first
         *  row and leave the rest at 0, which is PRIO_INPUT. */
        uint8_t _row_prio[GRID_ROWS];

        /** @brief Per class, the row present() resumes its scan at - wherever the
         *  tick's budget ran out. The walk only ever runs downward from there and
         *  starts a fresh pass at the top once the class is drained, so what
         *  reaches the wire in a tick is always in row order. */
        uint8_t _resume[PRIO_COUNT] = { 0 };

        /** @}
         *  @name Wire (on-the-wire) terminal state, tracked by present()
         *  @{ */

        CursorPos           _hw_pos         = {0, 0};   ///< real cursor position
        bool                _hw_inverted    = false;    ///< real reverse-video
        bool                _hw_at_eol      = false;    ///< parked at last col (pending wrap)
        bool                _hw_insert      = false;    ///< IRM (SM4) on the wire
        bool                _hw_cursor_visible = false; ///< DECTCEM on the wire
        bool                _screen_ready = false;      ///< screen established once
        /** @brief Which terminal is on the wire (constructor). */
        Ansi::TerminalType  _type;

        /** @}
         *  @name Pacing
         *  @{ */

        /** @brief Per-tick emit budget floor in bytes, derived from the link
         *  baud: present() emits at most this many above what the ring already
         *  holds, so the ring never overflows into a torn write. Moves only
         *  through set_baud(), which keeps it agreeing with the wire. */
        uint16_t            _tx_cold_depth = 0;

        /** @brief Ring headroom the budget floor always leaves free, so a single
         *  oversized run (see present()'s escape) still has somewhere to land
         *  rather than being deferred forever. */
        static constexpr uint16_t TX_HEADROOM_BYTES = Config::TX_HEADROOM_BYTES;

        static constexpr uint16_t tx_capacity() { return Utils::BufferedSerial::tx_capacity(); }
        /** @brief Bytes the TX ring holds now. */
        uint16_t tx_used() const {
            const uint16_t freed = _serial.availableForWrite();
            return freed <= tx_capacity() ? (uint16_t)(tx_capacity() - freed) : 0;
        }

        /** @brief Bytes that drain in @p target_ms at @p baud (8N1: baud/10 B/s),
         *  clamped to a small floor and to leave TX_HEADROOM_BYTES of ring free.
         *  The latency target turned into a byte depth. */
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
         *  @name Layout changes (reconcile.cpp)
         *  The changes renderers announced and present() has not yet put on
         *  the wire, oldest first. Each is applied whole or left waiting; the
         *  rows it covers wait with it, so the walk never writes cells a move
         *  is about to carry. @{ */

        static constexpr uint8_t CHANGE_QUEUE     = 6;   ///< changes a beat may leave pending
        static constexpr uint8_t CHANGE_MAX_BEATS = 4;   ///< beats a change waits for the ring
        static constexpr uint16_t SLIDE_BYTES     = 11;  ///< half a DL/IL pair: a move and the escape
        /** @brief Ring the whole DL/IL pair needs before its first escape goes:
         *  a move and an escape at each end, each escape reserving ANSI_MAX_SEQ.
         *  Half a pair on the wire with BACK unshifted is a screen nothing repairs. */
        static constexpr uint16_t SLIDE_ROOM      = 2 * (8 + 16);

        struct Queued { LayoutChange c; uint32_t left; };   ///< `left`: ripple rows still to shift
        Queued  _queue[CHANGE_QUEUE];
        uint8_t _queued    = 0;
        uint8_t _queue_age = 0;   ///< beats since a change last went out

        /** @brief Fold @p c into a pending change it continues or borders.
         *  @return false when it is a change of its own. */
        bool _merge_scroll(const LayoutChange& c);
        bool _merge_ripple(const LayoutChange& c);
        /** @brief Put the pending scrolls on the wire, oldest first, stopping
         *  at the first the ring cannot take; a ripple stays for the walk. */
        void _apply_changes();
        /** @brief Whether scroll @p c saves the walk more than its escapes
         *  cost, and how far down it should reach. */
        bool _slide_pays(const LayoutChange& c, uint16_t& bottom) const;
        /** @brief Move ripple @p q with the band [@p top, @p bottom] that just
         *  slid @p n rows, or forget it where it left the band. */
        void _carry(Queued& q, uint16_t top, uint16_t bottom, int16_t n);
        /** @brief Serve the ripples pending on @p row, each after the cells
         *  left of it. @return false when the budget ran out. */
        bool _ripple_row(uint16_t row, uint16_t& budget);
        /** @brief Rows [@p top, @p bottom] move @p n rows on the wire, as a
         *  DL/IL pair; BACK follows only if the whole pair went. */
        bool _slide(uint16_t top, uint16_t bottom, int16_t n);
        /** @brief On @p row, open @p n cells at @p at (n > 0, in insert mode,
         *  stamped from FRONT) or close |n| (n < 0, DCH), spending @p budget;
         *  BACK follows. @return false when it did not fit. */
        bool _shift(uint16_t row, uint16_t at, int16_t n, uint16_t& budget);
        /** @brief Rows the pending changes still cover, one bit each. */
        uint32_t _owed() const;
        static uint32_t _rows_of(const LayoutChange& c) { return _mask(c.top, c.bottom); }
        /** @brief Forget every pending change: the screen they described is gone. */
        void _drop_changes() { _queued = 0; _queue_age = 0; }
        /** @brief Send the row walk back to the top of every class: a slide
         *  reorders the rows under it. */
        void _restart_walk();

        /** @}
         *  @name The walk
         *  @{ */

        static constexpr uint16_t BRIDGE_CELLS = 3;   ///< widest gap rewritten rather than moved over
        static constexpr uint16_t EL_MIN_CELLS = 4;   ///< stale tail cells from which one EL pays

        /** @brief Emit the paced FRONT-vs-BACK cell diff. @return true when the
         *  screen is fully reconciled; false when work is left for the next tick. */
        bool _emit_diff();
        /** @brief Reconcile one row's changed runs and its stale tail.
         *  @return false when the tick's budget or the ring ran out. */
        bool _reconcile_row(uint16_t row, uint16_t& budget);
        /** @brief Emit the changed cells of @p row in [@p from, @p to), spending
         *  @p budget. @return false when it ran out. */
        bool _emit_runs(uint16_t row, uint16_t from, uint16_t to, uint16_t& budget);
        /** @brief Emit cells [@p a, @p b) of @p row from FRONT, @p insert in insert
         *  mode, and copy them into BACK. @return the bytes sent. */
        uint16_t _put_run(uint16_t row, uint16_t a, uint16_t b, bool insert = false);
        /** @brief Reconcile cursor visibility and park it: at the edit point when
         *  a renderer wants it visible, else idle in the bottom-right corner. */
        void _park_cursor();

        /** @brief Worst-case bytes for run [@p a, @p b) of @p row: a move, an SGR
         *  per attribute change and the glyphs. For the budget gate. */
        uint16_t _run_cost(uint16_t row, uint16_t a, uint16_t b) const;
        /** @brief Where @p row's blank tail starts: one past its last glyph. */
        uint16_t _blank_tail(const uint8_t* row) const {
            uint16_t e = _cols;
            while (e > 0 && row[e - 1] == CELL_BLANK) e--;
            return e;
        }
        /** @brief Whether a write of @p cost bytes may go out now, against the
         *  ring and this tick's @p budget. */
        bool _fits(uint16_t cost, uint16_t budget) const;
        /** @brief @p budget less @p sent, never below 0. */
        static uint16_t _spend(uint16_t budget, uint16_t sent) {
            return sent < budget ? (uint16_t)(budget - sent) : 0;
        }

        /** @brief Move the real cursor to @p target by the shortest sequence
         *  from _hw_pos. @return false if the ring could not hold it. */
        bool _go(const CursorPos& target);
        /** @brief Sync the wire's reverse video to @p inv, emitting only on a change. */
        void _set_hw_inverted(bool inv);
        /** @brief Sync the wire's insert mode to @p on, emitting only on a change.
         *  @return false if the ring could not hold it. */
        bool _set_hw_insert(bool on);

        /** @} */

    public:
        /** @brief Cells a column shift must carry to beat writing them: under
         *  this a RIPPLE's row is written rather than shifted. A producer
         *  choosing which side of a split to move reads it too. */
        static constexpr uint16_t SHIFT_MIN_CARRY = 8;

        /** @name Lifecycle
         *  @{ */

        /** @brief The wire, and nothing else. Geometry, rate and make are
         *  CONFIGURATION: they come from the bank through reconfigure() and
         *  set_baud() at PHASE_CONFIG, which is after the table is frozen and
         *  before the first render. Until then this is one usable cell. */
        explicit AnsiTerminal(BufferedSerial& serial);

        /** @brief Emit the declared terminal's one-time setup handshake. Call
         *  once before the first render. */
        void setup_terminal();

        /** @brief Re-declare what is on the console wire: its make and its
         *  size. @p cols / @p rows are clamped as the constructor clamps them.
         *  @note Blanks the screen and re-emits the setup handshake when the
         *  type moved, but does NOT repaint: the caller owns the frame.
         *  @return true when something moved, so the caller owes a repaint. */
        bool reconfigure(Ansi::TerminalType type, uint16_t cols, uint16_t rows);

        /** @}
         *  @name Cursor & geometry
         *  @{ */

        /** @brief Move the draw cursor to an absolute position (never emits; the
         *  wire cursor is present()'s business). */
        void move_to(const CursorPos& position);

        inline bool inverted() const { return _draw_inv; }
        inline bool screen_ready() const { return _screen_ready; }

        inline uint16_t cols() const { return _cols; }
        inline uint16_t rows_total() const { return _rows; }

        /** @brief Copy @p len glyphs of FRONT from (@p row, @p col) into
         *  @p out, attributes stripped; @return how many were written.
         *  @note For deciding HOW to attribute a change, never WHETHER to
         *  paint one. @see dev_screen */
        uint16_t front_cells(uint16_t row, uint16_t col, char* out,
                             uint16_t len) const {
            if (out == nullptr || row >= _rows || col >= _cols) return 0;
            const uint16_t avail = (uint16_t)(_cols - col);
            const uint16_t n     = (len < avail) ? len : avail;
            for (uint16_t i = 0; i < n; i++)
                out[i] = (char)(_front[_idx(row, (uint16_t)(col + i))] & CELL_CH);
            return n;
        }

        /** @brief Whether FRONT holds (@p row, @p col) in reverse video; false
         *  off-grid. The attribute front_cells() strips, so a caller comparing
         *  the whole cell can reach it. */
        bool front_inverted(uint16_t row, uint16_t col) const {
            if (row >= _rows || col >= _cols) return false;
            return (_front[_idx(row, col)] & CELL_INV) != 0;
        }

        /** @}
         *  @name Text output & character attributes
         *  @{ */

        /** @brief Stamp characters at the draw cursor into the FRONT grid.
         *  @return how many were stamped, NOT always @p len: the run parks at
         *  the right edge and the rest is dropped. */
        uint16_t write_chars(const char *buf, uint16_t len);

        void set_inverted(bool enable);
        void set_cursor_visible(bool show);

        /** @brief Hold a wanted cursor off the screen without clearing the wish
         *  for it, so whoever raised it does not have to know whether the page
         *  wanted a cursor in the first place. What the editor raises while a
         *  region is up. */
        void set_cursor_inhibited(bool hold) { _cursor_inhibited = hold; }

        /** @brief Re-derive the per-render TX budget for a link now at @p baud.
         *  @warning Whoever retunes the UART must call this too (Console does,
         *  from `console_baud`): pacing for a rate the wire no longer runs at
         *  either crawls or overflows the ring into a torn write. */
        void set_baud(uint32_t baud) {
            _tx_cold_depth = depth_for_baud(baud, Config::TX_COLD_TARGET_MS);
        }

        /** @brief Classify rows [@p top, @p bottom] for present()'s emit order.
         *  A class is a standing property, declared once by whoever owns the row:
         *  the Console resets the content band on a page switch, and the incoming
         *  page declares its own in focus(). Default PRIO_STRUCT. */
        void set_row_priority(uint16_t top, uint16_t bottom, RowPriority prio);
        /** @brief What class @p row is in. For a caller checking the frame
         *  still outranks what it frames. */
        RowPriority row_priority(uint16_t row) const {
            return (row < GRID_ROWS) ? (RowPriority)_row_prio[row] : PRIO_STRUCT;
        }
        void set_row_priority(uint16_t row, RowPriority prio) {
            set_row_priority(row, row, prio);
        }

        /** @brief Send present()'s row walk back to the top of every class and
         *  forget every pending change. Call it when the content changed as an
         *  ordered whole (a page switch, a full repaint). */
        void restart_scan();

        /** @brief Announce what the renderer's paint did to the screen's
         *  structure, in the coordinates the paint lands in. Changes go out in
         *  the order announced; a scroll merges into a touching one going the
         *  same way, a ripple into the one it continues. */
        void change(const LayoutChange& c);

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
        void clear_screen();
        void clear_screen_now();
        /** @brief Blank rows [@p top, @p bottom] of FRONT. */
        void clear_rows(uint16_t top, uint16_t bottom);

        /** @}
         *  @name Screen bring-up / low-level ops (for the Console)
         *  @{ */

        void establish_screen();

        /** @}
         *  @name Serial / TX
         *  @{ */

        const BufferedSerial& serial() const { return _serial; }

        /** @}
         *  @name Frame reconciliation
         *  @{ */

        /** @brief Reconcile the FRONT grid to the wire: the changes renderers
         *  announced, then the minimal cell diff, paced under this tick's byte
         *  budget. Called once per loop by the Console after renderers have drawn. */
        void present();

        /** @} */
    };

}}

#endif // CONSOLE_TERMINAL_H
