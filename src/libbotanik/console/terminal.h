#ifndef CONSOLE_TERMINAL_H
#define CONSOLE_TERMINAL_H

/**
 * @file terminal.h
 * @brief AnsiTerminal - a compositing screen over a VT device. Renderers draw
 *        immediate-mode into a FRONT grid; once a tick present() diffs it
 *        against BACK (what the wire shows) under a byte budget.
 * @warning Renderers never diff, scroll or emit escapes. One that moved rows
 *          or rippled a wrapped line announces a LayoutChange, and present()
 *          moves the wire the same way before it diffs. See @ref dev_screen.
 */

#include <cstdint>

#include "libbotanik/config/terminal.h"

#include "libbotanik/console/ansi.h"
#include "libbotanik/utils/serial.h"

namespace Botanik { namespace Terminal {

    /** @brief The one physical terminal a Console drives. These initializers
     *  ARE the factory defaults; the `console_` rows declare the same numbers. */
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

    /** @brief The order rows reach the wire in when a tick's budget cannot
     *  carry them all; lower first. A row that misses a tick still differs
     *  from BACK on the next one, so nothing is queued. */
    enum RowPriority : uint8_t {
        PRIO_INPUT  = 0,  /**< Keystroke echo and the edit point. */
        PRIO_CHROME = 1,  /**< Status bar and alarm strip: the frame lands before
                           *   what it frames, whichever row it is on. */
        PRIO_STRUCT = 2,  /**< Page content, and the resting class of every row. */
        PRIO_COUNT  = 3
    };

    /** @brief What a renderer's paint did to the screen's structure. present()
     *  moves the wire the same way, then diffs. BACK follows every escape, so
     *  a wrong one costs bytes and never a wrong screen. */
    struct LayoutChange {
        enum Kind : uint8_t {
            NONE,
            SCROLL,   /**< Rows [top, bottom] show at row y what row y - n showed. */
            RIPPLE,   /**< On `top`, n cells entered at `col` (n > 0) or left it
                       *   (n < 0) and what was right of them followed; each row
                       *   down to `bottom` did the same at `lead`, one row's
                       *   overflow being the next row's entry. */
        };
        Kind    kind;
        uint8_t top, bottom; /**< Rows involved, inclusive. */
        uint8_t col;         /**< RIPPLE: the column on `top`. */
        uint8_t lead;        /**< RIPPLE: the column on the rows under `top`. */
        uint8_t tail;        /**< RIPPLE: cells a shift on `bottom` carries; the
                              *   rows above it are full. */
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
     *  @brief The serial link, a FRONT grid renderers paint into and a BACK
     *  grid mirroring the wire. See the file header. */
    class AnsiTerminal {
        using BufferedSerial = Utils::BufferedSerial;
        using CursorPos = Terminal::Ansi::CursorPos;

        BufferedSerial&     _serial;
        uint16_t            _cols;          /**< Active width, at most GRID_COLS. */
        uint16_t            _rows;          /**< Active height, at most GRID_ROWS. */

        /** @name Grids
         *  Indexed [row*_cols + col]; a cell is a 7-bit glyph with CELL_INV in
         *  the high bit. @{ */
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
        /** @brief Blank both grids over the active region. */
        void _reset_grids();
        /** @brief Blank rows [@p top, @p bottom] of FRONT. */
        void clear_rows(uint16_t top, uint16_t bottom);

        /** @brief One bit per row whose grids may differ. A clear bit is a
         *  promise the walk can skip the row unread: every write to either
         *  grid sets it, only the walk clears it. */
        uint32_t _dirty = 0;
        void _touch(uint16_t row)  { _dirty |= 1u << row; }
        /** @brief Rows on screen only: a bit past _rows would never clear. */
        void _touch_all()          { _dirty = _mask(0, (uint16_t)(_rows - 1)); }
        static uint32_t _mask(uint16_t top, uint16_t bottom) {
            return (bottom >= 31 ? ~0u : ((1u << (bottom + 1)) - 1)) & (~0u << top);
        }
        /** @brief Under DEBUG, that a clear bit means the grids agree. */
        void _check_dirty() const;

        /** @}
         *  @name Draw state
         *  @{ */

        /** @brief Where the next write_chars stamps. _draw_col may equal _cols:
         *  parked past the edge, so a following clear_to_eol erases nothing. */
        uint16_t _draw_row = 0, _draw_col = 0;
        bool     _draw_inv = false;              /**< Reverse video on stamped cells. */
        bool     _want_cursor_visible = false;
        /** @brief Holds a wanted cursor off without clearing the wish: raised
         *  while a region highlight marks the edit point. */
        bool     _cursor_inhibited = false;
        /** @brief Where a visible cursor parks after a reconcile. Set only by
         *  set_cursor_pos(), never by move_to: the last renderer to paint (a
         *  bar) must not drag it off the edit point. */
        CursorPos _live_cursor_pos = {0, 0};
        /** @brief Per-row RowPriority. Filled by the constructor: a brace
         *  initialiser would leave the rest at 0, which is PRIO_INPUT. */
        uint8_t _row_prio[GRID_ROWS];
        /** @brief Per class, the row the walk resumes at when a tick's budget
         *  ran out; it only runs downward from there, so a tick's emissions
         *  stay in row order. */
        uint8_t _resume[PRIO_COUNT] = { 0 };

        /** @}
         *  @name Wire state, tracked by present()
         *  @{ */

        CursorPos           _hw_pos         = {0, 0};
        bool                _hw_inverted    = false;
        bool                _hw_at_eol      = false;    /**< Parked on the last column. */
        bool                _hw_insert      = false;    /**< IRM (SM4) on. */
        bool                _hw_cursor_visible = false;
        bool                _screen_ready = false;      /**< Established once. */
        Ansi::TerminalType  _type;

        /** @}
         *  @name Pacing
         *  @{ */

        /** @brief Bytes present() may emit above what the ring holds, from
         *  the baud through set_baud(). Beyond it the ring overflows into a
         *  torn write. */
        uint16_t            _tx_cold_depth = 0;
        /** @brief Ring the budget always leaves free, so one oversized run has
         *  somewhere to land. */
        static constexpr uint16_t TX_HEADROOM_BYTES = Config::TX_HEADROOM_BYTES;

        static constexpr uint16_t tx_capacity() { return Utils::BufferedSerial::tx_capacity(); }
        uint16_t tx_used() const {
            const uint16_t freed = _serial.availableForWrite();
            return freed <= tx_capacity() ? (uint16_t)(tx_capacity() - freed) : 0;
        }
        /** @brief Bytes that drain in @p target_ms at @p baud (8N1), between
         *  a floor of 8 and the ring less its headroom. */
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
         *  Announced and not yet on the wire, oldest first. A scroll goes
         *  before the walk and its rows wait for it; a ripple is served by the
         *  walk when it reaches each row. @{ */

        static constexpr uint8_t CHANGE_QUEUE     = 6;
        static constexpr uint8_t CHANGE_MAX_BEATS = 4;   /**< Beats a change waits for the ring. */
        /** @brief Ring a DL/IL pair needs before its first byte: half a pair
         *  on the wire with BACK unshifted is a screen nothing repairs. */
        static constexpr uint16_t SLIDE_ROOM      = 2 * (8 + 16);

        struct Queued { LayoutChange c; uint32_t left; };   /**< `left`: ripple rows still to shift. */
        Queued  _queue[CHANGE_QUEUE];
        uint8_t _queued    = 0;
        uint8_t _queue_age = 0;   /**< Beats since a change last went out. */

        /** @brief Fold @p c into a pending change it continues or borders.
         *  @return false when it is a change of its own. */
        bool _merge_scroll(const LayoutChange& c);
        bool _merge_ripple(const LayoutChange& c);
        /** @brief Emit the pending scrolls, stopping at the first the ring
         *  cannot take. */
        void _apply_changes();
        /** @brief Serve the ripples pending on @p row, each after the cells
         *  left of it. @return false when the budget ran out. */
        bool _ripple_row(uint16_t row, uint16_t& budget);
        /** @brief Rows [@p top, @p bottom] move @p n rows, as a DL/IL pair;
         *  BACK follows only if the whole pair went. */
        bool _slide(uint16_t top, uint16_t bottom, int16_t n);
        /** @brief On @p row, open @p n cells at @p at (n > 0, insert mode,
         *  stamped from FRONT) or close |n| (n < 0, DCH), spending @p budget;
         *  BACK follows. @return false when it did not fit. */
        bool _shift(uint16_t row, uint16_t at, int16_t n, uint16_t& budget);
        /** @brief Rows the pending scrolls still cover. */
        uint32_t _owed() const;
        static uint32_t _rows_of(const LayoutChange& c) { return _mask(c.top, c.bottom); }
        void _drop_changes() { _queued = 0; _queue_age = 0; }
        void _restart_walk();

        /** @}
         *  @name The walk
         *  @{ */

        static constexpr uint16_t BRIDGE_CELLS = 3;   /**< Widest unchanged gap a run crosses. */
        static constexpr uint16_t EL_MIN_CELLS = 4;   /**< Stale tail cells from which one EL pays. */

        /** @brief The paced FRONT-vs-BACK diff. @return true once the screen
         *  is fully reconciled. */
        bool _emit_diff();
        /** @brief One row's ripples, changed runs and stale tail.
         *  @return false when the budget or the ring ran out. */
        bool _reconcile_row(uint16_t row, uint16_t& budget);
        /** @brief The changed runs of @p row in [@p from, @p to). */
        bool _emit_runs(uint16_t row, uint16_t from, uint16_t to, uint16_t& budget);
        /** @brief Cells [@p a, @p b) of @p row from FRONT, copied into BACK.
         *  @return the bytes sent. */
        uint16_t _put_run(uint16_t row, uint16_t a, uint16_t b, bool insert = false);
        /** @brief Cursor visibility, then its place: the edit point, or the
         *  bottom-right corner when nobody owns it. */
        void _park_cursor();

        /** @brief Worst-case bytes for run [@p a, @p b): a move, an SGR per
         *  attribute change, the glyphs. */
        uint16_t _run_cost(uint16_t row, uint16_t a, uint16_t b) const;
        /** @brief One past @p row's last glyph. */
        uint16_t _blank_tail(const uint8_t* row) const {
            uint16_t e = _cols;
            while (e > 0 && row[e - 1] == CELL_BLANK) e--;
            return e;
        }
        /** @brief Whether @p cost bytes may go out now, against the ring and
         *  the tick's @p budget. */
        bool _fits(uint16_t cost, uint16_t budget) const;
        static uint16_t _spend(uint16_t budget, uint16_t sent) {
            return sent < budget ? (uint16_t)(budget - sent) : 0;
        }

        /** @brief The real cursor to @p target by the shortest sequence.
         *  @return false if the ring could not hold it. */
        bool _go(const CursorPos& target);
        void _set_hw_inverted(bool inv);
        bool _set_hw_insert(bool on);

        /** @} */

    public:
        /** @brief Cells a column shift must carry to beat writing them. A
         *  producer choosing which side of a split to move reads it too. */
        static constexpr uint16_t SHIFT_MIN_CARRY = 8;

        /** @name Lifecycle
         *  @{ */

        /** @brief The wire only. Geometry, rate and make arrive through
         *  reconfigure() and set_baud() at PHASE_CONFIG; until then this is
         *  one usable cell. */
        explicit AnsiTerminal(BufferedSerial& serial);

        /** @brief The declared terminal's one-time setup handshake. */
        void setup_terminal();

        /** @brief Re-declare the wire's make and size, clamped to the grid.
         *  Blanks the screen and does NOT repaint: the caller owns the frame.
         *  @return true when something moved, so a repaint is owed. */
        bool reconfigure(Ansi::TerminalType type, uint16_t cols, uint16_t rows);

        /** @}
         *  @name Draw cursor and geometry
         *  @{ */

        /** @brief Move the draw cursor; never emits. */
        void move_to(const CursorPos& position);

        inline bool inverted() const { return _draw_inv; }
        inline bool screen_ready() const { return _screen_ready; }
        inline uint16_t cols() const { return _cols; }
        inline uint16_t rows_total() const { return _rows; }

        /** @brief Copy @p len glyphs of FRONT from (@p row, @p col) into
         *  @p out, attributes stripped. @return how many were written. */
        uint16_t front_cells(uint16_t row, uint16_t col, char* out,
                             uint16_t len) const {
            if (out == nullptr || row >= _rows || col >= _cols) return 0;
            const uint16_t avail = (uint16_t)(_cols - col);
            const uint16_t n     = (len < avail) ? len : avail;
            for (uint16_t i = 0; i < n; i++)
                out[i] = (char)(_front[_idx(row, (uint16_t)(col + i))] & CELL_CH);
            return n;
        }

        /** @brief Whether FRONT holds (@p row, @p col) in reverse video. */
        bool front_inverted(uint16_t row, uint16_t col) const {
            if (row >= _rows || col >= _cols) return false;
            return (_front[_idx(row, col)] & CELL_INV) != 0;
        }

        /** @}
         *  @name Painting into FRONT
         *  @{ */

        /** @brief Stamp characters at the draw cursor. @return how many were
         *  stamped: the run parks at the right edge and the rest is dropped. */
        uint16_t write_chars(const char *buf, uint16_t len);

        void set_inverted(bool enable);
        void set_cursor_visible(bool show);

        /** @brief Hold a wanted cursor off without clearing the wish; what the
         *  editor raises while a region is up. */
        void set_cursor_inhibited(bool hold) { _cursor_inhibited = hold; }

        /** @brief Re-derive the byte budget for a link now at @p baud.
         *  @warning Whoever retunes the UART calls this too, or pacing for the
         *  wrong rate crawls or tears. */
        void set_baud(uint32_t baud) {
            _tx_cold_depth = depth_for_baud(baud, Config::TX_COLD_TARGET_MS);
        }

        /** @brief Class rows [@p top, @p bottom] for the emit order. A
         *  standing property: the Console resets the content band on a page
         *  switch and the incoming page declares its own in focus(). */
        void set_row_priority(uint16_t top, uint16_t bottom, RowPriority prio);
        void set_row_priority(uint16_t row, RowPriority prio) {
            set_row_priority(row, row, prio);
        }

        /** @brief Send the walk back to the top of every class and forget
         *  every pending change: the content changed as an ordered whole. */
        void restart_scan();

        /** @brief Announce what the renderer's paint did, in the coordinates
         *  the paint lands in. Changes go out in announcement order; a scroll
         *  merges into a touching one going the same way, a ripple into the
         *  one it continues. */
        void change(const LayoutChange& c);

        /** @brief Where the visible cursor belongs; the renderer owning it
         *  calls this whenever it moves. */
        void set_cursor_pos(const CursorPos& p) { _live_cursor_pos = p; }

        void fill_spaces(uint16_t to_col);

        /** @brief Paint the fixed-width field @p cf at (@p row, @p col), in
         *  reverse video where the @p hi column mask says. */
        void paint_field_runs(uint16_t row, uint16_t col, const char* cf,
                              uint16_t width, uint32_t hi);

        void clear_to_eol();
        void clear_screen();
        void clear_screen_now();
        /** @brief Blank rows [@p top, @p bottom] on the wire now as well as
         *  in FRONT, one EL per row so nothing else moves, and forget every
         *  pending change: a page switch. */
        void blank_rows(uint16_t top, uint16_t bottom);

        void establish_screen();

        const BufferedSerial& serial() const { return _serial; }

        /** @}
         *  @name Frame reconciliation
         *  @{ */

        /** @brief Reconcile FRONT to the wire: the announced changes, then the
         *  cell diff, paced. Once per loop, after every renderer has drawn. */
        void present();

        /** @} */
    };

}}

#endif // CONSOLE_TERMINAL_H
