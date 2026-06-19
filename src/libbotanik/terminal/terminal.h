#ifndef TERMINAL_TERMINAL_H
#define TERMINAL_TERMINAL_H

/**
 * @file terminal.h
 * @brief AnsiTerminal - owns the serial connection and drives the multiplexed
 *        page display, routing input events to the active page and managing the
 *        cursor, geometry, and status bar.
 */

#include <cstdint>

#include "libbotanik/utils/serial.h"
#include "libbotanik/terminal/ansi.h"
#include "libbotanik/context.h"

#include "libbotanik/pages/page.h"

namespace Botanik { namespace Alarms { class AlarmModel; } }

namespace Botanik { namespace Terminal {


    /** @class AnsiTerminal
     *  @brief Owns the serial link and drives the multiplexed page display: routes
     *         input to the active page, tracks cursor/geometry/attribute state, and
     *         renders the row-0 status bar.
     */
    class AnsiTerminal {
        using BufferedSerial = Utils::BufferedSerial;
        using CursorPos = Terminal::Ansi::CursorPos;

        /** @name Input & pages
         *  @{ */

        /** @brief Input parser used to read from the remote terminal. */
        InputParser         _parser;
        /** @brief Child pages multiplexed onto the display. */
        Pages::Page*        _pages[8];
        /** @brief Number of registered pages. */
        uint8_t             _pages_len  = 0;
        /** @brief Active page index. */
        uint8_t             _page_index = 0;

        /** @} */

        /** @name External references
         *  @{ */

        /** @brief Buffered serial UART wrapper. */
        BufferedSerial&              _serial;
        /** @brief Aggregate of subsystem status contexts for the status bar and
         *  the `stats` command (scheduler + PLC; members may be null). */
        SystemContext                _ctx       = {};

        /** @} */

        /** @name Display geometry
         *  @{ */

        /** @brief Display width in columns. */
        uint16_t            _cols;
        /** @brief Display height in rows. */
        uint16_t            _rows;

        /** @} */

        /** @name Hardware terminal state
         *  @{ */

        /** @brief Tracked hardware cursor position. */
        CursorPos           _hw_pos = {0, 0};
        /** @brief Reverse-video (inverted) attribute state. */
        bool                _hw_inverted = false;
        /** @brief DECAWM auto-wrap state (default: on). */
        bool                _hw_auto_wrap = true;
        /** @brief Cursor parked at last col after a row-filling write. */
        bool                _hw_at_eol = false;
        /** @brief SM4/RM4 insert mode - toggled lazily by write_chars. */
        bool                _hw_insert_mode = false;
        /** @brief DECTCEM cursor visibility (default: hidden). Pages that want a
         *  visible cursor (only the shell editor today) opt in via
         *  set_cursor_visible() in focus(); switch_to() hides it for everyone else. */
        bool                _hw_cursor_visible = false;
        /** @brief Screen has been established (cleared) at least once - set by
         *  setup_*() / clear_screen(). Lets the first switch_to() skip a redundant
         *  clear that would tx_discard a setup_*()'s freshly-emitted handshake. */
        bool                _screen_ready = false;

        /** @} */

        /** @name Status bar state
         *  @{ */

        /** @brief Last status-metrics refresh (wall clock). */
        uint64_t            _status_last_ms = 0;
        /** @brief Visible width (+NUL) of the right-aligned status field
         *  "PLC:xxx cpu:nnn% stk:nnn% ". Both the build buffer and the diff cache
         *  are sized from this one constant so they can never drift apart. */
        static constexpr uint16_t    STATUS_METRICS_LEN = 27;
        /** @brief Diff cache of the last drawn metrics (zero-init forces first draw). */
        char                         _status_metrics_cache[STATUS_METRICS_LEN] = {};
        /** @brief Last status-bar clock refresh (uptime ms). */
        uint64_t            _status_clock_last_ms = 0;
        /** @brief Visible width of the HH:MM:SS clock field (no NUL). */
        static constexpr uint16_t    STATUS_CLOCK_LEN = 8;
        /** @brief Diff cache of the last drawn clock (zero-init forces first draw). */
        char                         _status_clock_cache[STATUS_CLOCK_LEN] = {};
        /** @brief Round-robin cursor over the status-bar fields (0 = clock, 1 = metrics),
         *  so a tight TX_STATUS_FIELDS_PER_TICK budget rotates which one repaints first
         *  rather than always favouring the clock. */
        uint8_t                      _status_field = 0;

        /** @} */

        /** @name Alarm bar state
         *  The bottom alarm bar is a terminal-owned hot field, drawn on every page so
         *  alarms stay visible. Like the row-0 status bar it repaints cooperatively in
         *  step() - only when the unmasked alarm set changes, and only while the TX
         *  ring is shallow (the shared cold floor). No change-highlighting (the set is
         *  visible as a whole). _alarm_dirty survives a deferred paint (floor closed,
         *  or the row was wiped by a clear) so a later tick retries.
         *  @{ */

        /** @brief Alarm source feeding the bar (may be null). When null there is no
         *  bar, _reserved_bottom stays 0, and the bottom row belongs to pages. */
        const Alarms::AlarmModel*    _alarms = nullptr;
        /** @brief Rows reserved at the screen bottom for the bar: 1 when _alarms is
         *  set, else 0. Subtracted by rows() / _content_bottom(). */
        uint16_t                     _reserved_bottom = 0;
        /** @brief Unmasked alarm set last painted (bit i = table row i). */
        uint64_t                     _alarm_sig = 0;
        /** @brief A bar repaint is due (set on change / screen wipe, cleared once painted). */
        bool                         _alarm_dirty = true;

        /** @} */

        /** @name Cooperative-TX depth gates
         *  @{ */

        /** @brief Single paint depth floor in bytes, derived from the link baud at
         *  construction via depth_for_baud(). Shared by every cooperative paint path
         *  (status bar, page hot, page cold, draw_static, replays). See tx_below_cold(). */
        uint16_t            _tx_cold_depth = 0;

        /** @brief RX ring depth snapshotted at the top of step() (pre-parse), surfaced
         *  by rx_used() for the serial gauge. */
        uint16_t            _rx_cycle_used = 0;

        /** @brief Bytes that drain in @p target_ms at @p baud (8N1: baud/10 B/s),
         *  clamped to a small floor and below the ring capacity. The byte depth is
         *  the latency target turned into a FIFO depth; it is dilation-invariant. */
        static uint16_t depth_for_baud(uint32_t baud, uint32_t target_ms) {
            uint32_t d = (uint32_t)(((uint64_t)baud * target_ms) / 10000u);
            if (d < 8) d = 8;
            uint16_t cap = (uint16_t)(tx_capacity() - 64);   // leave ring headroom
            if (d > cap) d = cap;
            return (uint16_t)d;
        }

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Initialize the terminal. @p baud is the link bit rate; it sizes the
         *  cooperative-TX depth floor (see tx_below_cold()) so the felt echo latency
         *  stays constant across links. @p alarms, when non-null, feeds the always-
         *  visible bottom alarm bar and reserves the bottom screen row for it (the
         *  page content area shrinks by one row); null leaves the full screen to pages. */
        AnsiTerminal(Utils::BufferedSerial& serial, uint16_t cols, uint16_t rows,
                     const uint32_t* timestamp_ms, uint32_t baud, const SystemContext& ctx = {},
                     const Alarms::AlarmModel* alarms = nullptr);

        /** @brief Process input and update terminal state */
        void step();

        /** @brief Select and initialize the target terminal type. Call once, before
         *  the first render. Emits only the type's one-time handshake; the screen
         *  clear is deferred to the first step() so it lands after this handshake.
         *  - setup_minitel(): 80-col teleinformatique mode, lowercase keyboard, page
         *    mode over the DIN link (PRO2, sent while the unit is still native).
         *  - setup_vt102(): plain VT100/VT102. The default if neither is called. */
        void setup_minitel();
        void setup_vt102();

        /** @} */

        /** @name Cursor & geometry
         *  @{ */

        /** @brief Move cursor to an absolute position
         *  @param position Targeted coordinates
         *  @param skip If true, only update internal state without sending ANSI sequence
         */
        void move_to(const CursorPos& position, bool skip = false);

        /** @brief Get the current logical cursor position */
        inline CursorPos cursor_pos() const { return _hw_pos; }

        /** @brief Get the total number of columns (width) */
        inline uint16_t cols() const { return _cols; }

        /** @brief Rows available to pages (height minus the reserved bottom alarm
         *  bar, if any). Row 0 is the status bar and this count includes it, so the
         *  page content area is rows 1 .. rows()-1, mirroring how row 0 is handled.
         *  Pages and the editor size their layout and scroll bands off this, so
         *  reserving the alarm bar's row shrinks them by one with no other change. */
        inline uint16_t rows() const { return (uint16_t)(_rows - _reserved_bottom); }

        /** @} */

        /** @name Text output & character attributes
         *  @{ */

        /** @brief Write characters at the current cursor position
         *  @param buf Buffer to write
         *  @param len Number of characters to write
         *  @param insert If true, use insertion mode (shifts existing text right)
         *  @return Number of characters that actually reached the wire (< len when
         *          the TX ring filled), so callers can cache only what was drawn. */
        uint16_t write_chars(const char *buf, uint16_t len, bool insert = false);

        /** @brief Set or clear the reverse-video (inverted) attribute */
        void set_inverted(bool enable);

        /** @brief Enable or disable terminal auto-wrap (DECAWM, CSI ? 7 h/l) */
        void set_auto_wrap(bool enable);

        /** @brief Show or hide the (blinking) cursor (DECTCEM). Lazy: emits only on
         *  a state change. There is no steady cursor, so callers that want to stop
         *  the blink simply hide it. */
        void set_cursor_visible(bool show);

        /** @brief Emit columns [@p a, @p b) of the fixed-width field @p cf, positioned
         *  at screen (@p row, @p col + a), split into reverse-video segments per @p hi
         *  (bit k set = column k drawn inverted). The shared "cursor move + lazy SGR
         *  segmenting + write" run-emit used by both the status bar and MetricPage; the
         *  caller owns diffing, run-coalescing, budgeting, the value cache, and any
         *  inverted-state restore afterwards.
         *  @return Number of leading cells of the run that actually reached the wire
         *          (< b-a if the TX ring filled mid-run), so the caller caches only
         *          what was drawn. */
        uint16_t emit_field_run(uint16_t row, uint16_t col, const char* cf,
                                uint16_t a, uint16_t b, uint32_t hi);

        /** @} */

        /** @name Content manipulation
         *  @{ */

        /** @brief Delete characters at current position, shifting the line left */
        void delete_chars(uint16_t len);

        /** @brief Erase content from cursor to the end of the line */
        void clear_to_eol();

        /** @brief Erase from the cursor to the end of the display */
        void clear_to_eos();

        /** @brief Erase the entire display and home the cursor (ESC[2J + ESC[H) */
        void clear_screen();

        /** @brief Erase the page content area (rows 1..N-1), preserving the status bar */
        void clear_content();

        /** @brief Emit a line feed, optionally with a carriage return */
        void line_feed(bool carriage_return = true);

        /** @brief Insert blank rows at cursor, pushing subsequent rows down */
        void insert_rows(uint16_t count);

        /** @brief Delete rows at cursor, pulling subsequent rows up */
        void delete_rows(uint16_t count);

        /** @brief Scroll the inclusive screen-row band [@p top .. @p bottom] by @p n
         *  rows, leaving every row outside the band untouched. @p up scrolls toward
         *  the top (the n freed rows open at the band bottom, the top n are dropped);
         *  otherwise toward the bottom (freed rows open at the top). The caller fills
         *  the freed rows.
         *
         *  There is no DECSTBM scrolling region (a Minitel in teleinformatique mode
         *  does not honour it), so this is built from plain DL/IL: a DL/IL pair, one at
         *  each end of the band, where the second op pushes the rows the first op
         *  displaced past the band back to where they were (the pattern the scrollback
         *  live-tail already used inline). When the band runs to the physical last row
         *  there is nothing below to restore, so the compensating op is skipped and a
         *  single DL/IL is emitted - identical to a bare insert_rows()/delete_rows(). */
        void scroll_region(uint16_t top, uint16_t bottom, bool up, uint16_t n = 1);

        /** @brief Clear screen rows [@p top .. @p bottom] inclusive, leaving every row
         *  outside the range untouched (the status bar above, the alarm bar below, any
         *  content above @p top). X3.64 has no bounded-range erase, so this scrolls the
         *  band off by its full height (a DL/IL pair) - the way to blank a row range
         *  without an erase-to-end-of-display that would also wipe the alarm bar. */
        void clear_rows(uint16_t top, uint16_t bottom) {
            if (top > bottom) return;
            scroll_region(top, bottom, /*up=*/true, (uint16_t)(bottom - top + 1));
        }

        /** @} */

        /** @name Pages
         *  @{ */

        /** @brief Register a page; returns its index, or -1 if the slot array is full */
        int8_t add_page(Pages::Page* page);

        /** @brief Insert a page at @p index, shifting later pages right; returns the
         *  index, or -1 if the array is full or @p index is past the end. Lets a site
         *  splice its pages into the core set (e.g. its home page at index 0) without
         *  re-listing them. Intended for setup() before the first render. */
        int8_t add_page(Pages::Page* page, uint8_t index);

        /** @brief Switch to the page at the given index */
        void switch_to(uint8_t index);

        /** @brief Switch to the next (@p forward) or previous page, wrapping around
         *  the ends. No-op when no pages are registered. */
        void switch_relative(bool forward);

        /** @} */

        /** @name Status bar
         *  @{ */

        /** @brief Redraw the always-visible status bar on row 0 */
        void draw_status_row();

        /** @brief Redraw a single page tab in its current active/inactive video.
         *  Used by switch_to() to flip just the two tabs that change on a switch
         *  instead of rebuilding the whole bar. */
        void draw_status_tab(uint8_t index);

        /** @brief Refresh only the metrics portion of the status bar (skips if
         *  unchanged). @p restore_cursor returns the cursor to where it started, for
         *  standalone periodic refreshes; draw_status_row passes false since it is
         *  mid-rebuild and its caller repositions absolutely.
         *  @return true if the pass fully synced the on-screen metrics (or there was
         *  nothing to draw); false if it stopped early because a run would not fit -
         *  the caller then leaves the refresh timestamp unadvanced and retries. */
        bool update_status_metrics(bool restore_cursor = true);

        /** @brief Refresh the centred HH:MM:SS clock (hot field, normal video on the
         *  reverse-video bar; diffed so only changed digits emit). Skipped on a very
         *  narrow terminal. @p restore_cursor as for update_status_metrics().
         *  @return true if it emitted any bytes (a digit changed), so the caller can
         *  charge it against the status-bar field budget. */
        bool update_status_clock(bool restore_cursor = true);

        /** @} */

        /** @name Alarm bar
         *  @{ */

        /** @brief Redraw the always-visible bottom alarm bar: a reverse-video bar with
         *  a " ALARMS : " header, then the unmasked alarm codes drawn normal-video
         *  (each followed by an inverted space, so they read as a set against the bar),
         *  filled to the row end. No-op without an alarm source. Repaints
         *  unconditionally; the cooperative gate and cursor save/restore live in
         *  step()'s alarm-bar pass. */
        void draw_alarm_bar();

        /** @} */

        /** @name Serial / TX
         *  @{ */

        /** @brief Number of bytes that can still be written to the serial TX buffer */
        uint16_t tx_available() const;

        /** @brief True when the serial TX buffer has been fully flushed to the wire */
        bool tx_empty() const;

        /** @brief Fit check: true if @p need bytes can still be written right now.
         *  For all-or-nothing reserves (the status bar, any reserve-then-emit). */
        bool tx_ready(uint16_t need) const { return tx_available() >= need; }

        /** @brief Depth gate: true while the TX ring holds at most @p depth bytes.
         *  Cooperative bulk writers only make progress while the ring is this shallow,
         *  so interactive output and the status bar stay responsive (echo latency is
         *  ~ occupancy / wire_rate). Read live at each check, never snapshotted: the
         *  gate is feedback control on the real ring depth, and a concurrent drain
         *  only ever frees space, so a live read is at worst conservative. */
        bool tx_below(uint16_t depth) const { return tx_used() <= depth; }

        /** @brief The shared paint depth floor: ring shallow enough to preserve the
         *  latency target (Config::TX_COLD_TARGET_MS) at the link baud. Every cooperative
         *  paint path gates on this one floor (status bar, page hot, page cold,
         *  draw_static, focus replay); fairness between them comes from the per-class
         *  field budgets and the paint order, not from separate depth gates. */
        bool tx_below_cold() const { return tx_used() <= _tx_cold_depth; }

        /** @brief RX ring depth captured at the start of this step(), before the input
         *  parser drained it. The live depth would read ~0 by the time a page paints
         *  its serial gauge (the parse loop runs first), so this snapshot shows the
         *  bytes that were actually waiting this cycle. */
        uint16_t rx_used() const { return _rx_cycle_used; }

        /** @brief Bytes currently queued in the serial TX ring buffer */
        uint16_t tx_used() const {
            uint16_t cap = Utils::BufferedSerial::tx_capacity();
            uint16_t freed = _serial.availableForWrite();
            return (freed <= cap) ? (uint16_t)(cap - freed) : 0;
        }

        /** @brief Cumulative bytes dropped because the TX ring was full (console
         *  output that didn't retry); surfaced by the status page. */
        uint32_t tx_dropped() const { return _serial.tx_dropped(); }

        /** @brief Capacity of the serial RX / TX ring buffers, bytes */
        static constexpr uint16_t rx_capacity() { return Utils::BufferedSerial::rx_capacity(); }
        static constexpr uint16_t tx_capacity() { return Utils::BufferedSerial::tx_capacity(); }

        /** @} */

        /** @name Context access
         *  @{ */

        /** @brief Access the attached subsystem status contexts (members may be null) */
        const SystemContext& context() const { return _ctx; }

        /** @brief The attached alarm model, or null when none was supplied (drives
         *  the bottom alarm bar; also read by the shell's "alarms" command). */
        const Alarms::AlarmModel* alarms() const { return _alarms; }

        /** @} */

    private:
        /** @name Internals
         *  @{ */

        /** @brief Clear+home the screen and reset cursor/wrap tracking, re-asserting
         *  DECAWM off (?7l) on every call. Does not discard the TX buffer (the
         *  caller does). Shared by clear_screen() and setup_*(). */
        void _establish_screen();

        /** @brief Last screen row a page may scroll/draw content into (0-indexed).
         *  Row 0 is the status bar; the rows below it down to this one are the page
         *  content area that scroll_region() and line_feed() confine scrolling to.
         *  The reserved bottom row (the alarm bar, when present) is excluded. */
        uint16_t _content_bottom() const {
            uint16_t below = (uint16_t)(_reserved_bottom + 1);
            return (uint16_t)(_rows > below ? _rows - below : 0);
        }

        /** @brief Render the right-side status field into @p buf; returns its length.
         *  The width is fixed, so a byte index maps to a fixed column (relied on by
         *  update_status_metrics()'s per-cell diff). */
        int _build_metrics(char* buf, size_t cap) const;

        /** @brief Bitset of currently-unmasked alarms (the bar's listed set), used as
         *  the change signature gating a repaint. Zero when there is no alarm source. */
        uint64_t _unmasked_set() const;

        /** @brief Cooperative alarm-bar pass (called once per step): repaint when the
         *  unmasked set changed or the row was wiped, but only while the TX ring is
         *  shallow; saves and restores the page cursor and its visibility so the bar
         *  never steals a visible cursor (e.g. the shell editor's) to the bottom row. */
        void _run_alarm_bar();

        /** @} */
    };

}}

#endif // TERMINAL_TERMINAL_H
