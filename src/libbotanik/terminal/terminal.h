#ifndef TERMINAL_TERMINAL_H
#define TERMINAL_TERMINAL_H

/**
 * @file terminal.h
 * @brief AnsiTerminal — owns the serial connection and drives the multiplexed
 *        page display, routing input events to the active page and managing the
 *        cursor, geometry, and status bar.
 */

#include <cstdint>

#include "libbotanik/utils/serial.h"
#include "libbotanik/terminal/ansi.h"
#include "libbotanik/context.h"

#include "libbotanik/pages/page.h"

namespace Botanik { namespace Terminal {


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
        /** @brief SM4/RM4 insert mode — toggled lazily by write_chars. */
        bool                _hw_insert_mode = false;
        /** @brief DECTCEM cursor visibility (default: hidden). Pages that want a
         *  visible cursor (only the shell editor today) opt in via
         *  set_cursor_visible() in focus(); switch_to() hides it for everyone else. */
        bool                _hw_cursor_visible = false;
        /** @brief Screen has been established (cleared) at least once — set by
         *  setup_*() / clear_screen(). Lets the first switch_to() skip a redundant
         *  clear that would tx_discard a setup_*()'s freshly-emitted handshake. */
        bool                _screen_ready = false;

        /** @} */

        /** @name Status bar state
         *  @{ */

        /** @brief Last status-metrics refresh (wall clock). */
        uint64_t            _status_last_ms = 0;
        /** @brief Visible width (+NUL) of the right-aligned status field
         *  "PLC:xxx cpu:nnnn% stk:nnn% ". Both the build buffer and the diff cache
         *  are sized from this one constant so they can never drift apart. */
        static constexpr uint16_t    STATUS_METRICS_LEN = 28;
        /** @brief Diff cache of the last drawn metrics (zero-init forces first draw). */
        char                         _status_metrics_cache[STATUS_METRICS_LEN] = {};
        /** @brief Free TX bytes required before a metrics refresh runs. A typical pass
         *  rewrites only the few changed digits (well under this); write() truncates
         *  silently when full, so the refresh is deferred unless this much room is
         *  free — a half-emitted escape sequence would corrupt the status bar. */
        static constexpr uint16_t    STATUS_METRICS_TX_BUDGET = 64;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Initialize the terminal with given dimensions */
        AnsiTerminal(Utils::BufferedSerial& serial, uint16_t cols, uint16_t rows,
                     uint32_t* timestamp_ms, const SystemContext& ctx = {});

        /** @brief Process input and update terminal state */
        void step();

        /** @brief Select and initialize the target terminal type. Call once, before
         *  the first switch_to()/render. Emits only the type's one-time handshake; the
         *  screen clear is deferred to the first step() so it lands *after* this
         *  handshake (a Minitel needs its PRO2 bytes while still in its native command
         *  set), in the order clear -> "ACAB" marker -> contents.
         *  - setup_minitel(): 80-col téléinformatique mode, lowercase keyboard, and
         *    page mode over the DIN link (PRO2, sent before the ANSI clear while the
         *    unit is still in its native command set). DECAWM ?7l is also sent (on the
         *    first-step establish) as a harmless hedge in case the unit honors it.
         *  - setup_vt102():   plain VT100/VT102; DECAWM (?7l) (re)asserted on every
         *    screen establish. The default if neither is called. */
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

        /** @brief Get the total number of rows (height) */
        inline uint16_t rows() const { return _rows; }

        /** @} */

        /** @name Text output & character attributes
         *  @{ */

        /** @brief Write characters at the current cursor position
         *  @param buf Buffer to write
         *  @param len Number of characters to write
         *  @param insert If true, use insertion mode (shifts existing text right)
         */
        void write_chars(const char *buf, uint16_t len, bool insert = false);

        /** @brief Set or clear the reverse-video (inverted) attribute */
        void set_inverted(bool enable);

        /** @brief Enable or disable terminal auto-wrap (DECAWM, CSI ? 7 h/l) */
        void set_auto_wrap(bool enable);

        /** @brief Show or hide the (blinking) cursor (DECTCEM). Lazy: emits only on
         *  a state change. There is no steady cursor, so callers that want to stop
         *  the blink simply hide it. */
        void set_cursor_visible(bool show);

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

        /** @} */

        /** @name Pages
         *  @{ */

        /** @brief Register a page; returns its index, or -1 if the slot array is full */
        int8_t add_page(Pages::Page* page);

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

        /** @brief Refresh only the metrics portion of the status bar (skips if unchanged) */
        void update_status_metrics();

        /** @} */

        /** @name Serial / TX
         *  @{ */

        /** @brief Number of bytes that can still be written to the serial TX buffer */
        uint16_t tx_available() const;

        /** @brief True when the serial TX buffer has been fully flushed to the wire */
        bool tx_empty() const;

        /** @brief Bytes currently queued in the serial RX ring buffer */
        uint16_t rx_used() const { return _serial.available(); }

        /** @brief Bytes currently queued in the serial TX ring buffer */
        uint16_t tx_used() const {
            uint16_t cap = Utils::BufferedSerial::tx_capacity();
            uint16_t freed = _serial.availableForWrite();
            return (freed <= cap) ? (uint16_t)(cap - freed) : 0;
        }

        /** @brief Capacity of the serial RX / TX ring buffers, bytes */
        static constexpr uint16_t rx_capacity() { return Utils::BufferedSerial::rx_capacity(); }
        static constexpr uint16_t tx_capacity() { return Utils::BufferedSerial::tx_capacity(); }

        /** @} */

        /** @name Context access
         *  @{ */

        /** @brief Access the attached subsystem status contexts (members may be null) */
        const SystemContext& context() const { return _ctx; }

        /** @} */

    private:
        /** @name Internals
         *  @{ */

        /** @brief Clear+home the screen and reset cursor/wrap tracking, re-asserting
         *  DECAWM off (?7l) on every call. Does not discard the TX buffer (the
         *  caller does). Shared by clear_screen() and setup_*(). */
        void _establish_screen();

        /** @brief Render the right-side status field into @p buf; returns its length
         *  (clamped to >= 0). The width is fixed, so a byte index maps to a fixed
         *  screen column — relied on by update_status_metrics()'s per-cell diff. */
        int _build_metrics(char* buf, size_t cap) const;

        /** @} */
    };

}}

#endif // TERMINAL_TERMINAL_H
