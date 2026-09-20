#ifndef PAGES_TASKS_PAGE_H
#define PAGES_TASKS_PAGE_H

/**
 * @file tasks_page.h
 * @brief TasksPage - the beat, as a table: what runs, how often, and what it
 *        costs.
 *
 * The scheduler self-instruments per task (Tasks::TaskCost); `stats` reports the
 * whole beat with no attribution, so "which task blew the budget" is the
 * question this page answers.
 * @see dev_pages for the layout and what each column means.
 */

#include <cstdint>

#include "libbotanik/config/scheduler.h"
#include "libbotanik/config/terminal.h"

#include "libbotanik/console/console.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/console/page.h"
#include "libbotanik/pages/scroll_table.h"
#include "libbotanik/core/health.h"
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

        /** @name Views
         *  The base's view-mode machinery (scroll_table.h): RIGHT steps
         *  forward, LEFT back. One narrow view per HEALTH code, selecting on
         *  exactly what the column shows, so the badge and the column cannot
         *  say different things. An entry with no verdict - a control program
         *  that did not opt in - appears under ALL alone.
         *  @{ */
        static constexpr uint8_t MODE_ALL      = 0;
        static constexpr uint8_t MODE_HEALTHY  = 1;
        static constexpr uint8_t MODE_DEGRADED = 2;
        static constexpr uint8_t MODE_FAULTED  = 3;
        static constexpr uint8_t MODE_COUNT    = 4;

        uint8_t _mode_count() const override { return MODE_COUNT; }
        const char* _mode_label(uint8_t i) const override;
        /** @brief A view keeps a different set, and the row order was built for
         *  the old one. */
        void _on_mode_changed() override { _invalidate_order(); _dirty = true; }
        /** @brief The health a view selects on. Only meaningful past MODE_ALL. */
        static Plc::Health _mode_health(uint8_t m) {
            return (m == MODE_HEALTHY)  ? Plc::Health::HEALTHY
                 : (m == MODE_DEGRADED) ? Plc::Health::DEGRADED
                                        : Plc::Health::FAULTED;
        }
        /** @brief Whether task @p i belongs in the view now showing. A verdict
         *  moves with no keypress behind it, which is why the order is rebuilt
         *  every refresh in a narrow view. */
        bool _in_mode(uint8_t i) const {
            if (_mode == MODE_ALL) return true;
            const Plc::Health* h = _reg.task(i).health;
            return h != nullptr && *h == _mode_health(_mode);
        }
        /** @} */

        /** @name State
         *  @{ */
        /** @brief Borrowed; the firmware owns it and it outlives the page. */
        const Tasks::Registry&   _reg;
        uint64_t                     _last_refresh_ms = 0;
        /** @brief Row order: the tasks the filter keeps, programs first then by
         *  name (_before()). Registration order is the beat's order, but PHASE
         *  already carries that and finding one task by eye matters more here.
         *  The registry is frozen after bring-up, so only a changed count or a
         *  new filter rebuilds this. */
        mutable uint8_t              _order[Config::MAX_TASKS] = {};
        mutable uint8_t              _order_n = 0;
        /** @brief What _order holds: the task count it was built from, and the
         *  filter generation it was built for. */
        mutable uint8_t              _order_of = 0;
        /** @brief 0 = never built, which no filter generation ever is. */
        mutable uint16_t             _order_gen = 0;

        /** @brief Drop the row order. _filter_gen is never 0, so a rebuild is
         *  what any reader gets next. */
        void _invalidate_order() const { _order_gen = 0; }
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
            if (_mode_command(cmd)) return;         // LEFT/RIGHT change the view
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
