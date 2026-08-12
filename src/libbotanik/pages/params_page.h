// REVIEW_35: TODO
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
 *   rows()-1         edit prompt / hint line (just above the alarm bar)
 *
 * UP/DOWN move a selection cursor (the selected row is drawn inverted); the
 * detail panel tracks it. ENTER opens an inline edit on the bottom prompt line:
 * a static " name = " label and a single-line TextEditor (anchored to the
 * bottom row) render the value, seeded with the row's current text. ENTER
 * commits (parse + validate through Params::set); ESC/Ctrl+C cancels. An
 * invalid value is rejected and flagged, leaving edit mode open.
 *
 * An applied value is LIVE but not durable: the row shows a trailing '*', the
 * prompt row counts what is outstanding, and ^O (Params::save_all) writes them
 * all. Applying a config GROUP is still a separate act - bump that group's
 * commit serial by hand.
 */

#include <cstdint>                          // uint8_t/uint16_t/uint64_t
#include <cstdio>                           // snprintf
#include <cstring>                          // memcpy / strchr / strcmp / strlen

#include "libbotanik/config/terminal.h"   // Config::SCREEN_* / EDITOR_* / SHELL_* / PAGE_*
#include "libbotanik/config/vars.h"       // Config::VAR_* / STR_*

#include "libbotanik/console/console.h"     // Console::rows / terminal
#include "libbotanik/console/editor.h"      // Terminal::TextEditor
#include "libbotanik/console/terminal.h"    // AnsiTerminal
#include "libbotanik/pages/page.h"          // Pages::Page
#include "libbotanik/pages/scroll_table.h"  // Pages::ScrollTablePage
#include "libbotanik/vars/access.h"         // Vars::VAR_GROUPS_MAX / var_group
#include "libbotanik/vars/params.h"         // Vars::Params

namespace Botanik { namespace Pages {

    /** @brief Full-screen editable configuration table (see file header). */
    class ParamsPage : public ScrollTablePage {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Console      = Terminal::Console;
        using InputEditor  = Terminal::TextEditor;
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
        // [group] badge sits on (unit is a table column).
        static constexpr uint16_t PANEL_H    = 7;
        /** @brief Column order; the selection cursor inverts C_NAME's cell.
         *
         *  REGISTER rather than DEFAULT: a register is an EXTERNAL contract (a
         *  Modbus master's map is written against it), so it wants scanning
         *  down a column, while a default is a per-row fact the panel below
         *  answers. It sits last, outboard of UNIT, since the unit belongs
         *  against the value it qualifies and the register says nothing about
         *  the reading. */
        enum { C_NAME = 0, C_TYPE, C_VALUE, C_UNIT, C_REGISTER };
        /** @} */

        /** @name State
         *  @{ */
        Vars::Params* _params;
        bool                _dirty    = true;    ///< table needs a redraw
        uint64_t            _last_refresh_ms = 0; ///< last cadence rebuild

        /** @name Edit sub-state (bottom-row inline editor)
         *  @{ */
        /** buffers the editor borrows; declared before it */
        Terminal::EditorBuffers<Config::SHELL_INPUT_LEN,
                                Config::EDITOR_MAX_ROWS> _editor_bufs;
        InputEditor         _editor;                 ///< single-line value editor
        bool                _editing    = false;
        uint16_t _edit_index = 0;   ///< the parameter being edited
        bool                _edit_error = false;      ///< last commit rejected
        /** last ^O save was refused by storage (shown on the prompt row; the
         *  values themselves stay live and stay marked unsaved) */
        bool                _save_failed = false;
        /** @} */
        /** @} */

    public:
        /** @brief @p console owns the content geometry; @p config is the backend
         *  the table reads/writes (may be null -> empty table). */
        ParamsPage(Console& console, Vars::Params* config)
            : ScrollTablePage(console), _params(config),
              _editor(&console, _editor_bufs.storage()) {}

        const char* name() const override { return "PARAMS"; }

        /** @name Input
         *  @{ */

        void handle_command(Command cmd, uint16_t count = 1) override;

        void handle_text(char c) override;

        bool handle_cancel() override {
            if (_editing) { _cancel_edit(); return true; }   // Ctrl+C leaves edit
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
        uint16_t _param_count() const { return _params != nullptr ? _params->count() : 0; }

        /** @name Group modes
         *  A parameter's group IS its name prefix (`net_`, `ntp_`, `mqtt_`) -
         *  the same prefix that names its commit serial - so the groups are
         *  discovered from the table rather than declared twice. Mode 0 shows
         *  every parameter; 1..N show one group each. SPACE / RIGHT step
         *  forward, LEFT back (scroll_table.h).
         *  @{ */
        static constexpr uint8_t GROUPS_MAX = Vars::VAR_GROUPS_MAX;

        /** @brief Distinct group prefixes, in table order, discovered ONCE. The
         *  table is fixed at build time, so the set cannot move; rebuilding it
         *  inside _mode_label() (called per parameter by _in_mode(), itself
         *  called per parameter by _visible_count()) would be a squared walk of
         *  ~150 rows on every repaint. Mutable: a cache, not state. */
        mutable const char* _group_name[GROUPS_MAX];
        mutable uint8_t     _group_n      = 0;
        mutable bool        _groups_ready = false;

        void _ensure_groups() const;

        /** @brief _group_of() returns a pointer to its own static scratch, so a
         *  second call clobbers the first. Copy each distinct group once into a
         *  table-lifetime store the mode labels can keep pointing at. */
        static const char* _group_intern(const char* g);

        uint8_t _mode_count() const override {
            _ensure_groups();
            return (uint8_t)(_group_n + 1);
        }
        const char* _mode_label(uint8_t i) const override;
        void _on_mode_changed() override;

        /** @brief True when parameter @p k belongs to the active view. */
        bool _in_mode(uint16_t k) const {
            if (_mode == 0) return true;
            return strcmp(_group_of(*_params->param(k).meta()), _mode_label(_mode)) == 0;
        }

        /** @brief Map a visible row to its parameter index (VAR_NONE-ish: the
         *  count when out of range). */
        uint16_t _param_at(uint16_t row) const;

        /** @brief Parameters the active view shows. Cached: a parameter's group
         *  is its name prefix, so membership is fixed for the life of the build
         *  and only a view change can move this. */
        mutable uint16_t _vis_count = 0;
        mutable bool     _vis_ready = false;
        uint16_t _visible_count() const;
        /** @} */

        /** @brief Row the bottom edit/hint prompt sits on (above the alarm bar). */
        uint16_t _prompt_row() const { return (uint16_t)(_console.rows() - 1); }

        /** @brief The group badge sits on the panel's trailing spacer, one row
         *  above the prompt - so an open editor, which owns the prompt row,
         *  never covers it. */
        uint16_t _mode_row() const { return (uint16_t)(_panel_top() + PANEL_H - 1); }
        /** @brief Columns the panel's second and third fields start at
         *  (Minimum | Maximum | Default).
         *
         *  Each sits one value-field past the one before, not spread across the
         *  row: a 12-wide label plus " : " puts a value at +15, and a bound is
         *  at most ~12 wide, so the next field starts +12 from there. Even
         *  spacing over 80 columns leaves ~19 blank between a bound and the
         *  next label, which reads as three unrelated fields rather than one
         *  band. The slack goes to Default, the only non-numeric value. */
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
        // --------------------------------------------------------------------

        /** @brief Flexible VALUE width: the display minus the four fixed columns
         *  and every border, floored at 8 and capped at VAL_CAP-1. */
        uint16_t _value_w() const {
            return _flex_width((uint16_t)(MARGIN + 2 + NAME_W + 3 + TYPE_W + 3 + 3
                                          + REGISTER_W + 3 + UNIT_W + 2), (uint16_t)(VAL_CAP - 1));
        }

        uint8_t _columns(Col* out) const override;

        void _draw_row(uint16_t idx, uint16_t row, bool selected);

        void _draw_structure();

        // Detail panel (selected parameter's min/max/description/notes)
        // --------------------------------------------------------------------

        /** @brief The config group a parameter belongs to: the prefix of its
         *  name, which is also the name of the group's commit serial. */
        static const char* _group_of(const Vars::Variable& d);

        /** @brief Format a bound, in the parameter's OWN type - so a real
         *  parameter's band shows its decimals instead of being truncated to
         *  whole numbers. "-" when there is no declared band. */
        const char* _fmt_bound(char* buf, size_t cap, const Vars::Variable& d,
                               Vars::Scalar v) const {
            if (d.type == Vars::T_STR || (d.flags & Vars::F_BOUNDED) == 0) {
                snprintf(buf, cap, "-"); return buf;
            }
            const char* t = _params->format_scalar_of(d, v, buf, cap);
            if (t != buf) { strncpy(buf, t, cap - 1); buf[cap - 1] = 0; }
            return buf;
        }

        /** @brief @p d's factory default, rendered. A T_STR's lives in flash as
         *  text rather than in the Scalar, so it is taken straight; everything
         *  else goes through the row's own type and digits. */
        const char* _fmt_default(char* buf, size_t cap,
                                 const Vars::Variable& d) const {
            if (d.type == Vars::T_STR) return d.def.s ? d.def.s : "";
            return _params->format_scalar_of(d, d.def, buf, cap);
        }


        /** @brief Draw the detail panel for the selected parameter: a separator,
         *  a blank spacer, the four detail rows, and a trailing blank spacer. */
        void _draw_panel();

        // Bottom prompt row: inline edit field or hint
        // --------------------------------------------------------------------

        /** @brief " name = " static label for the parameter being edited. */
        uint16_t _edit_label(char* buf, size_t cap) const {
            int n = snprintf(buf, cap, " %s = ", _params->param(_edit_index).meta()->name);
            return (n < 0) ? 0 : (uint16_t)n;
        }

        void _draw_label();

        void _draw_prompt_row();

        /** @brief ^O: commit every unsaved parameter. */
        void _save_all();

        void _draw_error_marker();

        void _clear_error();

        // Edit sub-state
        // --------------------------------------------------------------------

        void _start_edit();

        void _cancel_edit();

        void _commit_edit();

        void _handle_edit_command(Command cmd, uint16_t count);
    };

}}

#endif // PAGES_PARAMS_PAGE_H
