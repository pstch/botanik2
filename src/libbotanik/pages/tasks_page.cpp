/**
 * @file tasks_page.cpp
 * @brief TasksPage implementation - the scheduler task-cost table.
 */
#include "libbotanik/core/health.h"
#include "libbotanik/pages/tasks_page.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_TASKS_PAGE

#include <cstdio>
#include <cstring>

#include "libbotanik/hal/platform.h"

namespace Botanik { namespace Pages {

    void TasksPage::focus() {
        // The filter badge acknowledges a keypress, so its row outranks the
        // table (the Console resets the band on every page switch).
        _claim_legend_row(_legend_row());
        _dirty = true;
        _clamp_selection();
        _follow_selection();
    }

    void TasksPage::tick(bool active) {
        if (!active) return;
        uint64_t now = HAL::Clock::uptime_ms();
        // The costs move every beat, but repainting the whole table at that
        // rate would spend the console's entire byte budget on digits nobody
        // can read that fast.
        if (!_dirty && now - _last_refresh_ms < REFRESH_MS) return;
        _last_refresh_ms = now;
        _dirty = false;
        // A narrow view selects on a verdict, which moves with no keypress
        // behind it: its row set has to be rebuilt every refresh.
        if (_mode != MODE_ALL) _invalidate_order();
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

    const char* TasksPage::_mode_label(uint8_t i) const {
        return (i == MODE_ALL) ? "ALL" : Plc::health_name(_mode_health(i));
    }

    void TasksPage::_ensure_order() const {
        const uint8_t n = (uint8_t)_reg.count();
        if (_order_of == n && _order_gen == _filter_gen) return;   // still current
        _order_n = 0;
        for (uint8_t i = 0; i < n; i++)
            if (_filter_match(_reg.task(i).name) && _in_mode(i)) _order[_order_n++] = i;
        for (uint8_t i = 1; i < _order_n; i++) {
            const uint8_t v = _order[i];
            uint8_t j = i;
            while (j > 0 && _before(v, _order[j - 1])) {
                _order[j] = _order[j - 1];
                j--;
            }
            _order[j] = v;
        }
        _order_of  = n;
        _order_gen = _filter_gen;
    }

    bool TasksPage::_before(uint8_t a, uint8_t b) const {
        // The site's programs first: they are what the plant does, and the
        // library's beat is the same on every unit. Then by name within each
        // block, which is how one row is found by eye.
        // Asked by PHASE: nothing but Plant registers at PHASE_CONTROL, so it
        // IS "this is one of the site's laws". Health used to stand in for it
        // and cannot any more - a program only has a verdict if it opted in.
        const bool pa = _reg.task(a).phase == Tasks::PHASE_CONTROL;
        const bool pb = _reg.task(b).phase == Tasks::PHASE_CONTROL;
        if (pa != pb) return pa;
        return std::strcmp(_name_of(a), _name_of(b)) < 0;
    }

    uint16_t TasksPage::_panel_top() const {
        uint16_t last = _console.last_row();
        return (last > (uint16_t)(DATA_TOP + PANEL_H))
             ? (uint16_t)(last - PANEL_H) : DATA_TOP;
    }

    uint8_t TasksPage::_columns(Col* out) const {
        out[C_NAME]   = Col{ "TASK",      0         };   // flex: the base fits it
        out[C_PERIOD] = Col{ "EVERY",     PERIOD_W  };
        out[C_HEALTH] = Col{ "HEALTH",    HEALTH_W  };
        out[C_PHASE]  = Col{ "PHASE",     PHASE_W   };
        out[C_MEAN]   = Col{ "MEAN us",   MEAN_W    };
        out[C_MISSED] = Col{ "MISSED",    MISSED_W  };
        return 6;
    }

    const char* TasksPage::_period_str(uint32_t period_ms) {
        // One buffer, filled per row as the table is drawn: a period is a
        // number a task carries now, not one of a handful of classes there
        // was any point caching.
        static char buf[12];
        Utils::format_int(buf, sizeof buf, (long long)period_ms);
        return buf;
    }

    const char* TasksPage::_health_str(const Tasks::Task& t) {
        // Blank, not "-": a task that is not a program has no verdict to give,
        // which is a different thing from one whose verdict is unknown.
        return (t.health != nullptr) ? Plc::health_name(*t.health) : "";
    }

    void TasksPage::_draw_row(uint16_t idx, uint16_t row, bool selected) {
        const Tasks::Task& t = _row(idx);
        char mean[CELL_CAP], missed[CELL_CAP], line[LINE_CAP];
        const char* cells[6] = {
            t.name != nullptr ? t.name : "",
            _period_str(t.period_ms),
            _health_str(t),
            Tasks::phase_name(t.phase),
            _num(mean,   sizeof(mean),   t.cost.us_mean(), t.cost.runs),
            _num(missed, sizeof(missed), t.cost.missed,    t.cost.runs),
        };
        const Fit fit = _fit_columns();          // one solve, both ends
        uint16_t n = _build_row(fit, line, sizeof(line), cells);
        uint16_t name_l = 0, name_r = 0;
        _cell_span(fit, C_NAME, name_l, name_r);
        _draw_selected_line(row, line, n, name_l, name_r,
                            selected);
    }

    void TasksPage::_draw_structure() {
        _blank_line(TOP_ROW);
        _draw_header(HEADER_ROW);
        _draw_rule(RULE_ROW, '+');
    }

    const char* TasksPage::_selected_cell() const {
        if (_selected >= _count()) return nullptr;
        return _row(_selected).name;
    }

    void TasksPage::_draw_panel() {
        uint16_t r = _panel_top();
        _draw_separator(r);
        _blank_line((uint16_t)(r + 1));
        if (_count() == 0) {
            for (uint16_t i = 2; i < PANEL_H; i++)
                if ((uint16_t)(r + i) != _legend_row()) _blank_line((uint16_t)(r + i));
            _draw_legend(_legend_row(), "no match");
            return;
        }
        const Tasks::Task& t = _row(_selected);
        char last[CELL_CAP], peak[CELL_CAP], runs[CELL_CAP], missed[CELL_CAP];
        char duty[24];
        // _num() returns "-" for a task that has never run, so its buffer is
        // untouched then - the return value is what to draw, never the buffer.
        const char* last_s   = _num(last,   sizeof(last),   t.cost.us_last, t.cost.runs);
        const char* peak_s   = _num(peak,   sizeof(peak),   t.cost.us_max,  t.cost.runs);
        const char* missed_s = _num(missed, sizeof(missed), t.cost.missed,  t.cost.runs);
        Utils::format_int(runs, sizeof(runs), (long long)t.cost.runs);

        const uint32_t period = t.period_ms;
        const char* duty_s = "-";
        if (t.cost.runs != 0 && period != 0) {
            char pct[16];
            snprintf(duty, sizeof(duty), "%s %%",
                     Utils::format_duty(pct, sizeof(pct), t.cost.us_mean(),
                                        period, 1));
            duty_s = duty;
        }
        // Two facts per row. MISSED repeats here beside the run count, the
        // number it has to be read against: 12 dropped out of 12 is a dead
        // task, out of a million is a bad afternoon.
        _panel_field2((uint16_t)(r + 2), "Last us", last_s,
                      PANEL_COL2, "Peak us", peak_s);
        _panel_field2((uint16_t)(r + 3), "Runs", runs,
                      PANEL_COL2, "Missed", missed_s);
        // The rate beside what the task spends of it: the two are read
        // together, and a declared period earns a panel field rather than a
        // column repeating one of four numbers down the table.
        _panel_field2((uint16_t)(r + 4), "Of period", duty_s,
                      PANEL_COL2, "Every ms", _period_str(t.period_ms));
        _draw_legend(_legend_row(), nullptr);
    }
}}

#endif // BOTANIK_FEATURE_TASKS_PAGE
