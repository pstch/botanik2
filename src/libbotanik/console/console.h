// REVIEW_35: TODO
#ifndef CONSOLE_CONSOLE_H
#define CONSOLE_CONSOLE_H

/**
 * @file console.h
 * @brief Console - the multiplexed page display driven over an AnsiTerminal:
 *        routes input to the active page, owns the row-0 status bar and the
 *        bottom alarm bar, reserves the content area, drives step().
 * @note Renderers reach the device through terminal(). See docs/dev/pages.md.
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
namespace Botanik { namespace Vars  { class Bank;   } }
// Held by pointer only, every call on one in console.cpp: forward-declared so
// pages/ does not land in everything that draws.
namespace Botanik { namespace Pages { class Page; } }

namespace Botanik { namespace Terminal {

    /** @class Console
     *  @brief Multiplexed page display over an AnsiTerminal (see file header). */
    class Console : public Botanik::Component {
        using BufferedSerial = Utils::BufferedSerial;
        using CursorPos = Terminal::Ansi::CursorPos;

        /** @name Device
         *  @{ */

        /** @brief The console UART, borrowed. This console is what pumps it:
         *  the bytes in are the first half of taking input and the bytes out
         *  the last half of putting a frame on the wire, so both belong to this
         *  component's own tasks rather than to two of the port's. */
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
        Pages::Page*        _pages[MAX_PAGES];
        uint8_t             _pages_len  = 0;
        uint8_t             _page_index = 0;

        /** @}
         *  @name Its own configuration
         *
         *  The console reads the `console_` group and its baud ITSELF - those
         *  rows describe nothing but this object. What is held here is what has
         *  been APPLIED, so the config task acts on an edge.
         *  @{ */

        /** @brief Where the settings live; null leaves the console at whatever
         *  it was constructed with, which is what a site that omitted the
         *  library splice gets. */
        const Vars::Bank*   _cfg = nullptr;
        uint32_t            _baud;
        uint16_t            _cols;
        uint16_t            _rows_cfg;
        uint8_t             _type;
        /** @brief Last `console_serial` applied, so a swap lands on the COMMIT and
         *  not on each of the three edits behind it. 0 is not a valid serial
         *  (the row's default is 1), so the first pass always applies - which is
         *  what picks the stored geometry up at bring-up. */
        uint32_t            _cfg_serial = 0;

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
        Console(const SetupContext& ctx, Utils::BufferedSerial& serial,
                uint16_t cols, uint16_t rows,
                const uint64_t* timestamp_ms, uint32_t baud,
                const Scheduler::SchedulerContext* sched = nullptr,
                const Alarms::AlarmController* alarms = nullptr,
                Ansi::TerminalType type = Ansi::CONFIGURED_TERMINAL,
                const Vars::Bank* cfg = nullptr);

        /** @brief Four points in the beat: its own configuration (CONFIG), the
         *  bytes in and the input they carry (INPUT), the frame drawn (PRESENT),
         *  and the frame plus the ring pushed out (PRESENT, every beat - see the
         *  flush note in console.cpp). */
        void register_tasks() override;

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

        /** @brief Apply whatever the store holds, without announcing it.
         *  @warning AFTER the parameter load. Called before, it records the
         *  DEFAULTS' commit serial as applied, and since a load restores that
         *  serial unchanged the group would never edge again. */
        void apply_stored_config();

        /** @brief Re-declare which terminal is on the wire and how big it is,
         *  and put the frame back on the screen it just blanked. What the
         *  `console_` group commits to, driven from PHASE_CONFIG.
         *  @note Everything reads rows()/cols() at render, so this repaint IS
         *  the reflow - no page caches geometry.
         *  @return true when something moved; a no-op call emits nothing. */
        bool reconfigure(Ansi::TerminalType type, uint16_t cols, uint16_t rows);

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
         *  Out of line: the only thing here needing Pages::Page complete. */
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

        static void _config_task(Component* c, const Tasks::Tick& t);

        static void _input_task(Component* c, const Tasks::Tick& t);
        static void _draw_task(Component* c, const Tasks::Tick& t);
        static void _flush_task(Component* c, const Tasks::Tick& t);

        /** @}
         *  @name The three points in the beat
         *  @{ */

        /** @brief Re-read the `console_` group and the console's baud, and apply
         *  them if either moved (PHASE_CONFIG).
         *  @param announce false at bring-up: applying what the store already
         *         holds is not a change anyone needs telling about. */
        void _apply_config(bool announce = true);

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
         *  Home/Alt-N/Tab switches, Ctrl-C) first. */
        void _pump_input();

        /** @brief Hand the content band back to the incoming page as plain
         *  structure (see the definition). */
        void _reset_content_priority();

        /** @brief Redraw the whole frame onto a screen the device has just
         *  blanked: chrome, then the active page's focus(). Shared by the
         *  first-step establish and reconfigure(), which owe the identical
         *  repaint for the identical reason. */
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
