/**
 * @file tasks_page.cpp
 * @brief TasksPage implementation - the scheduler task-cost table.
 */
#include "libbotanik/pages/tasks_page.h"

#include <cstdio>                           // snprintf
#include <cstring>                          // strlen / strcmp / memcpy

#include "libbotanik/hal/platform.h"        // HAL::Clock::uptime_ms

namespace Botanik { namespace Pages {

    void TasksPage::focus() {
        _dirty = true;
        _clamp_selection();
        _follow_selection();
    }

    void TasksPage::tick(bool active) {
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

    void TasksPage::_ensure_order() const {
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

    uint16_t TasksPage::_panel_top() const {
        uint16_t last = (uint16_t)(_console.rows() - 1);
        return (last > (uint16_t)(DATA_TOP + PANEL_H))
             ? (uint16_t)(last - PANEL_H) : DATA_TOP;
    }

    uint16_t TasksPage::_name_w() const {
        return _flex_width((uint16_t)(2 * MARGIN + 2 + 3 + GROUP_W + 3
                                      + PERIOD_W + 3 + PHASE_W + 3
                                      + MEAN_W + 3 + MISSED_W + 2), NAME_CAP);
    }

    uint8_t TasksPage::_columns(Col* out) const {
        out[C_NAME]   = Col{ "TASK",      _name_w() };
        out[C_GROUP]  = Col{ "GROUP",     GROUP_W   };
        out[C_PERIOD] = Col{ "EVERY ms",  PERIOD_W  };
        out[C_PHASE]  = Col{ "PHASE",     PHASE_W   };
        out[C_MEAN]   = Col{ "MEAN us",   MEAN_W    };
        out[C_MISSED] = Col{ "MISSED",    MISSED_W  };
        return 6;
    }

    const char* TasksPage::_period_str(Tasks::GroupId g) {
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

    void TasksPage::_draw_row(uint16_t idx, uint16_t row, bool selected) {
        const Tasks::Task& t = _row(idx);
        char mean[CELL_CAP], missed[CELL_CAP], line[LINE_CAP];
        const char* cells[6] = {
            t.name != nullptr ? t.name : "",
            Tasks::group_name(t.group),
            _period_str(t.group),
            Tasks::phase_name(t.phase),
            _num(mean,   sizeof(mean),   t.cost.us_mean(), t.cost.runs),
            _num(missed, sizeof(missed), t.cost.missed,    t.cost.runs),
        };
        uint16_t n = _build_row(line, sizeof(line), cells);
        _draw_selected_line(row, line, n, _cell_col(C_NAME), _cell_end(C_NAME),
                            selected);
    }

    void TasksPage::_draw_structure() {
        _blank_line(TOP_ROW);
        _draw_header(HEADER_ROW);
        _draw_rule(RULE_ROW, '+');
    }

    void TasksPage::_draw_panel() {
        uint16_t r = _panel_top();
        _draw_separator(r);
        _blank_line((uint16_t)(r + 1));
        if (_count() == 0) {
            for (uint16_t i = 2; i < PANEL_H; i++) _blank_line((uint16_t)(r + i));
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
        // Two facts per row: the peak lost its column to MISSED and lands here,
        // and MISSED itself repeats beside the run count, which is the number it
        // has to be read against (12 dropped out of 12 is a dead task, out of a
        // million is a bad afternoon).
        _panel_field2((uint16_t)(r + 2), "Last us", last_s,
                      PANEL_COL2, "Peak us", peak_s);
        _panel_field2((uint16_t)(r + 3), "Runs", runs,
                      PANEL_COL2, "Missed", missed_s);
        _panel_field((uint16_t)(r + 4), "Of period", duty_s);
        _blank_line((uint16_t)(r + 5));
    }
}}
