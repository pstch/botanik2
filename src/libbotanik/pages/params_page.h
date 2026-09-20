#ifndef PAGES_PARAMS_PAGE_H
#define PAGES_PARAMS_PAGE_H

/**
 * @file params_page.h
 * @brief ParamsPage - interactive table of configuration parameters.
 *
 * Layout (row 0 is the terminal status bar, the bottom row the alarm bar); the
 * same bordered shape as AlarmsPage - a blank top margin and blank columns each
 * side, widened to the display:
 *   row 1            blank top margin
 *   row 2            header  (| PARAMETER | TYPE | VALUE | UNIT | REGISTER |)
 *   row 3            top rule
 *   rows 4..K        one row per Param (windowed / scrollable)
 *   rows K..         detail panel for the selected parameter: a separator then
 *                    Minimum / Maximum / Default, Description, Notes
 *   last_row()       edit prompt / hint line (just above the alarm bar)
 *
 * UP/DOWN move a selection cursor (the selected row is drawn inverted); the
 * detail panel tracks it. ENTER opens an inline edit on the bottom prompt line:
 * a static " name = " label and a single-line TextEditor (anchored to the
 * bottom row) render the value, seeded with the row's current text. ENTER
 * commits (parse + validate through Bank::set); ESC/Ctrl+C cancels. An
 * invalid value is rejected and flagged, leaving edit mode open.
 *
 * Typing narrows the table to the parameters whose name holds what was typed
 * (BACKSPACE pops a character, ESC clears); the legend badge shows the filter.
 *
 * An applied value is LIVE but not durable: the row shows a trailing '*', the
 * prompt row counts what is outstanding, and ^O (Persist::save_all) writes them
 * all. Applying a config GROUP is still a separate act - bump that group's
 * commit serial by hand.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "libbotanik/config/terminal.h"

#include "libbotanik/console/console.h"
#include "libbotanik/console/editor.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/console/page.h"
#include "libbotanik/pages/inline_field.h"
#include "libbotanik/pages/scroll_table.h"
#include "libbotanik/vars/access.h"
#include "libbotanik/vars/persist.h"

namespace Botanik { namespace Pages {

    /** @brief Full-screen editable configuration table (see file header). */
    class ParamsPage : public ScrollTablePage {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Console      = Terminal::Console;
        using Command      = Terminal::InputEvent::Command;

        /** @name Layout
         *  @{ Bordered table with the same shape as AlarmsPage: a blank top
         *  margin row, MARGIN blank columns each side, widened to the display. */
        static constexpr uint16_t TOP_ROW    = 1;    // blank top margin
        static constexpr uint16_t HEADER_ROW = 2;
        static constexpr uint16_t RULE_ROW   = 3;
        static constexpr uint16_t DATA_TOP   = 4;    // first parameter row
        // Wide enough for the longest name today plus headroom (see monitor.h).
        static constexpr uint16_t NAME_W     = 24;   // parameter-name column
        static constexpr uint16_t TYPE_W     = 4;    // data-type column (u16/enum/f32/b)
        /** @brief The Modbus TCP slave register column. 8 wide: it has to hold
         *  the "REGISTER" header, and the number itself never exceeds 5 digits. */
        static constexpr uint16_t REGISTER_W = 8;
        static constexpr uint16_t UNIT_W     = 5;    // physical-unit column
        static constexpr uint16_t VAL_CAP    = 40;   // formatted value cap
        static constexpr uint16_t LINE_CAP   = Config::TABLE_LINE_LEN;
        // Detail panel below the table: separator, blank spacer, Minimum /
        // Maximum / Default, Description, Notes, then the trailing spacer the
        // [filter] badge sits on (unit is a table column).
        static constexpr uint16_t PANEL_H    = 7;
        /** @brief Column order; the selection cursor inverts C_NAME's cell.
         *  REGISTER rather than DEFAULT: a register is an external contract a
         *  master's map is written against, so it wants scanning down a
         *  column. Last, outboard of UNIT. */
        enum { C_NAME = 0, C_TYPE, C_VALUE, C_UNIT, C_REGISTER };
        /** @} */

        /** @name State
         *  @{ */
        Vars::Bank* _params;
        /** @brief What makes an edit durable; null leaves ^O a no-op. */
        Vars::Persist* _persist = nullptr;
        uint64_t            _last_refresh_ms = 0; ///< last cadence rebuild

        /** @} */

        /** @name Edit sub-state (bottom-row inline editor)
         *  @{ */
        using ValueField  = InlineField<Config::SHELL_INPUT_LEN,
                                        Config::EDITOR_MAX_ROWS>;
        using EditOutcome = ValueField::Outcome;
        ValueField          _field;
        uint16_t _edit_index = 0;   ///< the parameter being edited
        /** last ^O save was refused by storage (shown on the prompt row; the
         *  values themselves stay live and stay marked unsaved) */
        bool                _save_failed = false;

        /** @} */

    public:
        /** @brief @p console owns the content geometry; @p config is the backend
         *  the table reads/writes (may be null -> empty table). */
        ParamsPage(Console& console, Vars::Bank* config,
                   Vars::Persist* persist = nullptr)
            : ScrollTablePage(console), _params(config), _persist(persist),
              _field(console) {}

        const char* name() const override { return "PARAMS"; }

        /** @name Input
         *  @{ */

        void handle_command(Command cmd, uint16_t count = 1) override;

        void handle_text(char c) override;

        bool handle_cancel() override {
            if (_field.open()) { _cancel_edit(); return true; }   // Ctrl+C leaves edit
            return false;
        }

        /** @} */

        /** @name Focus & update
         *  @{ */

        void deactivate() override {}

        void focus() override;

        void tick(bool active) override;

        /** @} */

    private:
        uint16_t _param_count() const { return _params != nullptr ? _params->param_count() : 0; }

        /** @name Row filter
         *  A typed substring of the parameter's name, which is how a tuning
         *  session finds the handful of rows it is about: the name carries the
         *  group (`net_`, `ntp_`, `mqtt_`), so a group is just the filter that
         *  spells it. See scroll_table.h.
         *  @{ */
        void _on_filter_changed() override;

        /** @brief True when parameter @p k passes the filter. */
        bool _visible(uint16_t k) const {
            return _filter_match(_params->param_at(k).meta().name);
        }

        /** @brief Map a visible row to its parameter index (VAR_NONE-ish: the
         *  count when out of range). */
        uint16_t _param_at(uint16_t row) const;

        /** @brief Parameters the filter shows, and the filter generation they
         *  were counted for (0 = never counted). The table is fixed for the
         *  life of the build, so only a filter change can move this. Holding
         *  the generation rather than a validity flag makes the invalidation
         *  structural: a path that moves the filter without going through
         *  _on_filter_changed() still cannot leave a stale count behind. */
        mutable uint16_t _vis_count = 0;
        mutable uint16_t _vis_gen   = 0;
        uint16_t _visible_count() const;
        /** @} */

        /** @brief Row the bottom edit/hint prompt sits on (above the alarm bar). */
        uint16_t _prompt_row() const { return _console.last_row(); }

        /** @brief The filter badge sits on the panel's trailing spacer, one row
         *  above the prompt - so an open editor, which owns the prompt row,
         *  never covers it. */
        uint16_t _legend_row() const { return (uint16_t)(_panel_top() + PANEL_H - 1); }
        /** @brief Columns the panel's second and third fields start at
         *  (Minimum | Maximum | Default). Each sits one value-field past the
         *  one before rather than spread across the row: a 12-wide label plus
         *  " : " puts a value at +15, a bound is at most ~12 wide, so the next
         *  starts +12 from there. The slack goes to Default. */
        static constexpr uint16_t PANEL_COL2 = 28;
        static constexpr uint16_t PANEL_COL3 = 55;



        /** @brief First row of the detail panel (separator), reserving PANEL_H
         *  rows above the prompt. */
        uint16_t _panel_top() const {
            uint16_t pr = _prompt_row();
            return (pr > (uint16_t)(DATA_TOP + PANEL_H)) ? (uint16_t)(pr - PANEL_H) : DATA_TOP;
        }

        /** @brief Rows available for the parameter window (table shrinks to leave
         *  room for the detail panel). */
        uint16_t _visible_rows() const {
            uint16_t pt = _panel_top();
            return (pt > DATA_TOP) ? (uint16_t)(pt - DATA_TOP) : 0;
        }

        /** @name ScrollTablePage geometry
         *  @{ */
        uint16_t _data_top()  const override { return DATA_TOP; }
        uint16_t _window()    const override { return _visible_rows(); }
        uint16_t _row_count() const override { return _visible_count(); }
        const char* _selected_cell() const override;
        /** @} */

        // Table rendering (5 columns: name | type | value | register | unit)

        uint8_t _columns(Col* out) const override;
        /** @brief VALUE stretches: it is the cell whose contents vary. */
        uint8_t _flex_col() const override { return C_VALUE; }

        void _draw_row(uint16_t idx, uint16_t row, bool selected);

        void _draw_structure();

        // Detail panel (selected parameter's min/max/description/notes)

        /** @brief Format a bound, in the parameter's OWN type - so a real
         *  parameter's band shows its decimals instead of being truncated to
         *  whole numbers. */
        const char* _fmt_bound(char* buf, size_t cap, const Vars::Descriptor& d,
                               Vars::Scalar v) const {
            const char* t = Vars::format_declared(d, v, buf, cap);
            if (t != buf) { strncpy(buf, t, cap - 1); buf[cap - 1] = 0; }
            return buf;
        }

        /** @brief @p d's factory default, rendered. A T_STR's lives in flash as
         *  text rather than in the Scalar, so it is taken straight; everything
         *  else goes through the row's own type and digits. */
        const char* _fmt_default(char* buf, size_t cap,
                                 const Vars::Descriptor& d) const {
            if (d.type == Vars::T_STR) return d.def.s ? d.def.s : "";
            return Vars::format_declared(d, d.def, buf, cap);
        }


        /** @brief Draw the detail panel for the selected parameter: a separator,
         *  a blank spacer, the four detail rows, and a trailing blank spacer. */
        void _draw_panel();

        // Bottom prompt row: inline edit field or hint

        /** @brief " name = " static label for the parameter being edited. */
        uint16_t _edit_label(char* buf, size_t cap) const {
            int n = snprintf(buf, cap, " %s = ", _params->param_at(_edit_index).meta().name);
            return (n < 0) ? 0 : (uint16_t)n;
        }

        void _draw_prompt_row();

        /** @brief ^O: commit every unsaved parameter. */
        void _save_all();

        // Edit sub-state

        void _start_edit();

        void _cancel_edit();

        void _commit_edit();

        void _handle_edit_command(Command cmd, uint16_t count);
    };

}}

#endif // PAGES_PARAMS_PAGE_H
