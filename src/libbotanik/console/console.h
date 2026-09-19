#ifndef CONSOLE_CONSOLE_H
#define CONSOLE_CONSOLE_H

/**
 * @file console.h
 * @brief Console - the multiplexed page display driven over an AnsiTerminal:
 *        routes input to the active page, owns the row-0 status bar and the
 *        bottom alarm bar, reserves the content area, registers the input,
 *        draw and flush tasks.
 * @note Renderers reach the device through terminal(). See @ref dev_pages.
 */

#include <cstdint>

#include "libbotanik/console/alarm_bar.h"
#include "libbotanik/console/kill_ring.h"
#include "libbotanik/console/parser.h"
#include "libbotanik/console/status_bar.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/core/component.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/utils/serial.h"

namespace Botanik { namespace Alarms { class AlarmController; } }
namespace Botanik { namespace Scheduler { struct SchedulerContext; } }
namespace Botanik { namespace Tasks { struct Tick; } }
// Held by pointer only, every call on one in console.cpp: forward-declared so
// pages/ does not land in everything that draws.
namespace Botanik { namespace Terminal { class Page; } }

namespace Botanik { namespace Terminal {

    /** @class Console
     *  @brief Multiplexed page display over an AnsiTerminal (see file header). */
    class Console : public Botanik::Component {
        using BufferedSerial = Utils::BufferedSerial;
        using CursorPos = Terminal::Ansi::CursorPos;

        /** @name Device
         *  @{ */

        /** @brief The console UART, borrowed. This console pumps it, in its
         *  own tasks rather than the port's. */
        BufferedSerial&     _serial;
        /** @brief The VT device this console paints through. */
        AnsiTerminal        _term;
        /** @brief The cut buffer every editor and table on this console shares.
         *  Owned here, not global: one console per firmware, so it is the same
         *  single clipboard without being reachable from any header. */
        KillRing            _kill_ring;

        /** @}
         *  @name Input & pages
         *  @{ */

        InputParser         _parser;
        /** @brief Child pages multiplexed onto the display. Alt+<digit> selects
         *  one, so the tail digits only bind once this many pages exist. */
        static constexpr uint8_t MAX_PAGES = Config::CONSOLE_PAGES_MAX;
        static_assert(MAX_PAGES <= 10, "Alt+<digit> binds ten pages at most");
        Terminal::Page*        _pages[MAX_PAGES];
        uint8_t             _pages_len  = 0;
        uint8_t             _page_index = 0;
        /** @brief Input reached the active page since it last ticked. */
        bool                _input_routed = false;

        /** @}
         *  @name Its own configuration
         *
         *  What has been APPLIED, read back from the device: the terminal
         *  clamps to its own buffer. Pushed in, never looked up.
         *  @{ */

        uint32_t            _baud;
        uint16_t            _cols;
        uint16_t            _rows_cfg;
        uint8_t             _type;

        /** @}
         *  @name External references
         *  @{ */

        /** @brief Alarm source feeding the bar (may be null). When null there is
         *  no bar, _reserved_bottom stays 0, and the bottom row belongs to pages. */
        const Alarms::AlarmController*    _alarms = nullptr;
        /** @brief Rows reserved at the screen bottom for the alarm bar: 1 when
         *  _alarms is set, else 0. Subtracted by rows() / _content_bottom(). */
        uint16_t                     _reserved_bottom = 0;

        /** @}
         *  @name Bars
         *  @{ */

        /** @brief Row-0 chrome (page tabs, clock, metrics). */
        StatusBar           _status_bar;
        /** @brief Bottom-row alarm strip. */
        AlarmBar            _alarm_bar;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief The wire and the clock. Geometry, rate and make are NOT
         *  arguments: they arrive through set_config(). Until they do this is
         *  one usable cell, which is what a console nobody has configured is. */
        Console(const SetupContext& ctx, Utils::BufferedSerial& serial,
                const uint64_t* timestamp_ms,
                const Scheduler::SchedulerContext* sched = nullptr,
                const Alarms::AlarmController* alarms = nullptr);

        /** @brief Three points in the beat: bytes in and the input they carry
         *  (INPUT), the frame drawn, and the frame plus the ring pushed out
         *  (both PRESENT). */
        void register_tasks() override;

        /** @brief Paint the alarm bar onto a screen just blanked, claiming its
         *  row again. A fixture painting a page without the beat calls it, so
         *  it measures against the frame the field has. */
        void repaint_alarm_bar();

        /** @brief The VT device: renderers (pages, editor, bars) paint through
         *  this reference. */
        AnsiTerminal&       terminal()       { return _term; }
        const AnsiTerminal& terminal() const { return _term; }

        /** @brief The console's cut buffer: a line cut at the shell prompt yanks
         *  into NOTES, and Alt+W on a table copies into the same place. */
        KillRing&           kill_ring()       { return _kill_ring; }
        const KillRing&     kill_ring() const { return _kill_ring; }

        /** @brief Emit the terminal's setup handshake (forwarded to the
         *  device); call once before the first render. */
        void setup_terminal() { _term.setup_terminal(); }

        /** @brief Adopt @p cfg: retune the wire and the TX budget together,
         *  then re-declare the screen. Safe to call with the config already in
         *  force - nothing moves and nothing is emitted.
         *  @param announce log the change at NOTICE; off for bring-up, when
         *         there is no change to report and no sink to report it to. */
        void set_config(const ConsoleConfig& cfg, bool announce = true);

        /** @brief The rate the wire is running at, as last applied. */
        uint32_t baud() const { return _baud; }

        /** @brief Re-declare which terminal is on the wire and how big it is,
         *  and put the frame back on the screen it just blanked. What the
         *  `console_` group commits to, driven from PHASE_CONFIG.
         *  @note Everything reads rows()/cols() at render, so this repaint IS
         *  the reflow. Scrollback is the exception and has a reflow() of its
         *  own. @return true when something moved. */
        bool reconfigure(Ansi::TerminalType type, uint16_t cols, uint16_t rows);

        /** @}
         *  @name Content-area geometry
         *  @{ */

        /** @brief Rows available to pages: physical height minus the reserved
         *  bottom alarm bar. Row 0 is the status bar and is INCLUDED, so a
         *  page's content area is rows 1 .. rows()-1. */
        uint16_t rows() const {
            return (uint16_t)(_term.rows_total() - _reserved_bottom);
        }

        /** @brief Erase the page content area (rows 1..N-1), keeping the status
         *  bar (and the alarm bar, when reserved). */
        void clear_content();

        /** @}
         *  @name Pages
         *  @{ */

        /** @brief Register a page, appended in tab order; returns its index, or
         *  -1 if the slot array is full. Order of registration IS the tab order. */
        int8_t add_page(Terminal::Page* page);

        /** @brief Switch to the page at the given index. */
        void switch_to(uint8_t index);

        /** @brief Number of registered pages (the status-bar tab count). */
        uint8_t page_count() const { return _pages_len; }

        /** @brief Index of the active page. */
        uint8_t active_page() const { return _page_index; }

        /** @brief Name of page @p i, or null when the slot is empty / out of
         *  range (lets the status bar iterate tabs without touching _pages).
         *  Out of line: the only thing here needing Terminal::Page complete. */
        const char* page_name(uint8_t i) const;

        /** @}
         *  @name Context access
         *  @{ */

        /** @brief The attached alarm model, or null when none was supplied
         *  (drives the bottom alarm bar). */
        const Alarms::AlarmController* alarms() const { return _alarms; }

        /** @} */

    private:
        /** @name Registration handlers
         *  Static MEMBERS, so they reach the private methods below without a
         *  friend declaration. @{ */

        static void _input_task(Component* c, const Tasks::Tick& t);
        static void _draw_task(Component* c, const Tasks::Tick& t);
        static void _flush_task(Component* c, const Tasks::Tick& t);

        /** @}
         *  @name The three points in the beat
         *  @{ */

        /** @brief Take the wire's bytes in and route what they decode to
         *  (PHASE_INPUT). */
        void _take_input();
        /** @brief Repaint the chrome and tick every page (PHASE_PRESENT). Draws
         *  into the terminal's FRONT grid; nothing reaches the wire here. */
        void _draw(uint64_t now_ms);
        /** @brief Reconcile FRONT to the wire and push the ring out
         *  (PHASE_PRESENT, after _draw). */
        void _flush();

        /** @}
         *  @name Internals
         *  @{ */

        /** @brief Drain up to INPUT_EVENTS_PER_TICK parsed events into the active
         *  page, consuming the terminal-level intercepts (Ctrl-L redraw,
         *  Alt-N/Tab switches, Ctrl-C) first. */
        void _pump_input();

        /** @brief Switch to the next (@p forward) or previous page, wrapping. */
        void switch_relative(bool forward);

        /** @brief Hand the content band back to the incoming page as plain
         *  structure (see the definition). */
        void _reset_content_priority();

        /** @brief Redraw the whole frame onto a screen the device has just
         *  blanked: chrome, then the active page's focus(). Shared by the
         *  first-step establish and reconfigure(). */
        void _repaint_frame();

        /** @brief Last screen row a page may scroll/draw content into (0-indexed):
         *  physical last row minus the reserved bottom bar minus row 0. */
        uint16_t _content_bottom() const {
            uint16_t below = (uint16_t)(_reserved_bottom + 1);
            uint16_t rows  = _term.rows_total();
            return (uint16_t)(rows > below ? rows - below : 0);
        }

        /** @} */
    };

}}

#endif // CONSOLE_CONSOLE_H
