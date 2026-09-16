#ifndef CONSOLE_TERMINAL_H
#define CONSOLE_TERMINAL_H

/**
 * @file terminal.h
 * @brief AnsiTerminal - a compositing screen over a VT device. Renderers draw
 *        immediate-mode into a FRONT cell grid; once a tick present() diffs it
 *        against BACK (the wire) and emits the minimal ANSI under a byte budget.
 * @warning Pages must NOT track their own dirty regions or diff by hand -
 *          redrawing an unchanged cell already costs nothing. The one measured
 *          exception is the line editor (@ref dev_editor). See @ref dev_screen.
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
        /** @brief Transient suppression of a wanted cursor, kept apart from the
         *  wish above so releasing it needs nothing put back. Raised while a
         *  region is up: the highlight already says where the edit point is, and
         *  a cursor blinking inside it only competes with it. */
        bool     _cursor_inhibited = false;
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
        bool                _hw_auto_wrap   = true;      ///< DECAWM (kept off UI-wide)
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
         *  @name present() internals
         *  @{ */

        /** @brief Position the real cursor at @p target with the minimal sequence
         *  from _hw_pos (relative where cheaper, absolute for diagonals), tracking
         *  _hw_pos. Emits to the ring. @return false if the ring could not hold it. */
        bool _go(const CursorPos& target);
        /** @brief Sync the wire's reverse-video (SGR) state to @p inv, emitting
         *  the escape only when it changes. */
        void _set_hw_inverted(bool inv);
        /** @brief Sync the wire's insert mode (SM4/RM4) to @p on, emitting only
         *  on a change. @return false if the ring could not hold it. */
        bool _set_hw_insert(bool on);
        /** @brief Emit cells [@p a, @p b) of grid @p row from _front (positioning
         *  the cursor, toggling reverse-video per cell), copy them into _back, and
         *  return the byte cost. @p insert emits them in insert mode. */
        uint16_t _put_run(uint16_t row, uint16_t a, uint16_t b, bool insert = false);
        /** @brief Estimated worst-case bytes to emit run [@p a, @p b) of @p row
         *  (leading move + SGR toggles + glyphs), for the budget gate. */
        uint16_t _run_cost(uint16_t row, uint16_t a, uint16_t b) const;
        /** @brief Reconcile @p row as changed cells followed by a horizontal
         *  shift (SM4/RM4 insert or DCH) of everything after them. @return
         *  bytes emitted, 0 if no slide applies or none beats a rewrite.
         *  @note @p defer set means only the budget stood in the shift's way,
         *  so the row is owed the next tick rather than a full rewrite. */
        uint16_t _shift_row(uint16_t row, uint16_t budget, bool& defer);
        /** @brief Smallest changed span worth expressing as a shift. */
        static constexpr uint16_t SHIFT_MIN_SPAN   = 12;
        /** @brief Stale cells in a row's blank tail from which one EL beats
         *  writing the spaces: the escape is three bytes. */
        static constexpr uint16_t EL_MIN_CELLS     = 4;
        /** @brief Shift grid @p g for a band scroll (content moves, freed rows
         *  blanked). Called for FRONT by scroll_region and for BACK by present()
         *  once the matching DL/IL has reached the wire. */
        void _shift_grid(uint8_t* g, uint16_t top, uint16_t bottom, bool up, uint16_t n);
        /** @brief Blank the active region of both grids - after a physical
         *  clear, the wire is blank and nothing is drawn. */
        void _reset_grids();

        /** @} */

        /** @name present() phases
         *  @{ */
        /** @brief Emit each pending band DL/IL, shifting BACK on success. */
        void _emit_scrolls();
        /** @brief Emit the paced FRONT-vs-BACK cell diff. @return true when the
         *  screen is fully reconciled; false when work is left for the next tick. */
        bool _emit_diff();
        /** @brief Reconcile one row: its sideways slide, then its changed runs.
         *  @return false when the tick's budget or the ring ran out. */
        bool _reconcile_row(uint16_t row, uint16_t& budget);
        /** @brief Reconcile cursor visibility and park it: at the edit point when
         *  a renderer wants it visible, else idle in the bottom-right corner. */
        void _park_cursor();

        /** @} */

    public:
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

        /** @brief Which terminal is on the wire. */
        Ansi::TerminalType terminal_type() const { return _type; }

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

        inline CursorPos cursor_pos() const { return { _draw_row, _draw_col }; }
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

        /** @brief The whole of FRONT row @p row. @see front_cells */
        uint16_t front_row(uint16_t row, char* out, uint16_t cap) const {
            return front_cells(row, 0, out, cap);
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

        /** @brief Stamp characters at the draw cursor into the FRONT grid;
         *  @p insert shifts the row's existing cells right first. Grid-only.
         *  @return how many were stamped, NOT always @p len: nothing goes past
         *  the last row, and with auto-wrap off the run parks at the right
         *  edge and the rest is dropped. */
        uint16_t write_chars(const char *buf, uint16_t len, bool insert = false);

        void set_inverted(bool enable);
        void set_auto_wrap(bool enable);
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

        /** @brief Send present()'s row walk back to the top of every class.
         *  Call it when the content changed as an ordered whole (a band scroll,
         *  a page switch, a full repaint), or a walk parked mid-screen by the
         *  previous frame decides where the next one starts painting.
         *  scroll_region() and establish_screen() call it themselves. */
        void restart_scan();

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

        /** @brief Shift the row's cells left by @p len at the draw cursor,
         *  blanking the freed cells at the right edge (grid-only). */
        void delete_chars(uint16_t len);
        void clear_to_eol();
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


        /** @}
         *  @name Serial / TX
         *  @{ */

        const BufferedSerial& serial() const { return _serial; }
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
