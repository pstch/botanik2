// REVIEW_35: TODO
#ifndef PAGES_ALARMS_H
#define PAGES_ALARMS_H

/**
 * @file alarms.h
 * @brief AlarmsPage - scrollable bordered table of alarm conditions.
 *
 * Layout (row 0 is the terminal status bar):
 *   row 1            blank top margin
 *   row 2            table header  (| CODE | VARIABLE | ON | OVR | LAST EVENT |)
 *   row 3            top rule (+----+...+); its corners cap to '^' when more above
 *   rows 4..N-5      data rows
 *   row N-4          bottom rule; corners cap to 'v' when more below
 *   row N-3          blank row above the legend
 *   row N-2          footer legend: reverse-video mode badge, counts, filter
 *   row N-1          blank row below the legend
 *
 * Typing narrows the table to the conditions whose variable name holds what was
 * typed; ENTER toggles the selected one's acknowledgement.
 *
 * @note Rows are ordered active-first then by most-recent onset. The "all"
 *       view is the exception, keeping TABLE order so rows never reorder as
 *       alarms toggle.
 */

#include <cstdint>

#include "libbotanik/config/alarms.h"
#include "libbotanik/config/terminal.h"

#include "libbotanik/alarms/alarms.h"
#include "libbotanik/console/console.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/pages/page.h"
#include "libbotanik/pages/scroll_table.h"
#include "libbotanik/utils/time.h"

namespace Botanik { namespace Pages {

    /** @brief Full-screen scrollable alarm table (see file header). */
    class AlarmsPage : public ScrollTablePage {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Console      = Terminal::Console;
        using Command      = Terminal::InputEvent::Command;

        /** @name State
         *  @{ */

        /** @brief Alarm source (may be null). Non-const: ENTER acks the
         *  selected condition through it. */
        Alarms::AlarmController*       _model;
        /** @brief A nav / view / filter / ack happened: redraw on the next tick
         *  rather than waiting for the value cadence, so the keypress feels
         *  immediate. */
        bool                      _dirty = true;
        /** @brief Uptime of the last cadence rebuild (see tick()). */
        uint64_t                  _last_refresh_ms = 0;
        /** @brief Visible alarm indices (table order), filled by _collect(). */
        uint8_t                   _vis[Config::ALARM_SLOTS];
        /** @brief Number of visible entries in _vis. */
        uint8_t                   _nvis     = 0;

        /** @} */

        /** @name Views
         *  The base's view-mode machinery (scroll_table.h): SPACE / RIGHT step
         *  forward, LEFT steps back. Independent of the row filter, which
         *  narrows whichever view is showing.
         *  @{ */
        static constexpr uint8_t MODE_RECENT   = 0;  // last event, within window
        static constexpr uint8_t MODE_ALL      = 1;  // every defined alarm
        static constexpr uint8_t MODE_ACTIVE   = 2;  // active (masked or not)
        static constexpr uint8_t MODE_UNMASKED = 3;  // active and not masked
        static constexpr uint8_t MODE_COUNT    = 4;

        uint8_t _mode_count() const override { return MODE_COUNT; }
        const char* _mode_label(uint8_t i) const override;
        /** @brief A new view shows a different set, so re-collect and repaint. */
        void _on_mode_changed() override { _collect(); _dirty = true; }
        /** @brief Same, for the typed filter. */
        void _on_filter_changed() override { _collect(); _dirty = true; }
        /** @} */

        /** @name Layout
         *  Columns: CODE | VARIABLE (the selection cursor's cell) | ON
         *  (reverse-video badge when active) | OVR | LAST EVENT (the last
         *  transition's timestamp, "YY/MM/DD HH:MM").
         *  @{ */
        static constexpr uint16_t CODE_W     = 9;   // short display code
        static constexpr uint16_t STATE_W    = 3;   // "ON" badge / "ON" header
        /** @brief The override marker: what the row is being HELD at, in the
         *  state column's own ON/OFF vocabulary (that is what the state column
         *  shows while forced). Blank when live. Without it a hand-forced alarm
         *  is indistinguishable from a real one during a fault. */
        static constexpr uint16_t OVR_W      = 4;   // "ON" / "OFF", padded
        static constexpr uint16_t EVENT_W    = 15;  // "YY/MM/DD HH:MM"
        static constexpr uint16_t HEADER_ROW = 2;
        static constexpr uint16_t RULE_ROW   = 3;
        static constexpr uint16_t DATA_TOP   = 4;
        /** @brief Column order. The selection cursor inverts the VARIABLE cell -
         *  the row's identity, and what Alt+W copies - stopping one column short
         *  of the cell's right edge.
         *  @note The STATE cell's "ON" badge is split out of the row write, so
         *  there is no flicker-prone overdraw. */
        enum { C_CODE = 0, C_NAME, C_STATE, C_OVR, C_EVENT };
        /** @} */

        /** @brief The alarms grid packs its cells (no trailing pad): the width
         *  it saves goes to the flexible VARIABLE column. */
        bool _pad_cells() const override { return false; }

        uint8_t _columns(Col* out) const override;
        /** @brief VARIABLE stretches: it absorbs what the fixed columns leave,
         *  so the table spans the display. */
        uint8_t _flex_col() const override { return C_NAME; }

        /** @brief Format the last-event cell: edge_ms as "YY/MM/DD HH:MM", or
         *  empty if it never fired (ON vs OFF is shown by the ON column). */
        static void _fmt_event(char* buf, size_t cap, const Alarms::AlarmCondition& c);


        /** @brief Draw the footer legend: the view badge and its neighbours, the
         *  position, the filter, and how many conditions are active. */
        void _draw_footer(uint16_t row, uint8_t active);

        /** @brief Detail panel between the bottom rule and the legend, a blank
         *  row each side: the selected alarm's Description, its Remediation
         *  (what to do while it is up) and its ack time. */
        void _draw_notes();

        /** @brief True if @p c belongs in RECENT mode: a last event still
         *  within its window. Active = always recent; inactive expires
         *  recent_s after it cleared (0 = Config::ALARM_RECENT_S, <0 = never). */
        bool _is_recent(const Alarms::AlarmCondition& c, uint64_t now) const;

        /** @brief Order two visible entries: unack'd-active first, then
         *  ack'd-active, then inactive; each by most-recent onset. An ack
         *  demotes without removing. */
        bool _before(const Alarms::AlarmCondition& x,
                     const Alarms::AlarmCondition& y) const {
            bool xn = x.active && !x.user_ack;     // active and unack'd
            bool yn = y.active && !y.user_ack;
            // unack'd-active heads list
            if (xn != yn) return xn;
            // then ack'd-active first
            if (x.active != y.active) return x.active;
            return x.started_ms > y.started_ms;        // then most-recent onset
        }

        /** @brief Fill _vis/_nvis with the indices the view and the filter both
         *  keep, ordered active-first then by most-recent onset. */
        void _collect();

        /** @name Window geometry
         *  Bottom-anchored. From the bottom up: free row, legend, blank,
         *  Acknowledged, Remediation, Description, blank, bottom rule, then the
         *  data band up to the top rule.
         *  @{ */
        // free row below at rows()-1
        uint16_t _legend_row()  const { return (uint16_t)(_console.rows() - 2); }
        uint16_t _brule_row()   const { return (uint16_t)(_console.rows() - 8); }
        /** @brief Number of data rows that fit, or 0 if the screen is too
         *  short. */
        uint16_t _window() const override {
            uint16_t r = _console.rows();
            return (r >= (uint16_t)(DATA_TOP + 9)) ? (uint16_t)(r - DATA_TOP - 8) : 0;
        }
        /** @name ScrollTablePage geometry
         *  @{ */
        uint16_t _data_top()  const override { return DATA_TOP; }
        uint16_t _row_count() const override { return _nvis; }
        const char* _selected_cell() const override;
        /** @} */
        /** @brief Entries scrolled off the bottom of the visible window. */
        uint16_t _hidden_below(uint16_t window) const {
            uint16_t end = (uint16_t)(_top + window);
            return (_nvis > end) ? (uint16_t)(_nvis - end) : 0;
        }
        /** @brief Redraw the top/bottom rules, capping with '^'/'v' when alarms
         *  are hidden above / below the window. */
        void _draw_rules(uint16_t window);
        /** @} */

        /** @brief Write @p line up to and including the VARIABLE cell, inverting
         *  [@p l, @p r) for the selection cursor. The caller fits the columns
         *  once and passes both edges, rather than each end refitting. */
        void _write_name_cell(const char* line, bool selected,
                              uint16_t l, uint16_t r);

        /** @brief Draw a full data row (CODE/VARIABLE/ON/OVR/LAST EVENT). Only
         *  the VARIABLE cell inverts for the selection cursor; the state cell
         *  shows its own reverse-video "ON" badge while active and unacked. */
        void _draw_full_row(uint16_t row, uint8_t idx,
                            const Alarms::AlarmCondition& c, bool selected);

        /** @brief Paint data row at screen @p row for visible entry @p vi: the
         *  full row when it holds an alarm, else a blanked row (past the set). */
        void _draw_data_row(uint16_t row, uint16_t vi);


        /** @brief Acknowledge the selected condition, or take the
         *  acknowledgement back: one key, both ways. */
        void _toggle_ack();

        /** @brief Draw the static chrome: top margin, header, and the two blank
         *  rows around the legend. */
        void _draw_structure();

    public:
        /** @brief Bind to a terminal and alarm model. */
        AlarmsPage(Console& console, Alarms::AlarmController* model)
            : ScrollTablePage(console), _model(model) {}

        /** @name Page interface
         *  @{ */

        const char* name() const override { return "ALARMS"; }

        void handle_text(char c) override;

        void handle_command(Command cmd, uint16_t = 1) override;

        void deactivate() override {}

        void focus() override;

        void tick(bool active) override;

        /** @} */
    };

}}

#endif // PAGES_ALARMS_H
