#ifndef PAGES_TASKS_PAGE_H
#define PAGES_TASKS_PAGE_H

/**
 * @file tasks_page.h
 * @brief TasksPage - the beat, as a table: what runs, how often, and what it costs.
 *
 * The scheduler self-instruments per task (Scheduler::TaskCost), and until this
 * page there was nowhere to read it: `stats` reports the whole beat's CPU with no
 * attribution, so "which task blew the budget" had no answer. That is the
 * question this page exists for.
 *
 * Layout (row 0 is the terminal status bar, the bottom row the alarm bar), the
 * same bordered shape as PARAMS and MONITOR:
 *   row 1            blank top margin
 *   row 2            header  (| TASK | GROUP | ms | PHASE | LAST | MEAN |)
 *   row 3            top rule
 *   rows 4..K        one row per registered task (windowed / scrollable)
 *   rows K..         detail panel for the selected task: peak, runs, and the
 *                    share of its own period the mean accounts for
 *   rows()-1         hint line (just above the alarm bar)
 *
 * Read-only: UP/DOWN move a selection cursor and the panel tracks it. Rows are
 * in registration order, which is the order the beat runs them (phase-major),
 * so the table reads top to bottom as the beat does.
 *
 * The times are microseconds measured by the registry around each dispatch. An
 * all-time max never decays, so one outlier pins it for the boot: MEAN is the
 * column, and the peak sits in the panel where it reads as the diagnostic it is.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../../libbotanik_config.h"
#include "../scheduler/tasks.h"
#include "page.h"
#include "scroll_table.h"
#include "../console/console.h"
#include "../console/terminal.h"

namespace Botanik { namespace Pages {

    /** @brief Full-screen read-only table of the scheduler's tasks (see file
     *  header). */
    class TasksPage : public ScrollTablePage {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Console      = Terminal::Console;
        using Command      = Terminal::InputEvent::Command;

        /** @name Layout
         *  @{ */
        static constexpr uint16_t TOP_ROW    = 1;    // blank top margin
        static constexpr uint16_t HEADER_ROW = 2;
        static constexpr uint16_t RULE_ROW   = 3;
        static constexpr uint16_t DATA_TOP   = 4;    // first task row
        static constexpr uint16_t NAME_W     = 16;   // task-name column
        static constexpr uint16_t GROUP_W    = 5;    // "bkgnd"
        static constexpr uint16_t PERIOD_W   = 6;    // the group's period, ms
        static constexpr uint16_t PHASE_W    = 7;    // "service"
        static constexpr uint16_t MEAN_W     = 8;    // mean us
        static constexpr uint16_t CELL_CAP   = 24;   // one formatted cell
        static constexpr uint16_t LINE_CAP   = 200;  // row string buffer
        /** @brief Separator, spacer, two detail rows, spacer, hint framing. */
        static constexpr uint16_t PANEL_H    = 6;
        /** @brief Column order; the selection cursor inverts C_NAME's cell. */
        enum { C_NAME = 0, C_GROUP, C_PERIOD, C_PHASE, C_LAST, C_MEAN };
        /** @} */

        /** @name State
         *  @{ */
        Console&                     _console;
        /** @brief Borrowed; the firmware owns it and it outlives the page. Null
         *  leaves an empty table rather than a crash. */
        const Scheduler::Registry*   _reg = nullptr;
        bool                         _dirty = true;
        uint64_t                     _last_refresh_ms = 0;
        /** @} */

    public:
        /** @param console owns the content geometry; @p reg is the task table
         *  the page reads (may be null -> empty table). */
        TasksPage(Console& console, const Scheduler::Registry* reg = nullptr)
            : ScrollTablePage(console.terminal()), _console(console), _reg(reg) {}

        /** @brief Bind the registry after construction (the firmware builds the
         *  page before it has finished registering). */
        void set_registry(const Scheduler::Registry* reg) { _reg = reg; _dirty = true; }

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
            if (_selected >= _count()) _selected = 0;
            _follow_selection();
        }

        void tick(bool active) override {
            if (!active) return;
            uint64_t now = HAL::Clock::uptime_ms();
            // The costs move every beat, so this is a live page - but repainting
            // a whole table at the beat rate would spend the console's entire
            // byte budget on digits nobody can read that fast.
            if (!_dirty && now - _last_refresh_ms < Config::PAGE_HOT_MS) return;
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
        uint16_t _count() const {
            return (_reg != nullptr) ? _reg->count() : 0;
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

        /** @brief Flexible LAST width: the display minus the five fixed columns
         *  and every border. */
        uint16_t _last_w() const {
            return _flex_width((uint16_t)(2 * MARGIN + 2 + NAME_W + 3 + GROUP_W + 3
                                          + PERIOD_W + 3 + PHASE_W + 3 + 3
                                          + MEAN_W + 2), (uint16_t)(CELL_CAP - 1));
        }

        uint8_t _columns(Col* out) const override {
            out[C_NAME]   = Col{ "TASK",  NAME_W    };
            out[C_GROUP]  = Col{ "GROUP", GROUP_W   };
            out[C_PERIOD] = Col{ "ms",    PERIOD_W  };
            out[C_PHASE]  = Col{ "PHASE", PHASE_W   };
            out[C_LAST]   = Col{ "LAST",  _last_w() };
            out[C_MEAN]   = Col{ "MEAN",  MEAN_W    };
            return 6;
        }

        /** @brief Microseconds, or "-" before the first run. Rendered as a plain
         *  integer: a task that has never run and one that ran in under a
         *  microsecond are different facts, and 0 would conflate them. */
        static const char* _us(char* buf, size_t cap, uint32_t us, uint32_t runs) {
            if (runs == 0) { snprintf(buf, cap, "-"); return buf; }
            snprintf(buf, cap, "%lu", (unsigned long)us);
            return buf;
        }

        void _draw_row(uint16_t idx, uint16_t row, bool selected) {
            const Scheduler::Task& t = _reg->task((uint8_t)idx);
            char per[CELL_CAP], last[CELL_CAP], mean[CELL_CAP], line[LINE_CAP];
            snprintf(per, sizeof(per), "%lu",
                     (unsigned long)Scheduler::group_period_ms(t.group));
            const char* cells[6] = {
                t.name != nullptr ? t.name : "",
                Scheduler::group_name(t.group),
                per,
                Scheduler::phase_name(t.phase),
                _us(last, sizeof(last), t.cost.us_last, t.cost.runs),
                _us(mean, sizeof(mean), t.cost.us_mean(), t.cost.runs),
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

        /** @brief A full-width dashed separator between the table and the panel. */
        void _draw_separator(uint16_t row) {
            char buf[LINE_CAP];
            uint16_t n = 0;
            for (uint16_t i = 0; i < MARGIN && n < LINE_CAP - 1; i++) buf[n++] = ' ';
            uint16_t cols = _terminal.cols();
            uint16_t w = (cols > (uint16_t)(2 * MARGIN)) ? (uint16_t)(cols - 2 * MARGIN) : 0;
            for (uint16_t i = 0; i < w && n < LINE_CAP - 1; i++) buf[n++] = '-';
            _draw_line(row, buf, n);
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
            const Scheduler::Task& t = _reg->task((uint8_t)_selected);
            char peak[CELL_CAP], runs[CELL_CAP], duty[CELL_CAP];
            _us(peak, sizeof(peak), t.cost.us_max, t.cost.runs);
            snprintf(runs, sizeof(runs), "%lu", (unsigned long)t.cost.runs);

            const uint32_t period = Scheduler::group_period_ms(t.group);
            if (t.cost.runs == 0 || period == 0) snprintf(duty, sizeof(duty), "-");
            else {
                // Per-mille, so a task costing well under 1% still reads as a
                // number rather than rounding to zero.
                uint32_t permille = (uint32_t)(((uint64_t)t.cost.us_mean() * 1000u)
                                               / ((uint64_t)period * 1000u));
                snprintf(duty, sizeof(duty), "%lu.%lu %%",
                         (unsigned long)(permille / 10), (unsigned long)(permille % 10));
            }
            _panel_field((uint16_t)(r + 2), "Peak us", peak);
            _panel_field((uint16_t)(r + 3), "Runs", runs);
            _panel_field((uint16_t)(r + 4), "Of period", duty);
            _blank_line((uint16_t)(r + 5));
        }
    };

}}  // namespace Botanik::Pages

#endif // PAGES_TASKS_PAGE_H
