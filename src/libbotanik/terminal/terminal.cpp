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


namespace Botanik { namespace Terminal {

    namespace {
        using namespace Botanik::Utils;
        using namespace Botanik::Pages;
    }

    // Lifecycle
    // ------------------------------------------------------------------------

    // Constructor
    AnsiTerminal::AnsiTerminal(Utils::BufferedSerial& serial, uint16_t cols, uint16_t rows,
                               uint32_t* timestamp_ms, const SystemContext& ctx) :
        _parser(serial, timestamp_ms),
        _serial(serial),
        _ctx(ctx),
        _cols(cols),
        _rows(rows)
    {
    }

    // Step through the terminal handling code
    void AnsiTerminal::step() {
        using Command = InputEvent::Command;
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
        if (now - _status_last_ms >= Config::STATUS_REFRESH_MS) {
            // Only refresh when the TX buffer can absorb the pass; otherwise leave
            // _status_last_ms so the next step() retries instead of waiting a full
            // interval.
            if (tx_available() >= STATUS_METRICS_TX_BUDGET) {
                _status_last_ms = now;
                update_status_metrics();
            }
        }
        while (true) {
            InputEvent& ev = _parser.parse();
            if (ev.type == InputEvent::EventType::NONE) break;
            if (_pages_len == 0 || _pages[_page_index] == nullptr) continue;
            Pages::Page* page = _pages[_page_index];
            if (ev.type == InputEvent::EventType::CONTENT) {
                page->handle_text_batch(ev.batch_data, ev.batch_len);
                break;
            } else if (ev.type == InputEvent::EventType::COMMAND) {
                // Terminal-level intercepts - consumed before reaching the page
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
                    // TAB / Ctrl+PageDown advance; Shift+TAB / Ctrl+PageUp step
                    // back. Both wrap around the ends.
                    const bool back = ev.command == Command::CTRL_PAGE_UP ||
                                      ev.command == Command::SHIFT_TAB;
                    switch_relative(!back);
                    break;
                }
                page->handle_command(ev.command, ev.count);
                break;
            }
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
            // Prefer a relative move when the cursor shares the target's row or
            // column; goto_absolute is for true diagonals only. A single-axis hop is
            // never longer than an absolute goto, so there's no delta where absolute
            // wins. target == _hw_pos returned above, so the differing axis is non-zero.
            if (target.row == _hw_pos.row) { // Same row
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
    void AnsiTerminal::write_chars(const char *s, uint16_t len, bool insert) {
        if (len == 0) return;

        // Lazy SM4/RM4 toggling: keep insert mode latched so consecutive inserts
        // cost just the char byte, instead of re-bracketing each with SM4/RM4.
        if (insert != _hw_insert_mode) {
            // Commit the latch only if the toggle reached the wire. Latching past a
            // dropped RM4 would let a later replace-mode write paint in insert mode
            // and corrupt the line; bail and let the caller redraw.
            if (!Ansi::set_insert_mode(_serial, insert)) return;
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
        // Remember the active page so a HAL::reset() re-exec returns to it (simulator
        // only; no-op on OPTA, where a reset clears RAM and comes back default).
        HAL::save_active_page(index);
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
        // The tx_discard above may have dropped a queued metrics update; invalidate
        // the diff cache so the next refresh repaints them (no bytes go out here).
        memset(_status_metrics_cache, 0, sizeof(_status_metrics_cache));
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

    void AnsiTerminal::update_status_metrics(bool restore_cursor) {
        char metrics[STATUS_METRICS_LEN];
        int mlen = _build_metrics(metrics, sizeof(metrics));

        // mlen is constant (fixed-width fields), so a byte index maps to a fixed col.
        uint16_t visible = (uint16_t)std::min((int)_cols, mlen);
        if (visible == 0) return;
        uint16_t metrics_col = (uint16_t)(_cols - visible);

        CursorPos saved_pos = _hw_pos;
        bool      saved_inv = _hw_inverted;
        bool      saved_cur = _hw_cursor_visible;
        bool      touched   = false;

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
            uint16_t run = (uint16_t)(j - i);
            if (tx_available() < (uint16_t)(run + 16)) break;
            // Hide the cursor before the first status-bar write so it doesn't blink
            // across row 0 while the metrics paint; restored below (no-op if already
            // hidden, e.g. on a non-shell page).
            if (!touched) set_cursor_visible(false);
            set_inverted(!value);
            touched = true;
            move_to({0, (uint16_t)(metrics_col + i)});
            write_chars(metrics + i, run);
            memcpy(_status_metrics_cache + i, metrics + i, (size_t)run);
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
        unsigned cpu = (unsigned)lroundf(_ctx.scheduler->cpu_load_ema    * 100.0f);
        unsigned stk = (unsigned)lroundf(_ctx.scheduler->stack_usage_ema * 100.0f);
        if (cpu > 999) cpu = 999;
        if (stk > 999) stk = 999;
        return snprintf(buf, cap, "PLC:%s cpu:%3u%% stk:%3u%% ", plc_str, cpu, stk);
    }

}}
