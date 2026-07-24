// REVIEW_35: TODO
/**
 * @file console.cpp
 * @brief Console implementation - page multiplexing, bars, input routing, step().
 */
#include <cstdint>

#include "libbotanik_config.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/console/console.h"

namespace Botanik { namespace Terminal {

    // Lifecycle

    Console::Console(Utils::BufferedSerial& serial, uint16_t cols, uint16_t rows,
                     const uint32_t* timestamp_ms, uint32_t baud,
                     const SystemContext& ctx, const Alarms::AlarmModel* alarms,
                     Logs::Logger* logger) :
        Component(logger, "Console"),
        _term(serial, cols, rows, baud),
        _parser(serial, timestamp_ms),
        _ctx(ctx),
        _alarms(alarms),
        _status_bar(*this),
        _alarm_bar(*this)
    {
        // bottom row reserved for the alarm bar only when alarms are wired
        _reserved_bottom = (_alarms != nullptr) ? 1 : 0;
    }

    void Console::on_step(uint64_t now_ms) {
        // Establish the screen on the first step (not in setup_*()) so the clear
        // lands after the handshake setup_*() queued (establish_screen(), not
        // clear_screen, so it keeps the queued handshake bytes).
        if (!_term.screen_ready()) {
            _term.establish_screen();
            // Full redraw order: status bar, alarm bar, then the page. Order is
            // cosmetic now that present() reconciles the whole frame at once, but
            // it keeps the chrome first if a paced frame only gets partway.
            _status_bar.draw_row();
            _alarm_bar.mark_dirty();      // the clear wiped the alarm-bar row
            _alarm_bar.step();
            if (_pages_len > 0 && _pages[_page_index] != nullptr) {
                _reset_content_priority();
                _pages[_page_index]->focus();
            }
        }
        // Status bar paints first each tick (self-contained), interleaving with
        // the active page's draw.
        _status_bar.step(now_ms);
        // Drain buffered input into the active page (terminal-level intercepts
        // like Ctrl-L and page switches are consumed first).
        _pump_input();
        // Tick every page, not just the active one: each is its own LogWriter and
        // must drain into its scrollback every step so a later focus() replays an
        // up-to-date tail. Inactive pages don't render (gated on their active flag).
        for (uint8_t i = 0; i < _pages_len; i++)
            if (_pages[i] != nullptr) _pages[i]->tick(i == _page_index);
        // Last: top up the bottom alarm bar over whatever the active page drew.
        _alarm_bar.step();
        // Reconcile the frame: diff the FRONT grid the renderers just drew against
        // the wire and emit the minimal, paced ANSI to catch it up.
        _term.present();
    }

    void Console::_pump_input() {
        using Command = InputEvent::Command;
        // Handle up to INPUT_EVENTS_PER_TICK events this tick so a paste or fast
        // typing stays responsive without letting a flood monopolise the loop. A
        // heavy terminal intercept (full clear/redraw or a page switch) does its
        // one repaint and yields the rest of the tick so two can never stack.
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
                _term.clear_screen();
                // Chrome first (status bar, then alarm bar), then the page.
                _status_bar.draw_row();
                _alarm_bar.mark_dirty();   // clear_screen wiped the alarm-bar row
                _alarm_bar.step();
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
                // TAB / Ctrl+PageDown advance; Shift+TAB / Ctrl+PageUp step back.
                const bool back = ev.command == Command::CTRL_PAGE_UP ||
                                  ev.command == Command::SHIFT_TAB;
                switch_relative(!back);
                break;
            }
            // Ctrl+C: let the page consume it (e.g. cancel an input line); if it
            // doesn't, apply the default - quit on the simulator (Ctrl+C arrives
            // as a byte in raw mode), a no-op on hardware.
            if (ev.command == Command::CTRL_C) {
                if (!page->handle_cancel()) {
#ifdef TARGET_SIMULATOR
                    HAL::System::shutdown();
#endif
                }
                break;
            }
            // Light page command (edit/cursor/etc.): keep handling more this tick.
            page->handle_command(ev.command, ev.count);
        }
    }

    // Content-area geometry & scrolling

    void Console::clear_content() {
        if (_reserved_bottom == 0) {
            // No reserved bar: one erase-to-end-of-display clears rows 1..N-1,
            // leaving the row-0 status bar.
            _term.move_to({1, 0});
            _term.clear_to_eos();
            return;
        }
        // A reserved alarm bar sits below the content, so erase-to-end-of-display
        // would wipe it. Clear just the content rows (clear_rows scrolls the band
        // off by its full height, leaving the bar in place).
        _term.clear_rows(1, _content_bottom());
    }

    void Console::line_feed(bool carriage_return) {
        uint16_t content_bottom = _content_bottom();
        if (_term.cursor_pos().row < content_bottom) {
            // Normal case: cursor moves into the next row.
            _term.newline(carriage_return);
        } else {
            // At the bottom of the content band: scroll it up one so row 0
            // (status) and the reserved bottom row are never touched, then drop
            // onto the freed bottom row.
            uint16_t target_col = carriage_return ? 0 : _term.cursor_pos().col;
            _term.scroll_region(1, content_bottom, /*up=*/true, 1);
            _term.move_to({content_bottom, target_col});
        }
    }

    // Pages

    int8_t Console::add_page(Pages::Page* page) {
        if (page == nullptr || _pages_len >= 8) return -1;
        _pages[_pages_len] = page;
        return (int8_t)(_pages_len++);
    }

    void Console::switch_relative(bool forward) {
        if (_pages_len == 0) return;
        uint8_t next = forward
            ? (uint8_t)((_page_index + 1) % _pages_len)
            : (uint8_t)((_page_index + _pages_len - 1) % _pages_len);
        switch_to(next);
    }

    void Console::switch_to(uint8_t index) {
        if (index >= _pages_len || _pages[index] == nullptr) return;
        if (!_term.screen_ready()) {
            // Establish is deferred to the first step(); drawing here would be
            // wiped by that clear, so just record the target page.
            _page_index = index;
            return;
        }
        if (_pages[_page_index] != nullptr)
            _pages[_page_index]->deactivate();
        _page_index = index;
        // Hide the cursor before any repaint so it doesn't blink across the
        // cleared content or the row-0 tabs. focus() shows it again if wanted.
        _term.set_cursor_visible(false);
        // Clear only the content band: preserves the row-0 status bar and the
        // bottom alarm bar. The new page's focus() redraws it and the back-buffer
        // diff emits just the net change - no wipe-and-repaint flicker.
        clear_content();
        // The active tab expands to "N:NAME" and the rest collapse to a number,
        // so a switch reflows the whole strip (widths shift); repaint it as a unit.
        _status_bar.draw_tabs();
        // The alarm bar is NOT redrawn here: clear_content() preserves its bottom
        // row, so its already-drawn content survives the switch unchanged. Its
        // step() repaints it only when the alarm set actually moves (a switch that
        // coincides with an alarm change is caught by the next set-change check).
        _reset_content_priority();
        _pages[_page_index]->focus();
    }

    void Console::_reset_content_priority() {
        // The content band belongs to whichever page is on it, so hand it back as
        // plain structure on a switch. The incoming page declares anything of its
        // own in focus() - without this the outgoing page's classes (the shell
        // editor's rows, say) would outrank the new page's content indefinitely.
        _term.set_row_priority(1, _content_bottom(), Terminal::PRIO_STRUCT);
    }

}}
