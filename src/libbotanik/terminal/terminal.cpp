/**
 * @file terminal.cpp
 * @brief AnsiTerminal implementation - cursor/geometry tracking, text output,
 *        content manipulation, page routing, and status-bar rendering.
 */
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>

#include "libbotanik_config.h"

#include "libbotanik/hal/platform.h"
#include "libbotanik/utils/serial.h"
#include "libbotanik/terminal/parser.h"
#include "libbotanik/terminal/terminal.h"
#include "libbotanik/terminal/ansi.h"
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/plcio/sync.h"
#include "libbotanik/utils/time.h"


namespace Botanik { namespace Terminal {

    namespace {
        using namespace Botanik::Utils;
        using namespace Botanik::Pages;
    }

    // Lifecycle
    // ------------------------------------------------------------------------

    // Constructor
    AnsiTerminal::AnsiTerminal(Utils::BufferedSerial& serial, uint16_t cols, uint16_t rows,
                               const uint32_t* timestamp_ms, uint32_t baud, const SystemContext& ctx) :
        _parser(serial, timestamp_ms),
        _serial(serial),
        _ctx(ctx),
        _cols(cols),
        _rows(rows)
    {
        // Size the shared cooperative-TX depth floor from the link baud so the felt echo
        // latency target (Config::TX_COLD_TARGET_MS) holds across links.
        _tx_cold_depth = depth_for_baud(baud, Config::TX_COLD_TARGET_MS);
    }

    // Step through the terminal handling code
    void AnsiTerminal::step() {
        using Command = InputEvent::Command;
        // Snapshot the RX depth before the parse loop below drains it, so the serial
        // gauge reports what was actually waiting this cycle rather than ~0 post-drain.
        _rx_cycle_used = _serial.available();
        // Establish the screen on the first step (not in setup_*()) so the clear
        // lands after the handshake setup_*() queued, in order: clear, "ACAB" boot
        // marker, contents. _establish_screen() (not clear_screen) so the queued
        // handshake survives the tx_discard clear_screen would do.
        if (!_screen_ready) {
            _establish_screen();
            draw_status_row();
            if (_pages_len > 0 && _pages[_page_index] != nullptr)
                _pages[_page_index]->focus();
        }
        uint64_t now = HAL::uptime_ms();
        // Status bar paints first each tick (it is self-contained - no page labels to
        // wait on), so it interleaves with whatever the active page is drawing, including
        // its static phase. Its two fields (the centred HH:MM:SS clock and the right-
        // aligned metrics block) share TX_STATUS_FIELDS_PER_TICK and the shared depth
        // floor; the round-robin cursor rotates which one repaints first so a tight budget
        // never always favours the clock. A field repaints only on its own cadence, and an
        // unchanged field emits nothing and spends no budget.
        uint16_t status_budget = Config::TX_STATUS_FIELDS_PER_TICK;
        for (uint8_t v = 0; v < 2 && status_budget > 0 && tx_below_cold(); v++) {
            if (_status_field == 0) {
                if (now - _status_clock_last_ms >= Config::PAGE_HOT_MS) {
                    _status_clock_last_ms = now;
                    if (update_status_clock()) status_budget--;
                }
            } else {
                if (now - _status_last_ms >= Config::STATUS_REFRESH_MS) {
                    // Advance the refresh clock only once the pass fully synced the
                    // on-screen metrics; a partial run leaves it for the next tick instead
                    // of waiting out another full STATUS_REFRESH_MS.
                    if (update_status_metrics()) _status_last_ms = now;
                    status_budget--;
                }
            }
            _status_field ^= 1;
        }
        // Handle up to INPUT_EVENTS_PER_TICK events this tick so a paste or fast
        // typing stays responsive without letting a flood monopolise the loop. Light
        // events (text echo, cursor/edit commands) keep iterating; a heavy terminal
        // intercept (full clear/redraw or a page switch) does its one repaint and
        // yields the rest of the tick so two full repaints can never stack.
        for (uint16_t n = 0; n < Config::INPUT_EVENTS_PER_TICK; n++) {
            InputEvent& ev = _parser.parse();
            if (ev.type == InputEvent::EventType::NONE) break;
            if (_pages_len == 0 || _pages[_page_index] == nullptr) continue;
            Pages::Page* page = _pages[_page_index];
            if (ev.type == InputEvent::EventType::CONTENT) {
                page->handle_text_batch(ev.batch_data, ev.batch_len);
                continue;
            }
            if (ev.type != InputEvent::EventType::COMMAND) continue;
            // Terminal-level intercepts - consumed before reaching the page. These
            // repaint the whole screen, so each breaks (takes the rest of the tick).
            if (ev.command == Command::CTRL_L) {
                clear_screen();
                draw_status_row();
                page->redraw();
                break;
            }
            if (ev.command == Command::HOME)   { switch_to(0);  break; }
            if (ev.command >= Command::ALT_1 && ev.command <= Command::ALT_9) {
                switch_to((uint8_t)((uint16_t)ev.command - (uint16_t)Command::ALT_1));
                break;
            }
            if (ev.command == Command::ALT_0) { switch_to(9); break; }
            if (ev.command == Command::CTRL_PAGE_UP ||
                ev.command == Command::CTRL_PAGE_DOWN ||
                ev.command == Command::TAB ||
                ev.command == Command::SHIFT_TAB) {
                // TAB and Ctrl+PageDown advance to the next page; Shift+TAB
                // and Ctrl+PageUp step back. Both directions wrap around the
                // ends.
                const bool back = ev.command == Command::CTRL_PAGE_UP ||
                                  ev.command == Command::SHIFT_TAB;
                switch_relative(!back);
                break;
            }
            // Ctrl+C: let the page consume it (e.g. cancel an input line); if it
            // doesn't, apply the default - quit on the simulator (Ctrl+C arrives as a
            // byte in raw mode), a no-op on hardware so a stray Ctrl+C can't reboot
            // the controller.
            if (ev.command == Command::CTRL_C) {
                if (!page->handle_cancel()) {
#ifdef TARGET_SIMULATOR
                    HAL::shutdown();
#endif
                }
                break;
            }
            // Light page command (edit/cursor/etc.): keep handling more this tick.
            page->handle_command(ev.command, ev.count);
        }
        // Tick every page, not just the active one: each is its own LogWriter and
        // must drain into its scrollback every step so a later focus() replays an
        // up-to-date tail. Inactive pages don't render (gated on their active flag).
        for (uint8_t i = 0; i < _pages_len; i++)
            if (_pages[i] != nullptr) _pages[i]->tick(i == _page_index);
    }

    // Cursor & geometry
    // ------------------------------------------------------------------------

    // Move cursor to an absolute screen position
    void AnsiTerminal::move_to(const CursorPos& target_in, bool skip) {
        // The hardware caps the cursor at the last row/column, so clamp any
        // out-of-range target here to keep _hw_pos honest.
        CursorPos target = {
            (uint16_t)std::min((uint16_t)(_rows > 0 ? _rows - 1 : 0), target_in.row),
            (uint16_t)std::min((uint16_t)(_cols > 0 ? _cols - 1 : 0), target_in.col),
        };
        if (target == _hw_pos) {
            return;
        }

        // Emit the move all-or-nothing: on a TX-full failure leave _hw_pos untouched
        // so tracking stays truthful (a wrong _hw_pos smears every later write).
        // skip=true updates tracking without emitting (hardware already moved).
        bool ok = true;
        if (!skip) {
            // A row-filling write parks the cursor at the assumed last column, but a
            // real terminal wider than _cols leaves it one (or more) columns further
            // right - so the parked column is unknown and any move computed off it
            // would land short. Reposition absolutely: the one width-independent move.
            if (_hw_at_eol) {
                ok = Ansi::goto_absolute(_serial, target);
            }
            // Prefer a relative move when the cursor shares the target's row or
            // column; goto_absolute is for true diagonals only. A single-axis hop is
            // never longer than an absolute goto, so there's no delta where absolute
            // wins. target == _hw_pos returned above, so the differing axis is non-zero.
            else if (target.row == _hw_pos.row) { // Same row
                if (target.col == 0) {
                    ok = Ansi::move_row_start(_serial);   // 1-byte CR beats CUB
                } else if (target.col < _hw_pos.col) {
                    ok = Ansi::move_left(_serial, _hw_pos.col - target.col);
                } else {
                    ok = Ansi::move_right(_serial, target.col - _hw_pos.col);
                }
            } else if (target.col == _hw_pos.col) { // Same column
                if (target.row < _hw_pos.row) {
                    ok = Ansi::move_up(_serial, _hw_pos.row - target.row);
                } else {
                    ok = Ansi::move_down(_serial, target.row - _hw_pos.row);
                }
            } else if (target.col == 0 && target.row > _hw_pos.row
                       && (target.row - _hw_pos.row) < 4
                       && target.row < (uint16_t)(_rows - 1)) {
                // Common "spawn next row" target - CR + LF(s) is cheaper than
                // goto_absolute. Bounded off the last row so a stray LF can't scroll.
                ok = Ansi::down_to_row_start(_serial, (uint16_t)(target.row - _hw_pos.row));
            } else { // Different row and column, use absolute move
                ok = Ansi::goto_absolute(_serial, target);
            }
        }
        if (ok) {
            _hw_pos = target;
            _hw_at_eol = false;
        }
    }

    // Text output & character attributes
    // ------------------------------------------------------------------------

    // Write `len` chars at cursor, advancing it right.
    uint16_t AnsiTerminal::write_chars(const char *s, uint16_t len, bool insert) {
        if (len == 0) return 0;

        // Lazy SM4/RM4 toggling: keep insert mode latched so consecutive inserts
        // cost just the char byte, instead of re-bracketing each with SM4/RM4.
        if (insert != _hw_insert_mode) {
            // Commit the latch only if the toggle reached the wire. Latching past a
            // dropped RM4 would let a later replace-mode write paint in insert mode
            // and corrupt the line; bail and let the caller redraw.
            if (!Ansi::set_insert_mode(_serial, insert)) return 0;
            _hw_insert_mode = insert;
        }

        // Track only what reached the wire: write() drops the tail when TX fills, so
        // advancing _hw_pos by the full len would desync tracking. Dropped glyphs are
        // content loss a redraw fixes; a wrong cursor isn't.
        uint16_t written = _serial.write(s, len);
        uint32_t new_col = (uint32_t)_hw_pos.col + written;
        if (new_col >= _cols) {
            if (_hw_auto_wrap) {
                _hw_pos.col = 0;
                _hw_pos.row++;
                if (_hw_pos.row >= _rows) _hw_pos.row = _rows - 1;
                _hw_at_eol = false;
            } else {
                _hw_pos.col = _cols - 1;
                // Parked at the last cell, which holds a char we just wrote; flag it
                // so a follow-up clear_to_eol doesn't erase it.
                _hw_at_eol = true;
            }
        } else {
            _hw_pos.col = (uint16_t)new_col;
            _hw_at_eol = false;
        }
        return written;
    }

    // Set/clear reverse-video character attribute
    void AnsiTerminal::set_inverted(bool enable) {
        if (enable == _hw_inverted) return;
        // Reserve the longest form (CSI + ATTR_NEG) up front so a split SGR can't
        // swallow following payload bytes.
        constexpr uint16_t SGR_MAX = (sizeof(Ansi::CSI) - 1) + (sizeof(Ansi::ATTR_NEG) - 1);
        if (_serial.availableForWrite() < SGR_MAX) return;

        _serial.write(Ansi::CSI, sizeof(Ansi::CSI) - 1);
        if (enable) _serial.write(Ansi::ATTR_NEG,  sizeof(Ansi::ATTR_NEG) - 1);
        else        _serial.write(Ansi::ATTR_NONE, sizeof(Ansi::ATTR_NONE) - 1);
        _hw_inverted = enable;
    }

    // Enable or disable terminal auto-wrap (DECAWM)
    void AnsiTerminal::set_auto_wrap(bool enable) {
        Ansi::set_auto_wrap(_serial, enable);
        _hw_auto_wrap = enable;
    }

    // Show or hide the (blinking) cursor (DECTCEM)
    void AnsiTerminal::set_cursor_visible(bool show) {
        if (show == _hw_cursor_visible) return;
        // Only commit the tracked state if the sequence actually reached the wire,
        // so a TX-full skip is retried on the next call rather than silently lost.
        if (!Ansi::set_cursor_visible(_serial, show)) return;
        _hw_cursor_visible = show;
    }

    // Content manipulation
    // ------------------------------------------------------------------------

    // Delete `len` chars at cursor, shifting the rest of the line left
    void AnsiTerminal::delete_chars(uint16_t len) {
        if (len == 0) return;
        Ansi::delete_chars(_serial, len);
        // DCH shifts content left; cursor position is unaffected.
    }

    // Erase content from cursor to the end of the line
    void AnsiTerminal::clear_to_eol() {
        // Cursor parked at the last cell after a row-filling write: nothing past it
        // to erase, and the parked cell holds valid content.
        if (_hw_at_eol) return;
        Ansi::clear_to_eol(_serial);  // EL leaves the cursor in place
    }

    // Erase from the cursor to the end of the display; the cursor does not move
    void AnsiTerminal::clear_to_eos() {
        Ansi::clear_to_eos(_serial);
        _hw_at_eol = false;
    }

    void AnsiTerminal::clear_screen() {
        // Drop bytes still queued for TX - they'd paint onto a display we're wiping.
        // Emptying the ring also guarantees the clear sequence fits, so the _hw_pos
        // reset below is correct with no dropped-write path.
        _serial.tx_discard();
        // Resync the terminal: the discard may have torn a half-sent sequence on the
        // wire, which would otherwise swallow the leading bytes of the clear below.
        Ansi::cancel_sequence(_serial);
        _establish_screen();
    }

    void AnsiTerminal::_establish_screen() {
        // Clear+home, then reset tracked cursor/wrap state. No TX discard here - the
        // caller owns that (clear_screen drops stale paint; the first-step bootstrap
        // keeps the setup_*() handshake it queued).
        Ansi::clear_screen(_serial);
        _hw_pos = {0, 0};
        _hw_at_eol = false;
        // Boot marker: emit "ACAB" once per process (gated on the first establish, so
        // Ctrl-L redraws don't repeat it) to mark a fresh start / re-exec in the
        // stream. Track the four columns it advances. (cols >= 5, so it can't wrap.)
        if (!_screen_ready) {
            _serial.write("ACAB", 4);
            _hw_pos.col = 4;
        }
        // Auto-wrap stays off UI-wide (every page pre-wraps its own content); a stray
        // wrap parks the cursor and the next clear_to_eol eats a glyph. Emit
        // unconditionally (a prior copy may have been discarded). Harmless on a
        // Minitel that ignores it; free right-margin protection on one that honors it.
        set_auto_wrap(false);
        // Hide the cursor by default (only the shell editor opts back in via focus()).
        // A fresh clear leaves it shown, so force the emit by flipping the shadow past
        // the lazy guard.
        _hw_cursor_visible = true;
        set_cursor_visible(false);
        // Reset reverse-video and insert mode: ED clears neither, so the unit may
        // still hold them while our shadows read "normal". Force both emits to resync.
        _hw_inverted = true;
        set_inverted(false);                    // -> SGR normal (ESC[m)
        Ansi::set_insert_mode(_serial, false);  // -> replace mode (ESC[4l)
        _hw_insert_mode = false;
        _screen_ready = true;
    }

    void AnsiTerminal::setup_vt102() {
        // Plain VT100/VT102 (the default): no special handshake. DECAWM and the clear
        // are handled later (_establish_screen / first step()).
        Ansi::vt102_setup(_serial);
    }

    void AnsiTerminal::setup_minitel() {
        // Drive a real Minitel (1B+/2) over the DIN link. The PRO2 handshake (sent
        // before the ANSI clear, while the unit is still native) selects 80-col
        // teleinformatique mode, a lowercase keyboard, and page mode.
        Ansi::minitel_setup(_serial);
    }

    void AnsiTerminal::clear_content() {
        // Erase the content area (rows 1..N-1), leaving the row-0 status bar. One
        // erase-to-end-of-display from {1,0} beats clearing each row in a loop.
        move_to({1, 0});
        clear_to_eos();
    }

    // Emit a line feed
    void AnsiTerminal::line_feed(bool carriage_return) {
        if (_hw_pos.row < _rows - 1) {
            // Normal case: cursor moves into the next row
            if (carriage_return) {
                _serial.write("\r\n", 2);
                _hw_pos.col = 0;
            } else {
                _serial.write("\n", 1);
            }
            _hw_pos.row++;
        } else {
            // At the last row: DL 1 at row 1 scrolls only the content area
            // (rows 1..N-1) so the row-0 status bar is never touched.
            uint16_t target_col = carriage_return ? 0 : _hw_pos.col;
            move_to({1, 0});
            Ansi::delete_rows(_serial, 1);
            // Cursor stays at {1, 0} after DL; move to the new blank last row.
            _hw_pos = {1, 0};
            move_to({(uint16_t)(_rows - 1), target_col});
        }
    }

    // Insert blank rows
    void AnsiTerminal::insert_rows(uint16_t count) {
        if (count == 0) return;
        Ansi::insert_rows(_serial, count);  // IL shifts content; cursor unaffected
    }

    // Delete rows
    void AnsiTerminal::delete_rows(uint16_t count) {
        if (count == 0) return;
        Ansi::delete_rows(_serial, count);  // DL shifts content; cursor unaffected
    }

    // Pages
    // ------------------------------------------------------------------------

    int8_t AnsiTerminal::add_page(Pages::Page* page) {
        if (page == nullptr || _pages_len >= 8) return -1;
        _pages[_pages_len] = page;
        return (int8_t)(_pages_len++);
    }

    void AnsiTerminal::switch_relative(bool forward) {
        if (_pages_len == 0) return;
        uint8_t next = forward
            ? (uint8_t)((_page_index + 1) % _pages_len)
            : (uint8_t)((_page_index + _pages_len - 1) % _pages_len);
        switch_to(next);
    }

    void AnsiTerminal::switch_to(uint8_t index) {
        if (index >= _pages_len || _pages[index] == nullptr) return;
        if (!_screen_ready) {
            // Establish is deferred to the first step(); drawing here would be wiped
            // by that clear, so just record the target page for the bootstrap.
            _page_index = index;
            return;
        }
        if (_pages[_page_index] != nullptr)
            _pages[_page_index]->deactivate();
        uint8_t prev = _page_index;
        _page_index = index;
        // Page change: keep row 0, clear only the content area. Drop bytes queued for
        // the page we're leaving first (they'd paint onto the new page). _hw_pos is
        // stale after the drop, so reposition absolutely (not relative move_to).
        _serial.tx_discard();
        // The discard can strand a half-sent sequence on the wire (transmit() stops
        // mid-sequence under the baud throttle); cancel_sequence() resyncs the terminal
        // to ground state so the tab repaint below isn't swallowed by a stuck parser -
        // that swallow is what leaves some page-name chars un-inverted under TX load.
        Ansi::cancel_sequence(_serial);
        // Hide the cursor before any repaint so it doesn't blink across the cleared
        // content or the row-0 tabs. The page that wants it (shell editor) shows it
        // again from focus(). Lazy, so a no-op when already off.
        set_cursor_visible(false);
        Ansi::goto_absolute(_serial, {1, 0});
        _hw_pos = {1, 0};
        _hw_at_eol = false;
        clear_to_eos();
        // Only the tabs' video flips on a switch, so repaint just the two affected
        // tabs (the one we left, the one we entered) rather than the whole bar.
        draw_status_tab(prev);
        if (prev != index) draw_status_tab(index);
        // The tx_discard above may have dropped a queued metrics or clock update;
        // invalidate both diff caches so the next refresh repaints them in full (no
        // bytes go out here - the on-screen row 0 survived the content clear).
        memset(_status_metrics_cache, 0, sizeof(_status_metrics_cache));
        memset(_status_clock_cache,   0, sizeof(_status_clock_cache));
        _pages[_page_index]->focus();
    }

    // Status bar
    // ------------------------------------------------------------------------

    void AnsiTerminal::draw_status_row() {
        // The metrics field is fixed-width; we only need its width to know where it
        // starts (update_status_metrics() paints its bytes below).
        constexpr uint16_t mlen = STATUS_METRICS_LEN - 1;  // drop the trailing NUL

        // Save the attribute only, not the cursor: every caller repositions
        // absolutely right after, so a cursor restore here would be wasted.
        bool      saved_inv = _hw_inverted;

        move_to({0, 0});

        // Force the reverse-video background to emit before the tabs: a prior
        // tx_discard can leave _hw_inverted stale, which would make the first tab's
        // set_inverted() a wrong no-op. Clear the tracked state to force it.
        _hw_inverted = false;
        set_inverted(true);

        // Three blank columns, then one " NAME " tab per page. The bar is reverse
        // video; the active tab is drawn normal so it stands out.
        write_chars("   ", 3);
        for (uint8_t i = 0; i < _pages_len; i++) {
            if (_pages[i] == nullptr) continue;
            const char* nm = _pages[i]->name();
            set_inverted(i != _page_index);
            write_chars(" ", 1);
            write_chars(nm, (uint16_t)strlen(nm));
            write_chars(" ", 1);
        }
        set_inverted(true);

        // Middle: spaces up to metrics start column
        uint16_t metrics_col = (_cols > (uint16_t)(mlen)) ? (uint16_t)(_cols - mlen) : 0;
        // 31 spaces + NUL. The NUL must never reach the wire: terminals drop it
        // without moving, but write_chars would still bump _hw_pos by its length.
        static const char spaces[32] = "                               ";
        constexpr uint16_t SPACES_LEN = sizeof(spaces) - 1;
        while (_hw_pos.col < metrics_col) {
            uint16_t chunk = std::min((uint16_t)(metrics_col - _hw_pos.col), SPACES_LEN);
            write_chars(spaces, chunk);
        }

        // Right: invalidate the diff cache, then let update_status_metrics() paint -
        // it's the single place that knows the per-field video and truncates narrow.
        memset(_status_metrics_cache, 0, sizeof(_status_metrics_cache));
        update_status_metrics(/*restore_cursor=*/false);

        // Centred HH:MM:SS clock: invalidate its cache and paint the initial value so
        // it is not blank until the first per-second hot tick.
        memset(_status_clock_cache, 0, sizeof(_status_clock_cache));
        update_status_clock(/*restore_cursor=*/false);

        // Restore only the attribute; the cursor is left where the metrics parked it
        // (the caller repositions absolutely).
        set_inverted(saved_inv);
    }

    void AnsiTerminal::draw_status_tab(uint8_t index) {
        if (index >= _pages_len || _pages[index] == nullptr) return;

        // Tab columns mirror draw_status_row(): three leading blanks, then one
        // " NAME " (name length + 2) per page in order.
        uint16_t col = 3;
        for (uint8_t i = 0; i < index; i++) {
            if (_pages[i] == nullptr) continue;
            col += (uint16_t)(strlen(_pages[i]->name()) + 2);
        }
        const char* nm = _pages[index]->name();

        // Save the attribute only, not the cursor: switch_to() draws both flipped
        // tabs then lets the incoming focus() reposition, so a restore here is moot.
        bool      saved_inv = _hw_inverted;

        move_to({0, col});
        // Active tab normal video, others reverse. Force the SGR to emit: a preceding
        // tx_discard can leave _hw_inverted stale and make set_inverted() a wrong
        // no-op (same hazard draw_status_row() guards).
        bool inv = (index != _page_index);
        _hw_inverted = !inv;
        set_inverted(inv);
        write_chars(" ", 1);
        write_chars(nm, (uint16_t)strlen(nm));
        write_chars(" ", 1);

        // Restore only the attribute; the cursor is left for the caller (see above).
        set_inverted(saved_inv);
    }

    uint16_t AnsiTerminal::emit_field_run(uint16_t row, uint16_t col, const char* cf,
                                          uint16_t a, uint16_t b, uint32_t hi) {
        move_to({row, (uint16_t)(col + a)});
        uint16_t seg = a;
        bool seg_inv = ((hi >> a) & 1u) != 0;
        uint16_t written = 0;
        for (uint16_t k = a; k < b; ++k) {
            bool ki = ((hi >> k) & 1u) != 0;
            if (ki != seg_inv) {
                set_inverted(seg_inv);
                written += write_chars(&cf[seg], (uint16_t)(k - seg));
                seg = k; seg_inv = ki;
            }
        }
        set_inverted(seg_inv);
        written += write_chars(&cf[seg], (uint16_t)(b - seg));
        return written;   // contiguous leading cells that reached the wire
    }

    bool AnsiTerminal::update_status_metrics(bool restore_cursor) {
        char metrics[STATUS_METRICS_LEN];
        int mlen = _build_metrics(metrics, sizeof(metrics));

        // mlen is constant (fixed-width fields), so a byte index maps to a fixed col.
        uint16_t visible = (uint16_t)std::min((int)_cols, mlen);
        if (visible == 0) return true;  // nothing to draw == nothing left to sync
        uint16_t metrics_col = (uint16_t)(_cols - visible);

        CursorPos saved_pos = _hw_pos;
        bool      saved_inv = _hw_inverted;
        bool      saved_cur = _hw_cursor_visible;
        bool      touched   = false;
        bool      complete  = true;

        // The bar is reverse video except the live readings - plc (cols 4..6), cpu
        // (12..15), stk (21..24) - drawn normal. Coalesce changed cells into runs,
        // breaking at that boundary so each run is uniformly one video or the other.
        int i = 0;
        while (i < visible) {
            if (metrics[i] == _status_metrics_cache[i]) { ++i; continue; }
            bool value = (i >= 4 && i <= 6) || (i >= 12 && i <= 15) || (i >= 21 && i <= 24);
            int j = i + 1;
            while (j < visible && metrics[j] != _status_metrics_cache[j] &&
                   ((j >= 4 && j <= 6) || (j >= 12 && j <= 15) || (j >= 21 && j <= 24)) == value) ++j;
            // Only emit a run if it fully fits (move + invert + chars); otherwise
            // stop and retry next refresh, leaving the unwritten cells' cache stale.
            // Reserve the true worst case: cursor move (~9) + up to two SGR flips
            // (~4 each) + the run. The return-written check below is the real safety
            // net, but a tight reserve avoids starting a doomed emit.
            uint16_t run = (uint16_t)(j - i);
            if (tx_available() < (uint16_t)(run + 18)) { complete = false; break; }
            // Hide the cursor before the first status-bar write so it doesn't blink
            // across row 0 while the metrics paint; restored below (no-op if already
            // hidden, e.g. on a non-shell page).
            if (!touched) set_cursor_visible(false);
            touched = true;
            // Whole run is uniformly value (normal) or not (inverted): pass an all-set
            // or all-clear highlight mask so emit_field_run draws it in one segment.
            uint16_t wrote = emit_field_run(0, metrics_col, metrics, (uint16_t)i, (uint16_t)j,
                                            value ? 0u : ~0u);
            // Cache only the cells that reached the wire; if the run truncated, stop
            // and leave the rest stale so the next refresh repaints them.
            memcpy(_status_metrics_cache + i, metrics + i, (size_t)wrote);
            if (wrote < run) { complete = false; break; }
            i = j;
        }

        if (touched) {
            set_inverted(saved_inv);
            // Periodic refreshes restore the cursor to where the page left it.
            // draw_status_row passes restore_cursor=false (its caller repositions
            // absolutely right after, so a restore would be a wasted move).
            if (restore_cursor) move_to(saved_pos);
            // Re-show the cursor only after it's back at the page position, so it
            // never reappears on row 0. No-op when it was already hidden.
            set_cursor_visible(saved_cur);
        }
        return complete;
    }

    bool AnsiTerminal::update_status_clock(bool restore_cursor) {
        // Wall-clock HH:MM:SS (same source as the STATUS page's "Wall Clock" field).
        Utils::CalendarTime wc = Utils::cal_time_ms(HAL::uptime_ms());
        // Oversized scratch so snprintf can't truncation-warn; only the first
        // STATUS_CLOCK_LEN cells ("HH:MM:SS") are diffed/emitted below.
        char clk[16];
        snprintf(clk, sizeof(clk), "%02u:%02u:%02u",
                 (unsigned)wc.hour, (unsigned)wc.minute, (unsigned)wc.second);

        // Centre the field; skip on a terminal too narrow to fit it clear of the
        // right-aligned metrics.
        if (_cols < STATUS_CLOCK_LEN) return false;
        uint16_t clock_col   = (uint16_t)((_cols - STATUS_CLOCK_LEN) / 2);
        uint16_t metrics_col = (_cols > (uint16_t)(STATUS_METRICS_LEN - 1))
                                   ? (uint16_t)(_cols - (STATUS_METRICS_LEN - 1)) : 0;
        if ((uint16_t)(clock_col + STATUS_CLOCK_LEN) > metrics_col) return false;

        CursorPos saved_pos = _hw_pos;
        bool      saved_inv = _hw_inverted;
        bool      touched   = false;

        // Diff vs the cache and emit changed runs in normal video (hi = 0) so the
        // digits stand out against the reverse-video bar. Floor-gated by the caller, so
        // the (tiny) field is guaranteed to fit; no per-run budget check needed.
        uint16_t i = 0;
        while (i < STATUS_CLOCK_LEN) {
            if (clk[i] == _status_clock_cache[i]) { ++i; continue; }
            uint16_t j = (uint16_t)(i + 1);
            while (j < STATUS_CLOCK_LEN && clk[j] != _status_clock_cache[j]) ++j;
            touched = true;
            uint16_t wrote = emit_field_run(0, clock_col, clk, i, j, 0u);   // hi = 0 -> normal video
            memcpy(_status_clock_cache + i, clk + i, (size_t)wrote);        // cache only what was drawn
            if (wrote < (uint16_t)(j - i)) break;                           // truncated: retry next refresh
            i = j;
        }

        if (touched) {
            set_inverted(saved_inv);
            if (restore_cursor) move_to(saved_pos);
        }
        return touched;
    }

    // Serial / TX
    // ------------------------------------------------------------------------

    uint16_t AnsiTerminal::tx_available() const {
        return _serial.availableForWrite();
    }

    bool AnsiTerminal::tx_empty() const {
        return _serial.tx_empty();
    }

    // Internals
    // ------------------------------------------------------------------------

    int AnsiTerminal::_build_metrics(char* buf, size_t cap) const {
        if (_ctx.plc == nullptr) return 0;
        const char* plc_str;
        switch (_ctx.plc->status()) {
            case PlcIO::PLC_UNKNOWN:  plc_str = "UNK"; break;
            case PlcIO::PLC_RUNNING:  plc_str = "RUN"; break;
            case PlcIO::PLC_TIMEOUT:  plc_str = "TMO"; break;
            case PlcIO::PLC_FAULT:    plc_str = "FLT"; break;
            default:                  plc_str = "---"; break;
        }
        // The scheduler context may be absent (a site can wire a terminal without
        // one); show 0% rather than dereferencing null.
        unsigned cpu = 0, stk = 0;
        if (_ctx.scheduler != nullptr) {
            cpu = (unsigned)lroundf(_ctx.scheduler->cpu_load_ema    * 100.0f);
            stk = (unsigned)lroundf(_ctx.scheduler->stack_usage_ema * 100.0f);
        }
        if (cpu > 999) cpu = 999;
        if (stk > 999) stk = 999;
        return snprintf(buf, cap, "PLC:%s cpu:%3u%% stk:%3u%% ", plc_str, cpu, stk);
    }

}}
