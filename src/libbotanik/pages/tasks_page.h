#ifndef PAGES_TASKS_PAGE_H
#define PAGES_TASKS_PAGE_H

/**
 * @file tasks_page.h
 * @brief TasksPage - the beat, as a table: what runs, how often, and what it costs.
 *
 * The scheduler self-instruments per task (Tasks::TaskCost); this page is where
 * that is read. `stats` reports the whole beat's CPU with no attribution, so
 * "which task blew the budget" is the question this page answers.
 *
 * Layout (row 0 is the terminal status bar, the bottom row the alarm bar), the
 * same bordered shape as PARAMS and MONITOR:
 *   row 1            blank top margin
 *   row 2            header (| TASK | GROUP | HEALTH | PHASE | MEAN us | MISSED |)
 *   row 3            top rule
 *   rows 4..K        one row per registered task (windowed / scrollable)
 *   rows K..         detail panel for the selected task: its last and peak
 *                    sample, its run and missed counts, and the share of its own
 *                    period the mean takes
 *   rows()-2         legend: position, filter, hint
 *
 * Read-only: UP/DOWN move a selection cursor and the panel tracks it. Rows are
 * the site's control programs first, then the library's `sys_` tasks, each block
 * by name - the beat's own order is phase-major, and the PHASE column still
 * carries it, but finding one task by eye beats reading the cycle here.
 * Typing narrows the table to the tasks whose name holds what was typed.
 *
 * HEALTH is a control program's own verdict on its last activation
 * (Plc::Health), blank on an entry that is not a program - most of the table.
 * The panel carries the period instead: a rate is a declaration and does not
 * move, while this is the column worth running an eye down beside MISSED.
 *
 * MEAN is microseconds measured by the registry around each dispatch, and says
 * whether a task is affordable. MISSED takes the column beside it rather than
 * the peak: only one of the two is a fault. The peak never decays (one outlier
 * pins it for the boot) and lives in the panel; a missed activation means the
 * task did NOT run at the rate it declared, dropped and never caught up
 * (Tasks::TaskTimer).
 */

#include <cstdint>

#include "libbotanik/config/scheduler.h"
#include "libbotanik/config/terminal.h"

#include "libbotanik/console/console.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/pages/page.h"
#include "libbotanik/pages/scroll_table.h"
#include "libbotanik/tasks/tasks.h"
#include "libbotanik/utils/text.h"

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
        static constexpr uint16_t PERIOD_W   = 6;    // "60000" plus a margin
        static constexpr uint16_t HEALTH_W   = 9;    // "INHIBITED"
        static constexpr uint16_t PHASE_W    = 7;    // "service"
        static constexpr uint16_t MEAN_W     = 8;    // mean us
        static constexpr uint16_t MISSED_W   = 8;    // dropped activations
        static constexpr uint16_t CELL_CAP   = 24;   // one formatted cell
        static constexpr uint16_t LINE_CAP   = Config::TABLE_LINE_LEN;
        /** @brief Separator, spacer, three detail rows, then the legend. The
         *  panel carries five facts in those three rows (_panel_field2), rather
         *  than growing and taking two task rows off the table. */
        static constexpr uint16_t PANEL_H    = 6;
        /** @brief Where the second field on a panel row starts. Matches PARAMS,
         *  so the panels of the two tables line up. */
        static constexpr uint16_t PANEL_COL2 = 38;
        /** @brief How often the table repaints (Config::PAGE_TASKS_MS). Slower
         *  than PAGE_HOT_MS: these are smoothed aggregates, and a whole table of
         *  digits churning once a second is unreadable and a standing charge on
         *  the console's byte budget. */
        static constexpr uint32_t REFRESH_MS = Config::PAGE_TASKS_MS;
        /** @brief Column order; the selection cursor inverts C_NAME's cell. */
        enum { C_NAME = 0, C_PERIOD, C_HEALTH, C_PHASE, C_MEAN, C_MISSED };
        /** @} */

        /** @name State
         *  @{ */
        /** @brief Borrowed; the firmware owns it and it outlives the page. */
        const Tasks::Registry&   _reg;
        bool                         _dirty = true;
        uint64_t                     _last_refresh_ms = 0;
        /** @brief Row order: the tasks the filter keeps, programs first then by
         *  name (_before()). Registration order is the beat's order, but PHASE
         *  already carries that and finding one task by eye matters more here.
         *  The registry is frozen after bring-up, so only a changed count or a
         *  new filter rebuilds this. */
        mutable uint8_t              _order[Config::MAX_TASKS] = {};
        mutable uint8_t              _order_n = 0;
        /** @brief What _order holds: the task count it was built from, and the
         *  filter generation (0 = never built). */
        mutable uint8_t              _order_of = 0;
        mutable uint16_t             _order_gen = 0;
        /** @} */

    public:
        /** @param console owns the content geometry; @p reg is the task table the
         *  page reads. Both outlive the page (the firmware owns them). */
        TasksPage(Console& console, const Tasks::Registry& reg)
            : ScrollTablePage(console), _reg(reg) {}

        const char* name() const override { return "TASKS"; }

        /** @name Input
         *  @{ */
        void handle_command(Command cmd, uint16_t count = 1) override {
            if (_filter_command(cmd)) { _dirty = true; return; }
            if (_nav_command(cmd)) { _dirty = true; return; }
            (void)count;
        }
        void handle_text(char c) override { if (_filter_text(c)) _dirty = true; }
        /** @} */

        /** @name Focus & update
         *  @{ */
        void deactivate() override {}

        void focus() override;

        void tick(bool active) override;
        /** @} */

    private:
        /** @brief Tasks the filter keeps: the rows the cursor moves over. */
        uint16_t _count() const { _ensure_order(); return _order_n; }

        /** @brief Whether task @p a sorts before task @p b. */
        bool _before(uint8_t a, uint8_t b) const;

        /** @brief Build the sorted index of what the filter keeps.
         *  Insertion sort: the table is a couple of dozen entries and this runs
         *  once a boot, then once per filter keystroke. */
        void _ensure_order() const;

        const char* _name_of(uint8_t i) const {
            const char* n = _reg.task(i).name;
            return n != nullptr ? n : "";
        }

        /** @brief The task shown on display row @p r. */
        const Tasks::Task& _row(uint16_t r) const {
            _ensure_order();
            return _reg.task(_order[r < _order_n ? r : 0]);
        }

        /** @brief A different filter shows a different set. */
        void _on_filter_changed() override { _dirty = true; }

        /** @brief The legend sits on the panel's last row, as it does on the
         *  other tables. */
        uint16_t _legend_row() const { return (uint16_t)(_panel_top() + PANEL_H - 1); }

        /** @name ScrollTablePage geometry
         *  @{ */
        uint16_t _data_top()  const override { return DATA_TOP; }
        uint16_t _window()    const override { return _visible_rows(); }
        uint16_t _row_count() const override { return _count(); }
        const char* _selected_cell() const override;
        /** @} */

        /** @brief First row of the detail panel (separator). */
        uint16_t _panel_top() const;
        uint16_t _visible_rows() const {
            uint16_t pt = _panel_top();
            return (pt > DATA_TOP) ? (uint16_t)(pt - DATA_TOP) : 0;
        }

        uint8_t _columns(Col* out) const override;
        /** @brief TASK stretches: the name is what a reader scans, so it gets
         *  the display's slack rather than the numbers. */
        uint8_t _flex_col() const override { return C_NAME; }

        /** @brief One of the task's counters, or "-" before the first run: 0
         *  would conflate "never ran" with "ran in under a microsecond", and
         *  with "has never missed one". */
        static const char* _num(char* buf, size_t cap, uint32_t v, uint32_t runs) {
            if (runs == 0) return "-";      // never run: not the same fact as 0
            return Utils::format_int(buf, cap, (long long)v);
        }

        /** @brief The group's period as text, into a per-row buffer. */
        static const char* _period_str(uint32_t period_ms);

        /** @brief @p t's health as a word, or "" when it is not a program. */
        static const char* _health_str(const Tasks::Task& t);

        void _draw_row(uint16_t idx, uint16_t row, bool selected);

        void _draw_structure();


        /** @brief Panel for the selected task: its last and peak sample, its run
         *  and missed counts, and the share of its own period the mean takes -
         *  the number that says whether it is affordable at its rate. */
        void _draw_panel();
    };

}}  // namespace Botanik::Pages

#endif // PAGES_TASKS_PAGE_H
