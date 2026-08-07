// REVIEW_35: TODO
#ifndef CONSOLE_CONSOLE_H
#define CONSOLE_CONSOLE_H

/**
 * @file console.h
 * @brief Console - the multiplexed page display driven over an AnsiTerminal.
 *
 * The high-level half of the console: it composes an AnsiTerminal device
 * (terminal.h), routes input events to the active page, owns the row-0 status
 * bar and the bottom alarm bar, reserves the content area, and drives the
 * per-tick step() order. Renderers reach the device through terminal();
 * content-area geometry (rows()), the shared SystemContext (context()), and the
 * alarm model (alarms()) live here.
 */

#include <cstdint>                          // int8_t/uint16_t/uint64_t

#include "libbotanik/console/alarm_bar.h"   // AlarmBar
#include "libbotanik/console/parser.h"      // InputParser
#include "libbotanik/console/status_bar.h"  // StatusBar
#include "libbotanik/console/terminal.h"    // AnsiTerminal
#include "libbotanik/core/component.h"      // Component
#include "libbotanik/logs/logger.h"         // Logs::Logger
#include "libbotanik/utils/serial.h"        // Utils::BufferedSerial

namespace Botanik { namespace Alarms { class AlarmController; } }
namespace Botanik { namespace Scheduler { struct SchedulerContext; } }
// Held by pointer only, and every call on one lives in console.cpp - so the page
// interface is forward-declared rather than included. A page multiplexer knowing
// the page interface is fine; the whole of pages/ landing in everything that
// draws is not.
namespace Botanik { namespace Pages { class Page; } }

namespace Botanik { namespace Terminal {

    /** @class Console
     *  @brief Multiplexed page display over an AnsiTerminal (see file header). */
    class Console : public Botanik::Component {
        using BufferedSerial = Utils::BufferedSerial;
        using CursorPos = Terminal::Ansi::CursorPos;

        /** @name Device
         *  @{ */

        /** @brief The VT device this console paints through. */
        AnsiTerminal        _term;

        /** @}
         *  @name Input & pages
         *  @{ */

        InputParser         _parser;
        /** @brief Child pages multiplexed onto the display. Alt+<digit> selects
         *  one, so the tail digits only bind once this many pages exist. */
        static constexpr uint8_t MAX_PAGES = Config::CONSOLE_PAGES_MAX;
        static_assert(MAX_PAGES <= 10, "Alt+<digit> binds ten pages at most");
        Pages::Page*        _pages[MAX_PAGES];
        uint8_t             _pages_len  = 0;
        uint8_t             _page_index = 0;

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

        /** @brief Initialize the console. @p baud sizes the device's TX depth
         *  floor. @p alarms, when non-null, feeds the always-visible bottom alarm
         *  bar and reserves the bottom screen row for it (the content area shrinks
         *  by one row); null leaves the full screen to pages. */
        Console(Utils::BufferedSerial& serial, uint16_t cols, uint16_t rows,
                const uint64_t* timestamp_ms, uint32_t baud,
                const Scheduler::SchedulerContext* sched = nullptr,
                const Alarms::AlarmController* alarms = nullptr,
                Logs::Logger* logger = nullptr,
                Ansi::TerminalType type = Ansi::CONFIGURED_TERMINAL);

        /** @brief The VT device: renderers (pages, editor, bars) paint through
         *  this reference. */
        AnsiTerminal&       terminal()       { return _term; }
        const AnsiTerminal& terminal() const { return _term; }

        /** @brief Emit the terminal's setup handshake (forwarded to the
         *  device); call once before the first render. */
        void setup_terminal() { _term.setup_terminal(); }

        /** @}
         *  @name Content-area geometry & scrolling
         *  @{ */

        /** @brief Rows available to pages: physical height minus the reserved
         *  bottom alarm bar (if any). Row 0 is the status bar and this count
         *  includes it, so the page content area is rows 1 .. rows()-1. Pages and
         *  the editor size their layout and scroll bands off this. */
        uint16_t rows() const {
            return (uint16_t)(_term.rows_total() - _reserved_bottom);
        }

        /** @brief Erase the page content area (rows 1..N-1), keeping the status
         *  bar (and the alarm bar, when reserved). */
        void clear_content();

        /** @brief Emit a line feed within the content band, scrolling the band up
         *  when at its bottom so row 0 (status) and the reserved bottom row are
         *  never touched. */
        void line_feed(bool carriage_return = true);

        /** @}
         *  @name Pages
         *  @{ */

        /** @brief Register a page, appended in tab order; returns its index, or
         *  -1 if the slot array is full. Order of registration IS the tab order. */
        int8_t add_page(Pages::Page* page);

        /** @brief Switch to the page at the given index. */
        void switch_to(uint8_t index);

        /** @brief Switch to the next (@p forward) or previous page, wrapping. */
        void switch_relative(bool forward);

        /** @brief Number of registered pages (the status-bar tab count). */
        uint8_t page_count() const { return _pages_len; }

        /** @brief Index of the active page. */
        uint8_t active_page() const { return _page_index; }

        /** @brief Name of page @p i, or null when the slot is empty / out of
         *  range (lets the status bar iterate tabs without touching _pages).
         *  Out of line: it is the only thing here that needs Pages::Page to be
         *  complete, and keeping it in the .cpp is what lets this header get by
         *  with a forward declaration. */
        const char* page_name(uint8_t i) const;

        /** @}
         *  @name Context access
         *  @{ */

        /** @brief The attached alarm model, or null when none was supplied
         *  (drives the bottom alarm bar). */
        const Alarms::AlarmController* alarms() const { return _alarms; }

        /** @} */

    protected:
        /** @brief Process input and update the display, once per beat (driven by
         *  Component::step with the cycle timestamp). */
        void on_step(uint64_t now_ms) override;

    private:
        /** @name Internals
         *  @{ */

        /** @brief Drain up to INPUT_EVENTS_PER_TICK parsed events into the active
         *  page, consuming the terminal-level intercepts (Ctrl-L redraw,
         *  Home/Alt-N/Tab switches, Ctrl-C) first. */
        void _pump_input();

        /** @brief Hand the content band back to the incoming page as plain
         *  structure (see the definition). */
        void _reset_content_priority();

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
