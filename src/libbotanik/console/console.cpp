// REVIEW_35: TODO
/**
 * @file console.cpp
 * @brief Console implementation - page multiplexing, bars, input routing, step().
 */
#include "libbotanik/console/console.h" // Console

#include "libbotanik/pages/page.h"      // Pages::Page - complete type for the calls below

#include <cstdint>                      // int8_t/uint16_t/uint64_t

#include "libbotanik/config/scheduler.h"  // Config::LOOP_* / TASK_* / GROUP_*
#include "libbotanik/config/terminal.h"   // Config::SCREEN_* / EDITOR_* / SHELL_* / PAGE_*

#include "libbotanik/hal/platform.h"    // HAL::System::shutdown
#include "libbotanik/tasks/tasks.h"     // Tasks::Registry / Tick / GroupId

namespace Botanik { namespace Terminal {

    // present() tops the ring back up to TX_COLD_TARGET_MS of wire time however
    // often it runs, so what has to stay under that depth is the FLUSH rate, not
    // the DRAW rate: the wire only idles if the ring drains empty between two
    // flushes. Splitting the two is what buys the draw its freedom - it is
    // GROUP_SLOW against a GROUP_FAST flush, and could be slower still.
    static_assert(Config::GROUP_FAST_MS < Config::TX_COLD_TARGET_MS,
                  "the console's flush period must stay under TX_COLD_TARGET_MS, "
                  "or the TX ring drains between flushes and the link idles");

    // Lifecycle

    Console::Console(Utils::BufferedSerial& serial, uint16_t cols, uint16_t rows,
                     const uint64_t* timestamp_ms, uint32_t baud,
                     const Scheduler::SchedulerContext* sched,
                     const Alarms::AlarmController* alarms,
                     Logs::Logger* logger, Ansi::TerminalType type) :
        Component(logger, "Console"),
        _serial(serial),
        _term(serial, cols, rows, baud, type),
        _parser(serial, timestamp_ms),
        _alarms(alarms),
        _status_bar(*this, sched),
        _alarm_bar(*this)
    {
        // bottom row reserved for the alarm bar only when alarms are wired
        _reserved_bottom = (_alarms != nullptr) ? 1 : 0;
    }

    // The beat
    // ------------------------------------------------------------------------

    void Console::_input_task(Component* c, const Tasks::Tick&) {
        static_cast<Console*>(c)->_take_input();
    }
    void Console::_draw_task(Component* c, const Tasks::Tick& t) {
        static_cast<Console*>(c)->_draw(t.now_ms);
    }
    void Console::_flush_task(Component* c, const Tasks::Tick&) {
        static_cast<Console*>(c)->_flush();
    }

    void Console::register_tasks(Tasks::Registry& r) {
        // The port's own two tasks are gone: the console pumps the wire it
        // owns, at the two ends of the beat it was already working at.
        r.add(*this, &Console::_input_task, "console_in", Tasks::GROUP_FAST,
              Tasks::PHASE_INPUT);
        r.add(*this, &Console::_draw_task,  "console",    Tasks::GROUP_SLOW,
              Tasks::PHASE_PRESENT);
        r.add(*this, &Console::_flush_task, "console_tx", Tasks::GROUP_FAST,
              Tasks::PHASE_PRESENT);
    }

    void Console::_take_input() {
        _serial.receive();
        // Nothing is routed before the screen exists. _draw() below clears it
        // and resets both grids on its first run, so anything a page painted on
        // the way past would be wiped with nothing left to repaint it. The bytes
        // wait in the ring; at 4800 baud it takes 585 ms to fill.
        if (_term.screen_ready()) _pump_input();
    }

    void Console::_flush() {
        _term.present();
        _serial.transmit();
    }

    void Console::_draw(uint64_t now_ms) {
        // Establish the screen on the first step (not in setup_*()) so the clear
        // lands after the handshake setup_*() queued (establish_screen(), not
        // clear_screen, so it keeps the queued handshake bytes).
        if (!_term.screen_ready()) {
            _term.establish_screen();
            // Chrome first, then the page: if a paced frame only gets partway,
            // the frame lands before what it frames.
            _status_bar.draw_row();
            _alarm_bar.mark_dirty();      // the clear wiped the alarm-bar row
            _alarm_bar.step();
            if (_pages_len > 0 && _pages[_page_index] != nullptr) {
                _reset_content_priority();
                _pages[_page_index]->focus();
            }
        }
        _status_bar.step(now_ms);
        // Tick every page, not just the active one: each is its own LogWriter and
        // must drain into its scrollback every step so a later focus() replays an
        // up-to-date tail. Inactive pages don't render (gated on their active flag).
        for (uint8_t i = 0; i < _pages_len; i++)
            if (_pages[i] != nullptr) _pages[i]->tick(i == _page_index);
        // Last: top up the bottom alarm bar over whatever the active page drew.
        _alarm_bar.step();
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
            // Alt+0 is the tenth page by the usual convention; switch_to()
            // ignores it while fewer are registered.
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
        // clear_rows scrolls the band off by its full height: one escape, and it
        // shifts BACK too, so the diff then emits nothing. (An erase-to-end-of-
        // display would only blank FRONT, leaving the diff to write a space over
        // every non-blank cell - and would wipe a reserved alarm bar besides.)
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

    const char* Console::page_name(uint8_t i) const {
        return (i < _pages_len && _pages[i] != nullptr) ? _pages[i]->name() : nullptr;
    }

    int8_t Console::add_page(Pages::Page* page) {
        if (page == nullptr || _pages_len >= MAX_PAGES) return -1;
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
        // Both bounds: _pages_len is what is registered, MAX_PAGES what the
        // array holds. The compiler only knows the second one, and Alt+<digit>
        // can name a slot past it.
        if (index >= _pages_len || index >= MAX_PAGES ||
            _pages[index] == nullptr) return;
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
        // Clear only the content band: the row-0 status bar and the bottom alarm
        // bar survive, and the diff emits just the net change (no flicker).
        clear_content();
        // A switch reflows the whole strip (the active tab expands to "N:NAME"),
        // so repaint it as a unit.
        _status_bar.draw_tabs();
        // The alarm bar is NOT redrawn: clear_content() preserves its row, and
        // its step() repaints only when the alarm set moves.
        //
        // The whole content band is about to be redrawn, so send the diff's walk
        // back to the top, or the new page paints from wherever the outgoing
        // page's budget ran out and fills in above it afterwards.
        _term.restart_scan();
        _reset_content_priority();
        _pages[_page_index]->focus();
    }

    void Console::_reset_content_priority() {
        // Hand the content band back as plain structure on a switch; the incoming
        // page declares its own in focus(). Otherwise the outgoing page's classes
        // (the shell editor's rows) outrank the new page's content for good.
        _term.set_row_priority(1, _content_bottom(), Terminal::PRIO_STRUCT);
    }

}}
