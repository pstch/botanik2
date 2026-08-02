// REVIEW_35: TODO
#ifndef PAGES_ALARMS_H
#define PAGES_ALARMS_H

/**
 * @file alarms.h
 * @brief AlarmsPage - scrollable bordered table of alarm conditions.
 *
 * Layout (row 0 is the terminal status bar):
 *   row 1            blank top margin
 *   row 2            table header  (| CODE | VARIABLE | ON | LAST EVENT |)
 *   row 3            top rule (+----+...+); its corners cap to '^' when more above
 *   rows 4..N-5      data rows
 *   row N-4          bottom rule; corners cap to 'v' when more below
 *   row N-3          blank row above the legend
 *   row N-2          footer legend: reverse-video mode badge + counts, SPACE hint
 *   row N-1          blank row below the legend
 *
 * Rows are ordered active-first then by most-recent onset (started_ms);
 * never-active alarms sit at the bottom. The exception is the "all" filter,
 * which keeps the fixed definition (table) order so rows never reorder as
 * alarms toggle. The top / bottom rules cap their
 * column marks with '^' / 'v' when alarms are hidden above / below the window.
 * SPACE cycles the filter: recent (default) -> all -> active -> active and not
 * masked. A condition's recent window defaults to one week
 * (AlarmCondition::recent_s). UP/DOWN move a selection cursor (drawn inverted),
 * auto-scrolling the window only when the cursor would leave it; PAGE_UP/DOWN
 * move it half a window at a time and ENTER jumps back to the top. `a` acknowledges
 * the selected condition. The set is re-checked once a second; a repaint is
 * paced a few rows per tick under the shared TX floor, so it interleaves with
 * the bars.
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../../libbotanik_config.h"
#include "../alarms/alarms.h"
#include "../hal/platform.h"
#include "page.h"
#include "scroll_table.h"
#include "../console/console.h"
#include "../console/terminal.h"
#include "../utils/time.h"

namespace Botanik { namespace Pages {

    /** @brief Full-screen scrollable alarm table (see file header). */
    class AlarmsPage : public ScrollTablePage {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Console      = Terminal::Console;
        using Command      = Terminal::InputEvent::Command;

        /** @name State
         *  @{ */

        /** @brief Owning console (content-area geometry). */
        Console&                  _console;
        /** @brief Alarm source (may be null). Non-const: the `a` key acks the
         *  selected condition through it. */
        Alarms::AlarmModel*       _model;
        /** @brief A nav / mode / ack happened: redraw on the next tick rather
         *  than waiting for the value cadence, so the keypress feels immediate. */
        bool                      _dirty = true;
        /** @brief Uptime of the last cadence rebuild (see tick()). */
        uint64_t                  _last_refresh_ms = 0;
        /** @brief Visible alarm indices (table order), filled by _collect(). */
        uint8_t                   _vis[Alarms::ALARM_BITS];
        /** @brief Number of visible entries in _vis. */
        uint8_t                   _nvis     = 0;

        /** @} */

        /** @name Filter modes
         *  The base's view-mode machinery (scroll_table.h): SPACE / RIGHT step
         *  forward, LEFT steps back.
         *  @{ */
        static constexpr uint8_t MODE_RECENT   = 0;  // last event, within window
        static constexpr uint8_t MODE_ALL      = 1;  // every defined alarm
        static constexpr uint8_t MODE_ACTIVE   = 2;  // active (masked or not)
        static constexpr uint8_t MODE_UNMASKED = 3;  // active and not masked
        static constexpr uint8_t MODE_COUNT    = 4;

        uint8_t _mode_count() const override { return MODE_COUNT; }
        const char* _mode_label(uint8_t i) const override {
            static const char* const labels[MODE_COUNT] =
                {"recent", "all", "active", "unmasked"};
            return i < MODE_COUNT ? labels[i] : "all";
        }
        /** @brief A new filter shows a different set, so re-collect and repaint. */
        void _on_mode_changed() override { _collect(); _dirty = true; }
        /** @} */

        /** @name Layout
         *  Columns: CODE | ON (reverse-video badge when active) | LAST EVENT
         *  (the last transition's timestamp, "YY/MM/DD HH:MM") | DESCRIPTION.
         *  @{ */
        static constexpr uint16_t CODE_W     = 9;   // short display code
        /** @brief The VARIABLE column is the flexible one: it absorbs whatever
         *  the fixed columns leave, so the table spans the display instead of
         *  stopping short of it. */
        uint16_t _name_w() const {
            return _flex_width((uint16_t)(2 * MARGIN + (CODE_W + 2) + 2
                                          + (STATE_W + 2) + (EVENT_W + 2) + 1), 60);
        }
        static constexpr uint16_t STATE_W    = 3;   // "ON" badge / "ON" header
        static constexpr uint16_t EVENT_W    = 15;  // "YY/MM/DD HH:MM"
        static constexpr uint16_t HEADER_ROW = 2;
        static constexpr uint16_t RULE_ROW   = 3;
        static constexpr uint16_t DATA_TOP   = 4;
        /** @brief Column order. The CODE cell is the only part inverted for the
         *  selection cursor, so a selection move patches one small field, not a
         *  whole row; the highlight stops one column short of its right edge
         *  (_code_cell_r()), leaving a plain gap before the border - which is
         *  safe because the highlight stops one short of the cell. The STATE cell carries
         *  its own reverse-video "ON" badge, split out of the row write so there
         *  is no flicker-prone overdraw. */
        enum { C_CODE = 0, C_NAME, C_STATE, C_EVENT };
        /** @} */

        /** @brief The alarms grid packs its cells (no trailing pad): the width
         *  it saves goes to the description column. */
        bool _pad_cells() const override { return false; }

        uint8_t _columns(Col* out) const override {
            // No DESCRIPTION column: the name IS the variable, so the row says
            // what it is, and the sentence explaining it belongs in the panel
            // where there is room for the whole of it.
            out[C_CODE]  = Col{ "CODE",       CODE_W  };
            out[C_NAME]  = Col{ "VARIABLE",   _name_w() };
            out[C_STATE] = Col{ "ON",         STATE_W };
            out[C_EVENT] = Col{ "LAST EVENT", EVENT_W };
            return 4;
        }

        /** @brief Right edge of the selection highlight: one short of the CODE
         *  cell's contents (see the column note above). */
        uint16_t _code_cell_r() const { return (uint16_t)(_cell_end(C_CODE) - 1); }

        /** @brief Format the last-event cell: edge_ms as "YY/MM/DD HH:MM", or
         *  empty if it never fired (ON vs OFF is shown by the ON column). */
        static void _fmt_event(char* buf, size_t cap, const Alarms::AlarmCondition& c) {
            if (!c.active && c.edge_ms == 0) { if (cap) buf[0] = '\0'; return; }
            Utils::CalendarTime t = Utils::cal_time_ms(c.edge_ms);
            snprintf(buf, cap, "%02u/%02u/%02u %02u:%02u",
                     (unsigned)(t.year % 100), (unsigned)t.month, (unsigned)t.day,
                     (unsigned)t.hour, (unsigned)t.minute);
        }


        /** @brief Draw the footer legend: reverse-video mode badge + counts on
         *  the left, SPACE-cycles-mode hint on the right. Drawn immediate-mode; the
         *  terminal's back-buffer diff repaints it only on an actual mode / count
         *  change. */
        void _draw_footer(uint16_t row, uint8_t active) {
            char detail[32];
            snprintf(detail, sizeof(detail), "%u active   a:ack", (unsigned)active);
            // No group: the alarms table's views ARE its modes, so the badge and
            // its neighbours already say everything a group field would.
            _draw_mode_line(row, nullptr, detail);
        }

        /** @brief Detail panel between the bottom rule and the legend, spaced by
         *  a blank row each side: the selected alarm's Remediation (its notes -
         *  what to do while it is up) and the time it was acknowledged (if any). */
        void _draw_notes() {
            const Alarms::AlarmCondition* c =
                (_model != nullptr && _selected < _nvis)
                    ? &_model->condition(_vis[_selected]) : nullptr;
            char ackbuf[24];
            const char* ack = "-";
            if (c != nullptr && c->user_ack) {
                Utils::CalendarTime t = Utils::cal_time_ms(c->ack_ms);
                snprintf(ackbuf, sizeof(ackbuf), "%02u/%02u/%02u %02u:%02u",
                         (unsigned)(t.year % 100), (unsigned)t.month, (unsigned)t.day,
                         (unsigned)t.hour, (unsigned)t.minute);
                ack = ackbuf;
            }
            uint16_t br = _brule_row();
            _blank_line((uint16_t)(br + 1));
            // Description leads: it was a table column until the name became the
            // variable's own, which says what the row IS but not what it means.
            _panel_field((uint16_t)(br + 2), "Description",  c ? c->desc  : nullptr);
            _panel_field((uint16_t)(br + 3), "Remediation",  c ? c->notes : nullptr);
            _panel_field((uint16_t)(br + 4), "Acknowledged", ack);
        }

        /** @brief True if @p c belongs in RECENT mode: a last event still
         *  within its window. Active = always recent; inactive expires
         *  recent_s after it cleared (0 = DEFAULT_RECENT_S, <0 = never). */
        bool _is_recent(const Alarms::AlarmCondition& c, uint64_t now) const {
            if (c.edge_ms == 0) return false;     // never fired -> no event
            if (c.active)       return true;      // on -> always recent
            if (c.recent_s < 0) return true;      // never expires
            uint64_t window_ms = (uint64_t)(c.recent_s == 0 ? Alarms::DEFAULT_RECENT_S
                                                            : c.recent_s) * 1000u;
            return (now - c.edge_ms) <= window_ms;
        }

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

        /** @brief Fill _vis/_nvis with the indices shown in the current mode,
         *  ordered active-first then by most-recent onset. */
        void _collect() {
            _nvis = 0;
            if (_model == nullptr) return;
            uint64_t now = HAL::Clock::uptime_ms();
            for (uint8_t i = 0;
                 i < _model->count() && _nvis < Alarms::ALARM_BITS; i++) {
                const Alarms::AlarmCondition& c = _model->condition(i);
                if (c.name == nullptr) continue;             // undefined slot
                bool include;
                switch (_mode) {
                    case MODE_RECENT:   include = _is_recent(c, now);     break;
                    case MODE_ACTIVE:   include = c.active;               break;
                    // UNMASKED mirrors the alarm bar, so an ack'd alarm drops
                    // out of it too (the ALL / ACTIVE / RECENT views keep it,
                    // demoted by _before()). With masking removed this is the
                    // active-and-un-ack'd set.
                    case MODE_UNMASKED:
                        include = c.active && !c.user_ack; break;
                    default:            include = true;                   break;
                }
                if (include) _vis[_nvis++] = i;
            }
            // ALL mode lists every alarm in definition (table) order, so the
            // rows never reorder as alarms come and go - only the ON badge
            // changes in place. The other modes order active-first (_before()).
            if (_mode == MODE_ALL) { _clamp_selection(); return; }
            // Insertion sort by _before(): runs once per (gated) repaint,
            // _nvis is nearly always tiny, so it beats a general sort and is
            // stable in table order on ties.
            for (uint8_t a = 1; a < _nvis; a++) {
                uint8_t v = _vis[a];
                const Alarms::AlarmCondition& cv = _model->condition(v);
                int b = (int)a - 1;
                while (b >= 0 && _before(cv, _model->condition(_vis[b]))) {
                    _vis[b + 1] = _vis[b];
                    b--;
                }
                _vis[b + 1] = v;
            }
            _clamp_selection();
        }

        /** @name Window geometry
         *  Bottom-anchored on the content area. From the bottom up: free row,
         *  legend, blank, Acknowledged line, Remediation line, blank, bottom rule, then
         *  the data band up to the top rule. The rules carry the scroll arrows
         *  (see _draw_rules()).
         *  @{ */
        // free row below at rows()-1
        uint16_t _legend_row()  const { return (uint16_t)(_console.rows() - 2); }
        // bottom rule; between it and the legend: blank, Remediation, Acknowledged, blank
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
        /** @} */
        /** @brief Entries scrolled off the bottom of the visible window. */
        uint16_t _hidden_below(uint16_t window) const {
            uint16_t end = (uint16_t)(_top + window);
            return (_nvis > end) ? (uint16_t)(_nvis - end) : 0;
        }
        /** @brief Redraw the top/bottom rules, capping with '^'/'v' when alarms
         *  are hidden above / below the window. */
        void _draw_rules(uint16_t window) {
            // The alarms rules carry the arrow on EVERY column mark, not just
            // the outer caps - hence the join argument.
            char top = _top ? '^' : '+';
            char bot = _hidden_below(window) ? 'v' : '+';
            _draw_rule(RULE_ROW,     top, top);
            _draw_rule(_brule_row(), bot, bot);
        }
        /** @} */

        /** @brief Write the margin + CODE cell of @p line, inverting the CODE field
         *  for the selection cursor (only the first cell highlights). Leaves the
         *  cursor at CODE_CELL_R. */
        void _write_code_cell(const char* line, bool selected) {
            const uint16_t l = _cell_col(C_CODE), r = _code_cell_r();
            _terminal.write_chars(line, l);                        // margin + "| "
            if (selected) _terminal.set_inverted(true);
            _terminal.write_chars(line + l, (uint16_t)(r - l));
            if (selected) _terminal.set_inverted(false);
        }

        /** @brief Draw a full data row (CODE/ON/EVENT/DESCRIPTION) immediate-mode.
         *  Only the CODE cell inverts for the selection cursor; the state cell shows
         *  its own reverse-video "ON" badge while active and unacked. The terminal's
         *  back-buffer diff pares this to the cells that actually moved. */
        void _draw_full_row(uint16_t row, const Alarms::AlarmCondition& c, bool selected) {
            // 15 for "YY/MM/DD HH:MM" + NUL, rounded up to what a field that
            // decoded oddly would need: the cell clips to EVENT_W either way,
            // and a truncating snprintf is not something to leave to chance.
            char ev[24];
            _fmt_event(ev, sizeof(ev), c);
            const char* state = c.active ? "ON" : (c.edge_ms != 0 ? "OFF" : "");
            // The full variable name, prefix and all: it is what `get` and
            // `override` want typed at them, so showing a trimmed form here
            // would be handing out a name that does not resolve. The CODE
            // column is where the short label lives.
            const char* cells[4] = { c.code ? c.code : "?",
                                     c.name ? c.name : "?", state, ev };
            char     line[160];
            uint16_t n = _build_row(line, sizeof(line), cells);
            const uint16_t code_r = _code_cell_r(), badge = _cell_col(C_STATE);
            _terminal.move_to({row, 0});
            _terminal.set_inverted(false);
            _write_code_cell(line, selected);
            if (c.active && !c.user_ack && n > (uint16_t)(badge + 2)) {
                // "ON" leads the STATE cell: split so only it inverts.
                _terminal.write_chars(line + code_r, (uint16_t)(badge - code_r));
                _terminal.set_inverted(true);
                _terminal.write_chars(line + badge, 2);
                _terminal.set_inverted(false);
                _terminal.write_chars(line + badge + 2, (uint16_t)(n - badge - 2));
            } else {
                _terminal.write_chars(line + code_r, (uint16_t)(n - code_r));
            }
            _terminal.clear_to_eol();
        }

        /** @brief Paint data row at screen @p row for visible entry @p vi: the full
         *  row when it holds an alarm, else a blanked row (past the set). Drawn every
         *  time; the terminal's back-buffer diff drops the cells that did not move. */
        void _draw_data_row(uint16_t row, uint16_t vi) {
            if (vi >= _nvis) { _blank_line(row); return; }    // past the set: blank it
            const Alarms::AlarmCondition& c = _model->condition(_vis[vi]);
            _draw_full_row(row, c, vi == _selected);
        }


        /** @brief Acknowledge the selected condition through the model. */
        void _ack_selected() {
            if (_model == nullptr || _selected >= _nvis) return;
            const Alarms::AlarmCondition& c = _model->condition(_vis[_selected]);
            if (c.name != nullptr) _model->ack(c.name);
            _dirty = true;   // ack changes the badge / sort
        }

        /** @brief Draw the static chrome: top margin, header, and the two blank
         *  rows around the legend. */
        void _draw_structure() {
            uint16_t legend = _legend_row();
            _blank_line(1);                                     // top margin
            _draw_header(HEADER_ROW);
            _blank_line((uint16_t)(legend - 1));                // blank above legend
            _blank_line((uint16_t)(legend + 1));                // blank below legend
        }

    public:
        /** @brief Bind to a terminal and alarm model. */
        AlarmsPage(Console& console, Alarms::AlarmModel* model)
            : ScrollTablePage(console.terminal()), _console(console), _model(model) {}

        /** @name Page interface
         *  @{ */

        const char* name() const override { return "ALARMS"; }

        void handle_text(char c) override {
            // SPACE cycles the filter (base); the legend row already outranks the
            // table rows (declared in focus()), so the badge reaches the wire
            // ahead of the repaint the same keypress triggered.
            if (_mode_text(c)) return;
            if (c == 'a' || c == 'A') _ack_selected();   // ack the selected condition
        }

        void handle_command(Command cmd, uint16_t = 1) override {
            if (_mode_command(cmd)) return;              // LEFT/RIGHT change filter
            if (_nav_command(cmd)) { _dirty = true; return; }
            if (cmd == Command::ENTER) { _top = 0; _selected = 0; _dirty = true; }  // jump to top
        }

        void deactivate() override {}

        void focus() override {
            // The legend carries the mode badge, which acknowledges a keypress and
            // so must beat the table rows it sits under. Declared here because the
            // Console hands the content band back on every page switch.
            _claim_mode_row(_legend_row());
            _dirty = true;   // draw the whole page fresh (the terminal diff pares it)
        }

        void tick(bool active) override {
            if (!active) return;
            // Rebuild on the value cadence (or immediately after a nav), exactly
            // as the MONITOR and PARAMS tables do. This page used to carry three
            // 64-bit signatures of the alarm set to decide whether a repaint was
            // needed at all; that is the terminal back-buffer's job, and it does
            // it per cell. What is left is the cheap guard the other tables use:
            // don't re-collect and re-format twenty rows every single beat.
            uint64_t now = HAL::Clock::uptime_ms();
            if (!_dirty && now - _last_refresh_ms < Config::PAGE_HOT_MS) return;
            _last_refresh_ms = now;
            _dirty = false;
            uint16_t window = _window();
            if (window == 0) return;
            // Rebuild the view set and re-clamp the window, then redraw the whole
            // table immediate-mode: the terminal's back-buffer diff emits only the
            // cells that actually changed, so the page keeps no paint shadow.
            _collect();
            uint16_t maxtop = _max_top();
            if (_top > maxtop) _top = maxtop;
            _draw_structure();
            _draw_rules(window);
            _draw_notes();
            _draw_footer(_legend_row(), _model ? _model->active_count() : 0);
            for (uint16_t vi = 0; vi < window; vi++)
                _draw_data_row((uint16_t)(DATA_TOP + vi), (uint16_t)(_top + vi));
        }

        /** @} */
    };

}}

#endif // PAGES_ALARMS_H
