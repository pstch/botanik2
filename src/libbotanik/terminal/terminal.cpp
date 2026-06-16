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
        // The screen is established here, on the first step - not in setup_*() - so
        // the clear lands *after* the terminal-type handshake setup_*() queued (a
        // Minitel needs its PRO2 bytes while still in its native command set) and in
        // the order the protocol wants: clear, then the "ACAB" boot marker, then the
        // contents (status row + active page). A re-exec on reset() comes back
        // through this same path. switch_to() before the loop only records the active
        // page; all first paint happens here. _establish_screen() (not clear_screen())
        // so the queued handshake survives - clear_screen() would tx_discard it.
        if (!_screen_ready) {
            _establish_screen();
            draw_status_row();
            if (_pages_len > 0 && _pages[_page_index] != nullptr)
                _pages[_page_index]->focus();
        }
        uint64_t now = HAL::uptime_ms();
        if (now - _status_last_ms >= Config::STATUS_REFRESH_MS) {
            // Only refresh when the TX buffer can absorb the pass. When it can't,
            // leave _status_last_ms untouched so the next step() retries rather than
            // waiting out another full STATUS_REFRESH_MS interval.
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
                    // TAB and Ctrl+PageDown advance to the next page; Shift+TAB
                    // and Ctrl+PageUp step back. Both directions wrap around the
                    // ends.
                    const bool back = ev.command == Command::CTRL_PAGE_UP ||
                                      ev.command == Command::SHIFT_TAB;
                    switch_relative(!back);
                    break;
                }
                page->handle_command(ev.command, ev.count);
                break;
            }
        }
        // Tick every page, not just the active one: each page is its own
        // LogWriter and must drain its queue into its own scrollback on every
        // step so a later focus() replays an up-to-date tail. Inactive pages keep
        // their hands off the screen (gated internally on their active flag); only
        // the active page renders.
        for (uint8_t i = 0; i < _pages_len; i++)
            if (_pages[i] != nullptr) _pages[i]->tick(i == _page_index);
    }

    // Cursor & geometry
    // ------------------------------------------------------------------------

    // Move cursor to an absolute screen position
    void AnsiTerminal::move_to(const CursorPos& target_in, bool skip) {
        // The hardware caps the cursor at the last row/column when asked to
        // move past either edge, so any out-of-range target must be clamped
        // here to keep _hw_pos honest.
        CursorPos target = {
            (uint16_t)std::min((uint16_t)(_rows > 0 ? _rows - 1 : 0), target_in.row),
            (uint16_t)std::min((uint16_t)(_cols > 0 ? _cols - 1 : 0), target_in.col),
        };
        if (target == _hw_pos) {
            return;
        }

        // Emit the move all-or-nothing. If the escape can't be fully written (TX
        // buffer full), leave _hw_pos untouched so cursor tracking stays truthful
        // - a wrong _hw_pos would smear every later move/write. skip=true updates
        // tracking without emitting (caller already knows the hardware moved).
        bool ok = true;
        if (!skip) {
            // Optimizations for short moves, otherwise goto_absolute
            if (target.row == _hw_pos.row) { // Same row
                if (target.col == 0) {
                    ok = Ansi::move_row_start(_serial);
                } else if (target.col < _hw_pos.col && (_hw_pos.col - target.col) < 4) {
                    ok = Ansi::move_left(_serial, _hw_pos.col - target.col);
                } else if (target.col > _hw_pos.col && (target.col - _hw_pos.col) < 4) {
                    ok = Ansi::move_right(_serial, target.col - _hw_pos.col);
                } else {
                    ok = Ansi::goto_absolute(_serial, target);
                }
            } else if (target.col == _hw_pos.col) { // Same column
                if (target.row < _hw_pos.row && (_hw_pos.row - target.row) < 4) {
                    ok = Ansi::move_up(_serial, _hw_pos.row - target.row);
                } else if (target.row > _hw_pos.row && (target.row - _hw_pos.row) < 4) {
                    ok = Ansi::move_down(_serial, target.row - _hw_pos.row);
                } else {
                    ok = Ansi::goto_absolute(_serial, target);
                }
            } else if (target.col == 0 && target.row > _hw_pos.row
                       && (target.row - _hw_pos.row) < 4
                       && target.row < (uint16_t)(_rows - 1)) {
                // Common "spawn next row" target - CR + LF(s) costs delta+1
                // bytes vs ~8 for goto_absolute. Bounded away from the last row so
                // a stray LF can't scroll the screen.
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

        // Lazy SM4/RM4 toggling - re-bracketing every call burns 8 bytes per
        // single-char middle-of-line insert (SM4 + char + RM4 = 9 bytes,
        // ~90 ms at 900 baud). Keep insert mode latched so consecutive
        // inserts cost just the char byte, and only emit RM4 the first time
        // a non-insert write happens.
        if (insert != _hw_insert_mode) {
            // Commit the latch only if the toggle actually reached the wire.
            // BufferedSerial drops bytes when full; latching past a dropped RM4
            // would later let a replace-mode write (e.g. the status row) paint in
            // insert mode and corrupt the line. Bail rather than emit the payload
            // in the wrong mode - the caller (status refresh / page) redraws.
            if (!Ansi::set_insert_mode(_serial, insert)) return;
            _hw_insert_mode = insert;
        }

        // Track only what reached the wire. write() drops the tail when the TX
        // buffer fills; advancing _hw_pos by the full len would then desync cursor
        // tracking from the real screen and smear every later move/write. The
        // dropped glyphs are content loss a redraw fixes - a wrong cursor isn't.
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
                // Cursor is parked at the last cell. The cell holds a char we
                // just wrote; a follow-up clear_to_eol would erase it.
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
        // swallow the following payload bytes; the reserve guarantees it all lands.
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
        // Cursor position is typically not affected by DCH itself,
        // but the content shifts left. The logical cursor position remains.
    }

    // Erase content from cursor to the end of the line
    void AnsiTerminal::clear_to_eol() {
        // When the cursor is parked at the last cell because a write just filled
        // the row, there is nothing past it to erase - and the parked cell holds
        // valid content we'd otherwise destroy.
        if (_hw_at_eol) return;
        Ansi::clear_to_eol(_serial);
        // Cursor position is typically not affected by EL
    }

    // Erase from the cursor to the end of the display; the cursor does not move
    void AnsiTerminal::clear_to_eos() {
        Ansi::clear_to_eos(_serial);
        _hw_at_eol = false;
    }

    void AnsiTerminal::clear_screen() {
        // Every byte still queued for TX is about to paint onto a display we're
        // wiping - drop them first. That also empties the ring, so the (tiny)
        // clear sequence is guaranteed to fit and the _hw_pos reset below is
        // correct by construction, with no dropped-write path to handle.
        _serial.tx_discard();
        _establish_screen();
    }

    void AnsiTerminal::_establish_screen() {
        // Clear+home first, then drop the boot marker and reset tracked cursor/wrap
        // state. No TX discard here - the caller owns that (clear_screen drops stale
        // paint; the first-step bootstrap must instead keep the setup_*() handshake it
        // queued).
        Ansi::clear_screen(_serial);
        _hw_pos = {0, 0};
        _hw_at_eol = false;
        // Boot marker: emit "ACAB" once per process, right after the clear and before
        // any contents, so a fresh start (or a post-reset re-exec) is identifiable in
        // the stream. Gated on the first establish so Ctrl-L redraws don't repeat it.
        // The four glyphs print at the home cell and advance the cursor four columns;
        // track that so the status-row redraw that overwrites them moves from the
        // right place. (cols >= 5 always, so this can't wrap.)
        if (!_screen_ready) {
            _serial.write("ACAB", 4);
            _hw_pos.col = 4;
        }
        // Auto-wrap stays off for the whole UI: every page pre-wraps its own content
        // to the width, so the terminal must never wrap on its own. A deferred wrap
        // on a full-width line parks the cursor pending at the last cell - the next
        // clear_to_eol then eats that glyph and the following row drops its first
        // char. Re-assert it on every establish, and emit *unconditionally*: a prior
        // copy may have been discarded and _hw_auto_wrap may already read false, so
        // gating on it would skip this and leave the terminal wrapping. Sent on every
        // terminal type: a VT102 honors it; a Minitel that ignores it loses nothing,
        // and one that honors it gets free right-margin protection.
        set_auto_wrap(false);
        // Hide the cursor by default - only the shell editor wants it visible, and
        // it opts back in from its focus(). A fresh clear leaves the real cursor
        // shown (VT100 default), so force the emit by flipping the shadow first;
        // the lazy guard would otherwise skip it and leave a stray blinking cursor.
        _hw_cursor_visible = true;
        set_cursor_visible(false);
        // Reset reverse-video and insert mode. Neither is cleared by ED, so after a
        // reset the unit may still hold them while our freshly-defaulted shadows read
        // "normal" - which corrupts every later write. Force both emits to resync:
        // flip the inverted shadow past the lazy guard, and assert RM4 directly
        // (write_chars only toggles IRM lazily off its shadow).
        _hw_inverted = true;
        set_inverted(false);                    // -> SGR normal (ESC[m)
        Ansi::set_insert_mode(_serial, false);  // -> replace mode (ESC[4l)
        _hw_insert_mode = false;
        _screen_ready = true;
    }

    void AnsiTerminal::setup_vt102() {
        // Plain VT100/VT102 (the default). No special handshake; DECAWM (?7l) is
        // (re)asserted on every screen establish by _establish_screen(). The screen
        // itself is not cleared here - that is deferred to the first step() so the
        // clear lands after this setup and before any contents.
        Ansi::vt102_setup(_serial);
    }

    void AnsiTerminal::setup_minitel() {
        // Drive a real Minitel (1B+/2) over the DIN link. The PRO2 handshake - sent
        // before the ANSI clear, while the unit is still in its native command set -
        // selects 80-col téléinformatique mode, a lowercase keyboard, and page mode.
        // The clear is deferred to the first step() (which also sends DECAWM ?7l:
        // harmless if the unit ignores it, free right-margin protection if it honors
        // it), so this handshake reaches the wire while the unit is still native.
        Ansi::minitel_setup(_serial);
    }

    void AnsiTerminal::clear_content() {
        // Erase the page content area (rows 1..N-1), leaving the status bar on
        // row 0 untouched. One erase-to-end-of-display from {1,0} is far cheaper
        // than clearing each row in a loop.
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
            // At the last row: scroll only the content area (rows 1..N-1) so that
            // row 0 (status bar) is never touched by a hardware scroll.
            // DL 1 at row 1 pulls rows 1..N-1 upward and inserts a blank at row N-1.
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
        Ansi::insert_rows(_serial, count);
        // Cursor position is typically not affected by IL, only content shifts.
    }

    // Delete rows
    void AnsiTerminal::delete_rows(uint16_t count) {
        if (count == 0) return;
        Ansi::delete_rows(_serial, count);
        // Cursor position is typically not affected by DL, only content shifts.
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
        // Remember the active page so a HAL::reset() re-exec returns to the page you
        // were on rather than the default landing page (simulator only; a no-op on
        // OPTA, where a hardware reset clears RAM and the board comes back default).
        HAL::save_active_page(index);
        if (!_screen_ready) {
            // Screen establish is deferred to the first step() (so the clear lands
            // after the setup_*() handshake, in the order clear -> ACAB -> contents).
            // Drawing onto an unestablished screen here would be wiped by that clear,
            // so just record the target page; the bootstrap clears the screen, draws
            // the status row, and focuses it.
            _page_index = index;
            return;
        }
        if (_pages[_page_index] != nullptr)
            _pages[_page_index]->deactivate();
        _page_index = index;
        // Page change: keep row 0; only the content area is cleared. Drop the bytes
        // still queued for the page we're leaving first - they'd paint onto the page
        // we're switching to - exactly like Ctrl^L's full clear does. _hw_pos is stale
        // once the queue is dropped, so reposition with an absolute move (not the
        // relative-optimized move_to) before erasing.
        _serial.tx_discard();
        Ansi::goto_absolute(_serial, {1, 0});
        _hw_pos = {1, 0};
        _hw_at_eol = false;
        clear_to_eos();
        // Redraw the status row so its left-hand page name reflects the new page.
        draw_status_row();
        // Cursor is hidden by default for every page; the one that wants it (the
        // shell editor) shows it from its focus(). Lazy, so a no-op when already off.
        set_cursor_visible(false);
        _pages[_page_index]->focus();
    }

    // Status bar
    // ------------------------------------------------------------------------

    void AnsiTerminal::draw_status_row() {
        // Build right-side metrics string (fixed-width field, see STATUS_METRICS_LEN)
        char metrics[STATUS_METRICS_LEN];
        int mlen = _build_metrics(metrics, sizeof(metrics));

        // Keep the metrics cache in sync so update_status_metrics() won't re-draw immediately.
        memcpy(_status_metrics_cache, metrics, (size_t)mlen);

        // Save state
        CursorPos saved_pos = _hw_pos;
        bool      saved_inv = _hw_inverted;

        // Draw full inverted row 0. Insert/replace mode is owned by write_chars
        // (insert defaults to false here), so the status row always overwrites.
        move_to({0, 0});

        // Establish the reverse-video background deterministically before the tabs.
        // The cached attribute can be stale after switch_to()/clear_screen() dropped
        // an un-sent SGR with tx_discard; without forcing the emit here, the active
        // tab's set_inverted(false) below becomes a wrong no-op when the active page
        // is the *first* tab (its reset is never sent, so its name inherits whatever
        // attribute the terminal happens to hold). Force the emit by clearing the
        // tracked state first.
        _hw_inverted = false;
        set_inverted(true);

        // Three blank leading columns, then one " NAME " tab per registered page.
        // The whole bar is reverse-video; the active page's tab is drawn in normal
        // video so it stands out from the others.
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
        // 31 spaces + trailing NUL. The NUL must never reach the wire: terminals
        // drop it without advancing the cursor, but write_chars bumps _hw_pos by
        // the full length anyway, desyncing cursor tracking by one column.
        static const char spaces[32] = "                               ";
        constexpr uint16_t SPACES_LEN = sizeof(spaces) - 1;
        while (_hw_pos.col < metrics_col) {
            uint16_t chunk = std::min((uint16_t)(metrics_col - _hw_pos.col), SPACES_LEN);
            write_chars(spaces, chunk);
        }

        // Right: metrics (truncate if terminal is very narrow)
        uint16_t remaining = (_cols > _hw_pos.col) ? (_cols - _hw_pos.col) : 0;
        uint16_t write_len = (uint16_t)std::min((int)remaining, mlen);
        if (write_len > 0) write_chars(metrics, write_len);

        // Restore state
        set_inverted(saved_inv);
        move_to(saved_pos);
    }

    void AnsiTerminal::update_status_metrics() {
        char metrics[STATUS_METRICS_LEN];
        int mlen = _build_metrics(metrics, sizeof(metrics));

        // mlen is constant across calls (all fields are fixed-width), so a byte
        // index in metrics maps to a fixed screen column.
        uint16_t visible = (uint16_t)std::min((int)_cols, mlen);
        if (visible == 0) return;
        uint16_t metrics_col = (uint16_t)(_cols - visible);

        CursorPos saved_pos = _hw_pos;
        bool      saved_inv = _hw_inverted;
        bool      touched   = false;

        int i = 0;
        while (i < visible) {
            if (metrics[i] == _status_metrics_cache[i]) { ++i; continue; }
            int j = i + 1;
            while (j < visible && metrics[j] != _status_metrics_cache[j]) ++j;
            // Only emit a run if the whole of it (move + optional invert + chars)
            // fits; otherwise stop and retry the rest next refresh, leaving the
            // cache for the unwritten cells stale so they are picked up again.
            uint16_t run = (uint16_t)(j - i);
            if (tx_available() < (uint16_t)(run + 16)) break;
            if (!touched) { set_inverted(true); touched = true; }
            move_to({0, (uint16_t)(metrics_col + i)});
            write_chars(metrics + i, run);
            memcpy(_status_metrics_cache + i, metrics + i, (size_t)run);
            i = j;
        }

        if (touched) {
            set_inverted(saved_inv);
            move_to(saved_pos);
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
        if (cpu > 9999) cpu = 9999;
        if (stk > 999)  stk = 999;
        return snprintf(buf, cap, "PLC:%s cpu:%4u%% stk:%3u%% ", plc_str, cpu, stk);
    }

}}
