#ifndef PAGES_TASKS_PAGE_H
#define PAGES_TASKS_PAGE_H

/**
 * @file tasks_page.h
 * @brief TasksPage - the beat, as a table: what runs, how often, and what it costs.
 *
 * The scheduler self-instruments per task (Tasks::TaskCost), and until this
 * page there was nowhere to read it: `stats` reports the whole beat's CPU with no
 * attribution, so "which task blew the budget" had no answer. That is the
 * question this page exists for.
 *
 * Layout (row 0 is the terminal status bar, the bottom row the alarm bar), the
 * same bordered shape as PARAMS and MONITOR:
 *   row 1            blank top margin
 *   row 2            header (| TASK | GROUP | EVERY ms | PHASE | MEAN us | PEAK us |)
 *   row 3            top rule
 *   rows 4..K        one row per registered task (windowed / scrollable)
 *   rows K..         detail panel for the selected task: its last sample, its
 *                    run count, and the share of its own period the mean takes
 *
 * Read-only: UP/DOWN move a selection cursor and the panel tracks it. Rows are
 * sorted by name - the beat's own order is phase-major, and the PHASE column
 * still carries it, but finding one task by eye beats reading the cycle here.
 *
 * The times are microseconds measured by the registry around each dispatch.
 * MEAN is what says whether a task is affordable; PEAK is beside it because an
 * all-time max never decays, so one outlier pins it for the boot and it has to
 * be read as the diagnostic it is rather than as the running cost.
 */

#include <cstdint>                          // uint8_t/uint32_t/uint64_t
#include <cstdio>                           // snprintf
#include <cstring>                          // strcmp

#include "libbotanik/config/scheduler.h"  // Config::LOOP_* / TASK_* / GROUP_*
#include "libbotanik/config/terminal.h"   // Config::SCREEN_* / EDITOR_* / SHELL_* / PAGE_*

#include "libbotanik/console/console.h"     // Console::rows / terminal
#include "libbotanik/console/terminal.h"    // AnsiTerminal
#include "libbotanik/pages/page.h"          // Pages::Page
#include "libbotanik/pages/scroll_table.h"  // Pages::ScrollTablePage
#include "libbotanik/tasks/tasks.h"     // Tasks::Registry / Task / TaskCost
#include "libbotanik/utils/text.h"          // Utils::format_int / format_float

namespace Botanik { namespace Pages {

    /** @brief Full-screen read-only table of the scheduler's tasks (see file
     *  header). */
    class TasksPage : public ScrollTablePage {
    private:
        using Console      = Terminal::Console;
        using Command      = Terminal::InputEvent::Command;

        /** @name Layout
         *  @{ */
        static constexpr uint16_t TOP_ROW    = 1;    // blank top margin
        static constexpr uint16_t HEADER_ROW = 2;
        static constexpr uint16_t RULE_ROW   = 3;
        static constexpr uint16_t DATA_TOP   = 4;    // first task row
        static constexpr uint16_t NAME_CAP   = 24;   // task-name column cap (flex)
        static constexpr uint16_t GROUP_W    = 5;    // "bkgnd"
        // Wide enough for its own heading: _build_row truncates a head to the
        // column width, so a narrower column would silently drop the unit.
        static constexpr uint16_t PERIOD_W   = 8;    // the group's period, ms
        static constexpr uint16_t PHASE_W    = 7;    // "service"
        static constexpr uint16_t MEAN_W     = 8;    // mean us
        static constexpr uint16_t PEAK_W     = 8;    // all-time peak us
        static constexpr uint16_t CELL_CAP   = 24;   // one formatted cell
        static constexpr uint16_t LINE_CAP   = Config::TABLE_LINE_LEN;
        /** @brief Separator, spacer, three detail rows, trailing spacer. */
        static constexpr uint16_t PANEL_H    = 6;
        /** @brief How often the table repaints (Config::PAGE_TASKS_MS). Slower
         *  than PAGE_HOT_MS: these are smoothed aggregates, not a live trace, and
         *  a whole table of digits churning once a second is both unreadable and
         *  a standing charge on the console's byte budget. */
        static constexpr uint32_t REFRESH_MS = Config::PAGE_TASKS_MS;
        /** @brief Column order; the selection cursor inverts C_NAME's cell. */
        enum { C_NAME = 0, C_GROUP, C_PERIOD, C_PHASE, C_MEAN, C_PEAK };
        /** @} */

        /** @name State
         *  @{ */
        Console&                     _console;
        /** @brief Borrowed; the firmware owns it and it outlives the page. */
        const Tasks::Registry&   _reg;
        bool                         _dirty = true;
        uint64_t                     _last_refresh_ms = 0;
        /** @brief Row order: task indices sorted by name.
         *
         *  Registration order is the order the beat runs them, which is the more
         *  informative default - but it makes finding one task a scan of the
         *  whole table, and PHASE already carries the ordering. Built once; the
         *  registry is frozen after bring-up, so a changed count is the only
         *  thing that can invalidate it. */
        mutable uint8_t              _order[Config::MAX_TASKS] = {};
        mutable uint8_t              _order_n = 0;
        /** @} */

    public:
        /** @param console owns the content geometry; @p reg is the task table the
         *  page reads. Both outlive the page (the firmware owns them). */
        TasksPage(Console& console, const Tasks::Registry& reg)
            : ScrollTablePage(console.terminal()), _console(console), _reg(reg) {}

        const char* name() const override { return "TASKS"; }

        /** @name Input
         *  @{ */
        void handle_command(Command cmd, uint16_t count = 1) override {
            if (_nav_command(cmd)) { _dirty = true; return; }
            (void)count;
        }
        void handle_text(char /*c*/) override {}
        bool handle_cancel() override { return false; }
        /** @} */

        /** @name Focus & update
         *  @{ */
        void deactivate() override {}

        void focus() override {
            _dirty = true;
            _clamp_selection();
            _follow_selection();
        }

        void tick(bool active) override {
            if (!active) return;
            uint64_t now = HAL::Clock::uptime_ms();
            // The costs move every beat, so this is a live page - but repainting
            // a whole table at the beat rate would spend the console's entire
            // byte budget on digits nobody can read that fast.
            if (!_dirty && now - _last_refresh_ms < REFRESH_MS) return;
            _last_refresh_ms = now;
            _dirty = false;
            uint16_t n = _count();
            _clamp_selection();
            _follow_selection();
            _draw_structure();
            uint16_t win = _window();
            for (uint16_t vi = 0; vi < win; vi++) {
                uint16_t row = (uint16_t)(DATA_TOP + vi), idx = (uint16_t)(_top + vi);
                if (idx < n) _draw_row(idx, row, idx == _selected);
                else { _terminal.move_to({row, 0}); _terminal.set_inverted(false);
                       _terminal.clear_to_eol(); }
            }
            _draw_panel();
        }
        /** @} */

    private:
        uint16_t _count() const { return _reg.count(); }

        /** @brief Build the name-sorted index, once. Insertion sort: the table is
         *  a couple of dozen entries and this runs at most once a boot. */
        void _ensure_order() const {
            const uint8_t n = (uint8_t)_count();
            if (_order_n == n) return;               // already built for this table
            for (uint8_t i = 0; i < n; i++) _order[i] = i;
            for (uint8_t i = 1; i < n; i++) {
                const uint8_t v = _order[i];
                const char* vn = _reg.task(v).name;
                uint8_t j = i;
                while (j > 0 && std::strcmp(_name_of(_order[j - 1]), vn ? vn : "") > 0) {
                    _order[j] = _order[j - 1];
                    j--;
                }
                _order[j] = v;
            }
            _order_n = n;
        }

        const char* _name_of(uint8_t i) const {
            const char* n = _reg.task(i).name;
            return n != nullptr ? n : "";
        }

        /** @brief The task shown on display row @p r. */
        const Tasks::Task& _row(uint16_t r) const {
            _ensure_order();
            return _reg.task(_order[r < _order_n ? r : 0]);
        }

        /** @name ScrollTablePage geometry
         *  @{ */
        uint16_t _data_top()  const override { return DATA_TOP; }
        uint16_t _window()    const override { return _visible_rows(); }
        uint16_t _row_count() const override { return _count(); }
        /** @} */

        /** @brief First row of the detail panel (separator). */
        uint16_t _panel_top() const {
            uint16_t last = (uint16_t)(_console.rows() - 1);
            return (last > (uint16_t)(DATA_TOP + PANEL_H))
                 ? (uint16_t)(last - PANEL_H) : DATA_TOP;
        }
        uint16_t _visible_rows() const {
            uint16_t pt = _panel_top();
            return (pt > DATA_TOP) ? (uint16_t)(pt - DATA_TOP) : 0;
        }

        /** @brief Flexible TASK width: the display minus the four fixed columns
         *  and every border. The name is what a reader scans, so it gets the
         *  slack rather than the numbers. */
        uint16_t _name_w() const {
            return _flex_width((uint16_t)(2 * MARGIN + 2 + 3 + GROUP_W + 3
                                          + PERIOD_W + 3 + PHASE_W + 3
                                          + MEAN_W + 3 + PEAK_W + 2), NAME_CAP);
        }

        uint8_t _columns(Col* out) const override {
            out[C_NAME]   = Col{ "TASK",      _name_w() };
            out[C_GROUP]  = Col{ "GROUP",     GROUP_W   };
            out[C_PERIOD] = Col{ "EVERY ms",  PERIOD_W  };
            out[C_PHASE]  = Col{ "PHASE",     PHASE_W   };
            out[C_MEAN]   = Col{ "MEAN us",   MEAN_W    };
            out[C_PEAK]   = Col{ "PEAK us",   PEAK_W    };
            return 6;
        }

        /** @brief Microseconds, or "-" before the first run. Rendered as a plain
         *  integer: a task that has never run and one that ran in under a
         *  microsecond are different facts, and 0 would conflate them. */
        static const char* _us(char* buf, size_t cap, uint32_t us, uint32_t runs) {
            if (runs == 0) return "-";      // never run: not the same fact as 0 us
            return Utils::format_int(buf, cap, (long long)us);
        }

        /** @brief The group's period as text. Four compile-time constants, so
         *  they are rendered once into a small cache rather than through an
         *  snprintf per row per refresh. */
        static const char* _period_str(Tasks::GroupId g) {
            static char buf[Tasks::GROUP_COUNT][8];
            static bool built = false;
            if (!built) {
                built = true;
                for (uint8_t i = 0; i < Tasks::GROUP_COUNT; i++)
                    Utils::format_int(buf[i], sizeof(buf[i]),
                                      (long long)Tasks::group_period_ms((Tasks::GroupId)i));
            }
            return (g < Tasks::GROUP_COUNT) ? buf[g] : "";
        }

        void _draw_row(uint16_t idx, uint16_t row, bool selected) {
            const Tasks::Task& t = _row(idx);
            char mean[CELL_CAP], peak[CELL_CAP], line[LINE_CAP];
            const char* cells[6] = {
                t.name != nullptr ? t.name : "",
                Tasks::group_name(t.group),
                _period_str(t.group),
                Tasks::phase_name(t.phase),
                _us(mean, sizeof(mean), t.cost.us_mean(), t.cost.runs),
                _us(peak, sizeof(peak), t.cost.us_max,  t.cost.runs),
            };
            uint16_t n = _build_row(line, sizeof(line), cells);
            _draw_selected_line(row, line, n, _cell_col(C_NAME), _cell_end(C_NAME),
                                selected);
        }

        void _draw_structure() {
            _blank_line(TOP_ROW);
            _draw_header(HEADER_ROW);
            _draw_rule(RULE_ROW, '+');
        }


        /** @brief Panel for the selected task: the peak, the run count, and what
         *  share of its own period the mean accounts for - which is the number
         *  that says whether a task is affordable at the rate it is running. */
        void _draw_panel() {
            uint16_t r = _panel_top();
            _draw_separator(r);
            _blank_line((uint16_t)(r + 1));
            if (_count() == 0) {
                for (uint16_t i = 2; i < PANEL_H; i++) _blank_line((uint16_t)(r + i));
                return;
            }
            const Tasks::Task& t = _row(_selected);
            char last[CELL_CAP], runs[CELL_CAP], duty[24];
            _us(last, sizeof(last), t.cost.us_last, t.cost.runs);
            Utils::format_int(runs, sizeof(runs), (long long)t.cost.runs);

            const uint32_t period = Tasks::group_period_ms(t.group);
            const char* duty_s = "-";
            if (t.cost.runs != 0 && period != 0) {
                // us_mean/period_ms IS per-mille of the period - the us/ms ratio
                // does the scaling - so this is a 32-bit UDIV, not the 64-bit
                // divide the explicit x1000 on both sides used to force.
                char pct[16];
                Utils::format_float(pct, sizeof(pct),
                                    (double)t.cost.us_mean() / (double)(period * 10u), 1);
                snprintf(duty, sizeof(duty), "%s %%", pct);
                duty_s = duty;
            }
            _panel_field((uint16_t)(r + 2), "Last us", last);
            _panel_field((uint16_t)(r + 3), "Runs", runs);
            _panel_field((uint16_t)(r + 4), "Of period", duty_s);
            _blank_line((uint16_t)(r + 5));
        }
    };

}}  // namespace Botanik::Pages

#endif // PAGES_TASKS_PAGE_H
