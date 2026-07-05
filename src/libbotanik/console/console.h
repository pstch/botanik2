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

#include <cstdint>

#include "libbotanik/utils/serial.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/console/parser.h"
#include "libbotanik/console/status_bar.h"
#include "libbotanik/console/alarm_bar.h"
#include "libbotanik/core/context.h"
#include "libbotanik/core/component.h"      // Component (first-step announce)
#include "libbotanik/logs/logger.h"         // Logs::Logger
#include "libbotanik/pages/page.h"

namespace Botanik { namespace Alarms { class AlarmModel; } }

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
        /** @brief Child pages multiplexed onto the display. */
        Pages::Page*        _pages[8];
        uint8_t             _pages_len  = 0;
        uint8_t             _page_index = 0;

        /** @}
         *  @name External references
         *  @{ */

        /** @brief Subsystem status contexts for the status bar and `stats`
         *  (scheduler + PLC; members may be null). */
        SystemContext                _ctx    = {};
        /** @brief Alarm source feeding the bar (may be null). When null there is
         *  no bar, _reserved_bottom stays 0, and the bottom row belongs to pages. */
        const Alarms::AlarmModel*    _alarms = nullptr;
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
                const uint32_t* timestamp_ms, uint32_t baud,
                const SystemContext& ctx = {},
                const Alarms::AlarmModel* alarms = nullptr,
                Logs::Logger* logger = nullptr);

        /** @brief The VT device: renderers (pages, editor, bars) paint through
         *  this reference. */
        AnsiTerminal&       terminal()       { return _term; }
        const AnsiTerminal& terminal() const { return _term; }

        /** @brief Select the terminal type (forwarded to the device); call once
         *  before the first render. */
        void setup_minitel() { _term.setup_minitel(); }
        void setup_vt102()   { _term.setup_vt102();   }

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
         *  range (lets the status bar iterate tabs without touching _pages). */
        const char* page_name(uint8_t i) const {
            return (i < _pages_len && _pages[i] != nullptr) ? _pages[i]->name() : nullptr;
        }

        /** @}
         *  @name Context access
         *  @{ */

        /** @brief Access the attached subsystem status contexts (members may be
         *  null). */
        const SystemContext& context() const { return _ctx; }

        /** @brief Replace the attached status contexts (copied by value). Lets the
         *  owner wire the aggregate after construction. */
        void set_context(const SystemContext& ctx) { _ctx = ctx; }

        /** @brief The attached alarm model, or null when none was supplied
         *  (drives the bottom alarm bar; also read by the shell's "alarms"
         *  command). */
        const Alarms::AlarmModel* alarms() const { return _alarms; }

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
