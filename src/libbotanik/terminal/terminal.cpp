// REVIEW_35: TODO
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


namespace Botanik { namespace Terminal {

    namespace {
        using namespace Botanik::Utils;
        using namespace Botanik::Pages;
    }

    // Lifecycle

    AnsiTerminal::AnsiTerminal(Utils::BufferedSerial& serial, uint16_t cols,
                               uint16_t rows, const uint32_t* timestamp_ms,
                               uint32_t baud, const SystemContext& ctx,
                               const Alarms::AlarmModel* alarms,
                               Logs::Logger* logger) :
        _parser(serial, timestamp_ms),
        _serial(serial),
        _ctx(ctx),
        _cols(cols),
        _rows(rows),
        _alarms(alarms),
        _logger(logger),
        _status_bar(*this),
        _alarm_bar(*this)
    {
        // Size the shared cooperative-TX depth floor from the link baud so the
        // felt echo latency target (Config::TX_COLD_TARGET_MS) holds across
        // links.
        _tx_cold_depth = depth_for_baud(baud, Config::TX_COLD_TARGET_MS);
        // bottom row only when alarms wired
        _reserved_bottom = (_alarms != nullptr) ? 1 : 0;
    }

    void AnsiTerminal::step() {
        announce_step(_logger, "AnsiTerminal");
        // Snapshot the RX depth before the parse loop below drains it, so the
        // serial gauge reports what was actually waiting this cycle rather than
        // ~0 post-drain.
        _rx_cycle_used = _serial.available();
        // Establish the screen on the first step (not in setup_*()) so the
        // clear lands after the handshake setup_*() queued, in order: clear,
        // "ACAB" boot marker, contents. _establish_screen() (not clear_screen)
        // so the queued handshake survives the tx_discard clear_screen would
        // do.
        if (!_screen_ready) {
            _establish_screen();
            _status_bar.draw_row();
            if (_pages_len > 0 && _pages[_page_index] != nullptr)
                _pages[_page_index]->focus();
        }
        // Status bar paints first each tick (self-contained), interleaving with
        // the active page's draw.
        _status_bar.step(HAL::Clock::uptime_ms());
        // Drain buffered input into the active page (terminal-level intercepts
        // like Ctrl-L and page switches are consumed first).
        _pump_input();
        // Tick every page, not just the active one: each is its own LogWriter
        // and must drain into its scrollback every step so a later focus()
        // replays an up-to-date tail. Inactive pages don't render (gated on
        // their active flag).
        for (uint8_t i = 0; i < _pages_len; i++)
            if (_pages[i] != nullptr) _pages[i]->tick(i == _page_index);
        // Last: top up the bottom alarm bar over whatever the active page just
        // drew. Self-gated (no-op unless the set changed or the row was wiped)
        // and saves/restores the page cursor, so it never disturbs a settled
        // page.
        _alarm_bar.step();
    }

    void AnsiTerminal::_pump_input() {
        using Command = InputEvent::Command;
        // Handle up to INPUT_EVENTS_PER_TICK events this tick so a paste or
        // fast typing stays responsive without letting a flood monopolise the
        // loop. Light events (text echo, cursor/edit commands) keep iterating;
        // a heavy terminal intercept (full clear/redraw or a page switch) does
        // its one repaint and yields the rest of the tick so two full repaints
        // can never stack.
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
            // Terminal-level intercepts - consumed before reaching the page.
            // These repaint the whole screen, so each breaks (takes the rest of
            // the tick).
            if (ev.command == Command::CTRL_L) {
                clear_screen();
                _status_bar.draw_row();
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
                // back. Both wrap.
                const bool back = ev.command == Command::CTRL_PAGE_UP ||
                                  ev.command == Command::SHIFT_TAB;
                switch_relative(!back);
                break;
            }
            // Ctrl+C: let the page consume it (e.g. cancel an input line); if
            // it doesn't, apply the default - quit on the simulator (Ctrl+C
            // arrives as a byte in raw mode), a no-op on hardware so a stray
            // Ctrl+C can't reboot the controller.
            if (ev.command == Command::CTRL_C) {
                if (!page->handle_cancel()) {
#ifdef TARGET_SIMULATOR
                    HAL::System::shutdown();
#endif
                }
                break;
            }
            // Light page command (edit/cursor/etc.): keep handling more this
            // tick.
            page->handle_command(ev.command, ev.count);
        }
    }

    // Cursor & geometry

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

        // All-or-nothing: on TX-full leave _hw_pos untouched (a wrong _hw_pos
        // smears every later write). skip=true tracks without emitting.
        bool ok = true;
        if (!skip) {
            // After a row-filling write the real column is unknown (a wider
            // terminal parks further right), so a relative move lands short:
            // reposition absolutely.
            if (_hw_at_eol) {
                ok = Ansi::goto_absolute(_serial, target);
            }
            // Prefer a relative move when sharing the target's row or column
            // (never longer than an absolute goto); absolute is for true
            // diagonals only.
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
                // goto_absolute. Bounded off the last row so a stray LF can't
                // scroll.
                ok = Ansi::down_to_row_start(_serial,
                                             (uint16_t)(target.row - _hw_pos.row));
            } else { // Different row and column, use absolute move
                ok = Ansi::goto_absolute(_serial, target);
            }
        }
        if (ok) {
            _hw_pos = target;
            _hw_at_eol = false;
        }
    }

    void AnsiTerminal::restore_cursor(const CursorSnapshot& snap, bool restore_pos) {
        set_inverted(snap.inverted);
        if (restore_pos) {
            move_to(snap.pos);
            // move_to() cleared it; the page may have parked at EOL.
            _hw_at_eol = snap.at_eol;
        }
        set_cursor_visible(snap.cursor_visible);
    }

    // Text output & character attributes

    uint16_t AnsiTerminal::write_chars(const char *s, uint16_t len, bool insert) {
        if (len == 0) return 0;

        // Lazy SM4/RM4 toggling: keep insert mode latched so consecutive
        // inserts cost just the char byte, instead of re-bracketing each with
        // SM4/RM4.
        if (insert != _hw_insert_mode) {
            // Commit the latch only if the toggle reached the wire. Latching
            // past a dropped RM4 would let a later replace-mode write paint in
            // insert mode and corrupt the line; bail and let the caller redraw.
            if (!Ansi::set_insert_mode(_serial, insert)) return 0;
            _hw_insert_mode = insert;
        }

        // Track only what reached the wire: write() drops the tail when TX
        // fills, so advancing _hw_pos by the full len would desync tracking.
        // Dropped glyphs are content loss a redraw fixes; a wrong cursor isn't.
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
                // Parked at the last cell, which holds a char we just wrote;
                // flag it so a follow-up clear_to_eol doesn't erase it.
                _hw_at_eol = true;
            }
        } else {
            _hw_pos.col = (uint16_t)new_col;
            _hw_at_eol = false;
        }
        return written;
    }

    void AnsiTerminal::set_inverted(bool enable) {
        if (enable == _hw_inverted) return;
        // Reserve the longest form (CSI + ATTR_NEG) up front so a split SGR
        // can't swallow following payload bytes.
        constexpr uint16_t SGR_MAX =
            (sizeof(Ansi::CSI) - 1) + (sizeof(Ansi::ATTR_NEG) - 1);
        if (_serial.availableForWrite() < SGR_MAX) return;

        _serial.write(Ansi::CSI, sizeof(Ansi::CSI) - 1);
        if (enable) _serial.write(Ansi::ATTR_NEG,  sizeof(Ansi::ATTR_NEG) - 1);
        else        _serial.write(Ansi::ATTR_NONE, sizeof(Ansi::ATTR_NONE) - 1);
        _hw_inverted = enable;
    }

    void AnsiTerminal::set_auto_wrap(bool enable) {
        Ansi::set_auto_wrap(_serial, enable);
        _hw_auto_wrap = enable;
    }

    void AnsiTerminal::set_cursor_visible(bool show) {
        if (show == _hw_cursor_visible) return;
        // Only commit the tracked state if the sequence actually reached the
        // wire, so a TX-full skip is retried on the next call rather than
        // silently lost.
        if (!Ansi::set_cursor_visible(_serial, show)) return;
        _hw_cursor_visible = show;
    }

    void AnsiTerminal::fill_spaces(uint16_t to_col) {
        // 31 spaces + NUL; the NUL must never reach the wire (terminals drop it
        // without moving, but write_chars would still bump _hw_pos by its len).
        static const char sp[32] = "                               ";
        constexpr uint16_t SP_LEN = sizeof(sp) - 1;
        // Drive termination off a local counter, not _hw_pos.col: filling to the
        // last column parks the cursor at _cols-1 (auto-wrap is off UI-wide), so
        // it never reaches to_col == _cols and a _hw_pos.col loop would spin.
        for (uint16_t col = _hw_pos.col; col < to_col; col = (uint16_t)(col + SP_LEN)) {
            write_chars(sp, std::min((uint16_t)(to_col - col), SP_LEN));
        }
    }

    // Content manipulation

    void AnsiTerminal::delete_chars(uint16_t len) {
        if (len == 0) return;
        // DCH shifts content left; cursor unaffected
        Ansi::delete_chars(_serial, len);
    }

    void AnsiTerminal::clear_to_eol() {
        // Cursor parked at the last cell after a row-filling write: nothing
        // past it to erase, and the parked cell holds valid content.
        if (_hw_at_eol) return;
        Ansi::clear_to_eol(_serial);  // EL leaves the cursor in place
    }

    void AnsiTerminal::clear_to_eos() {
        Ansi::clear_to_eos(_serial);
        _hw_at_eol = false;
    }

    void AnsiTerminal::clear_screen() {
        // Drop bytes still queued for TX - they'd paint onto a display we're
        // wiping. Emptying the ring also guarantees the clear sequence fits, so
        // the _hw_pos reset below is correct with no dropped-write path.
        _serial.tx_discard();
        // Resync the terminal: the discard may have torn a half-sent sequence
        // on the wire, which would otherwise swallow the leading bytes of the
        // clear below.
        Ansi::cancel_sequence(_serial);
        _establish_screen();
    }

    void AnsiTerminal::clear_screen_now() {
        clear_screen();
        // Push the clear out now: the loop won't run again to drain the ring.
        // transmit() never blocks, so spin until empty, bounded so a stuck wire
        // can't hang the exit path.
        for (uint16_t i = 0; i < tx_capacity() && !_serial.tx_empty(); i++)
            _serial.transmit();
    }

    void AnsiTerminal::_establish_screen() {
        // Clear+home, then reset tracked cursor/wrap state. No TX discard here
        // - the caller owns that (clear_screen drops stale paint; the
        // first-step bootstrap keeps the setup_*() handshake it queued).
        Ansi::clear_screen(_serial);
        _hw_pos = {0, 0};
        _hw_at_eol = false;
        // Boot marker: emit "ACAB" once per process (not on Ctrl-L redraws) to
        // mark a fresh start in the stream. Track the 4 cols it advances (cols
        // >= 5, so it can't wrap).
        if (!_screen_ready) {
            _serial.write("ACAB", 4);
            _hw_pos.col = 4;
        }
        // Auto-wrap stays off UI-wide (pages pre-wrap; a stray wrap parks the
        // cursor and the next clear_to_eol eats a glyph). Emit unconditionally
        // (a prior copy may have been discarded).
        set_auto_wrap(false);
        // Hide the cursor by default (only the shell editor opts back in via
        // focus()). A fresh clear leaves it shown, so force the emit by
        // flipping the shadow past the lazy guard.
        _hw_cursor_visible = true;
        set_cursor_visible(false);
        // Reset reverse-video and insert mode: ED clears neither, so the unit
        // may still hold them while our shadows read "normal". Force both emits
        // to resync.
        force_inverted(false);                  // -> SGR normal (ESC[m)
        Ansi::set_insert_mode(_serial, false);  // -> replace mode (ESC[4l)
        _hw_insert_mode = false;
        _screen_ready = true;
        // The clear wiped the alarm bar's row; flag it for the next step's bar
        // pass.
        _alarm_bar.mark_dirty();
    }

    void AnsiTerminal::setup_vt102() {
        // Plain VT100/VT102 (the default): no special handshake. DECAWM and the
        // clear are handled later (_establish_screen / first step()).
        Ansi::vt102_setup(_serial);
    }

    void AnsiTerminal::setup_minitel() {
        // Drive a real Minitel (1B+/2) over the DIN link. The PRO2 handshake
        // (sent before the ANSI clear, while the unit is still native) selects
        // 80-col teleinformatique mode, a lowercase keyboard, and page mode.
        Ansi::minitel_setup(_serial);
    }

    void AnsiTerminal::clear_content() {
        if (_reserved_bottom == 0) {
            // No reserved bar: one erase-to-end-of-display clears rows 1..N-1,
            // leaving the row-0 status bar.
            move_to({1, 0});
            clear_to_eos();
            return;
        }
        // A reserved alarm bar sits below the content, so
        // erase-to-end-of-display would wipe it. Clear just the content rows
        // instead (clear_rows scrolls the band off by its full height, leaving
        // the bar in place).
        clear_rows(1, _content_bottom());
    }

    void AnsiTerminal::line_feed(bool carriage_return) {
        uint16_t content_bottom = _content_bottom();
        if (_hw_pos.row < content_bottom) {
            // Normal case: cursor moves into the next row
            if (carriage_return) {
                _serial.write("\r\n", 2);
                _hw_pos.col = 0;
            } else {
                _serial.write("\n", 1);
            }
            _hw_pos.row++;
        } else {
            // At the bottom of the content area: scroll the content band (rows
            // 1..content_bottom) up one so the row-0 status bar - and the
            // reserved bottom row, once there is one - are never touched, then
            // drop onto the freed bottom row.
            uint16_t target_col = carriage_return ? 0 : _hw_pos.col;
            scroll_region(1, content_bottom, /*up=*/true, 1);
            move_to({content_bottom, target_col});
        }
    }

    void AnsiTerminal::scroll_region(uint16_t top, uint16_t bottom, bool up,
                                     uint16_t n) {
        if (n == 0 || top > bottom) return;
        uint16_t phys_last = (uint16_t)(_rows > 0 ? _rows - 1 : 0);
        if (bottom > phys_last) bottom = phys_last;
        uint16_t height = (uint16_t)(bottom - top + 1);
        if (n > height) n = height;   // scrolling the whole band just clears it

        // DL/IL pair: the first op displaces every row below the band, the
        // compensating op restores it (a band running to the physical bottom
        // needs no restore). Best-effort: sub-ops are all-or-nothing, so a tight
        // ring drops the scroll rather than tearing the layout.
        bool restore = (bottom < phys_last);

        if (up) {
            // Drop the top n rows: DL n at the band top slides rows
            // top+n..bottom up to top..bottom-n (and pulls everything below the
            // band up with them).
            move_to({top, 0});
            delete_rows(n);
            // IL n at the band's new bottom pushes the displaced below-band
            // rows back down and leaves the n freed rows blank. Skipped when
            // the band runs to the screen bottom: the DL already left those
            // rows blank and there is nothing below to restore.
            if (restore) {
                move_to({(uint16_t)(bottom - n + 1), 0});
                insert_rows(n);
            }
        } else {
            // Drop the bottom n rows first (when the band stops short of the
            // screen bottom) so the IL below cannot shove band content onto
            // rows it must leave alone; when the band runs to the bottom those
            // rows just fall off.
            if (restore) {
                move_to({(uint16_t)(bottom - n + 1), 0});
                delete_rows(n);
            }
            // IL n at the band top slides rows top..bottom-n down to
            // top+n..bottom and opens the n freed rows at the top.
            move_to({top, 0});
            insert_rows(n);
        }
    }

    void AnsiTerminal::insert_rows(uint16_t count) {
        if (count == 0) return;
        // IL shifts content; cursor unaffected
        Ansi::insert_rows(_serial, count);
    }

    void AnsiTerminal::delete_rows(uint16_t count) {
        if (count == 0) return;
        // DL shifts content; cursor unaffected
        Ansi::delete_rows(_serial, count);
    }

    // Pages

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
            // Establish is deferred to the first step(); drawing here would be
            // wiped by that clear, so just record the target page for the
            // bootstrap.
            _page_index = index;
            return;
        }
        if (_pages[_page_index] != nullptr)
            _pages[_page_index]->deactivate();
        uint8_t prev = _page_index;
        _page_index = index;
        // Drop bytes queued for the page we're leaving (they'd paint onto the
        // new one). _hw_pos is stale after the drop, so reposition absolutely.
        _serial.tx_discard();
        // The discard can strand a half-sent sequence on the wire;
        // cancel_sequence() resyncs to ground state so the tab repaint below
        // isn't swallowed by a stuck parser (that swallow is what left some tab
        // chars un-inverted under TX load).
        Ansi::cancel_sequence(_serial);
        // Hide the cursor before any repaint so it doesn't blink across the
        // cleared content or the row-0 tabs. The page that wants it (shell
        // editor) shows it again from focus(). Lazy, so a no-op when already
        // off.
        set_cursor_visible(false);
        Ansi::goto_absolute(_serial, {1, 0});
        _hw_pos = {1, 0};
        _hw_at_eol = false;
        // Clear only the content band: clear_content() preserves both the row-0
        // status bar and the bottom alarm bar (no wipe-and-repaint flicker on a
        // page switch).
        clear_content();
        // Only the tabs' video flips on a switch, so repaint just the two
        // affected tabs (the one we left, the one we entered) rather than the
        // whole bar.
        _status_bar.draw_tab(prev);
        if (prev != index) _status_bar.draw_tab(index);
        // The tx_discard may have dropped a queued row-0 update; invalidate the
        // diff caches so the next refresh repaints them in full.
        _status_bar.invalidate();
        // The discard can likewise tear a half-sent alarm-bar repaint; the set
        // rarely changes, so without re-flagging the torn bar would never
        // recover.
        _alarm_bar.mark_dirty();
        _pages[_page_index]->focus();
    }

    // Field painter (shared by StatusBar and MetricPage)

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

    void AnsiTerminal::paint_field_runs(uint16_t row, uint16_t col, const char* cf,
                                        uint16_t width, uint32_t emit, uint32_t hi) {
        if (emit == 0) return;

        // Bridge clean gaps up to MERGE wide: a ~7-byte cursor move costs more
        // than overwriting a couple of unchanged characters in place.
        const uint16_t MERGE = 4;
        uint32_t render = emit;
        int last = -1;
        for (uint16_t i = 0; i < width; ++i) {
            if (!((emit >> i) & 1u)) continue;
            if (last >= 0 && (int)i - last - 1 <= (int)MERGE)
                for (int g = last + 1; g < (int)i; ++g) render |= (1u << g);
            last = (int)i;
        }

        uint16_t i = 0;
        while (i < width) {
            if (!((render >> i) & 1u)) { i++; continue; }
            uint16_t a = i;
            while (i < width && ((render >> i) & 1u)) i++;
            // [a, i); SGR segmented per hi
            emit_field_run(row, col, cf, a, i, hi);
        }
        set_inverted(false);   // leave the terminal normal
    }

}}
