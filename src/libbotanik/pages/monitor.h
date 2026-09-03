// REVIEW_35: TODO
#ifndef PAGES_MONITOR_H
#define PAGES_MONITOR_H

/**
 * @file monitor.h
 * @brief MonitorPage - scrollable read-only table of every variable except the
 *        two kinds with a page of their own, narrowed by a typed filter.
 *
 * Layout (row 0 is the status bar, the bottom row the alarm bar):
 *   row 1            blank top margin
 *   row 2            header  (| SCOPE | VARIABLE | VALUE | UNIT | OVERRIDE |)
 *   row 3            top rule (corners cap to '^' when rows are hidden above)
 *   rows 4..N-10      one row per status variable (window into the full set)
 *   row N-9          bottom rule (corners cap to 'v' when hidden below)
 *   rows N-7..N-3    detail panel: Description / Notes / Mapping / Quality
 *   row N-2          legend: view badge + position + [filter] + hint
 *   row N-1          the override field, or what the last override key did
 *
 * LEFT / RIGHT step the view: every published row, only the ones reading dead,
 * or only the ones being forced. The typed filter narrows whichever is showing.
 *
 * The one page that FORCES a row as well as showing it: `^O` opens an override
 * field on the bottom row (ENTER commits, ESC abandons), `^V` toggles whether
 * the force reads as a good value or a dead one, `^R` releases it and `^S`
 * writes it to flash. All four act on the row under the cursor. An override is
 * live the moment it commits and is lost at the next boot until saved.
 *
 * @note Nothing is declared per site - naming a variable is what files it - so
 *       there is no per-variable code and no table to fall out of step with.
 */

#include <cstdint>
#include <cstring>

#include "libbotanik/config/terminal.h"

#include "libbotanik/console/console.h"
#include "libbotanik/console/editor.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/pages/page.h"
#include "libbotanik/pages/scroll_table.h"
#include "libbotanik/vars/access.h"

namespace Botanik { namespace Pages {

    /** @brief Full-screen scrollable status-variable table (see file header). */
    class MonitorPage : public ScrollTablePage {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Console      = Terminal::Console;
        using InputEditor  = Terminal::TextEditor;
        using Command      = Terminal::InputEvent::Command;

        /** @name Layout
         *  @{ */
        static constexpr uint16_t TOP_ROW    = 1;    // blank top margin
        static constexpr uint16_t HEADER_ROW = 2;
        static constexpr uint16_t TRULE_ROW  = 3;    // top rule
        static constexpr uint16_t DATA_TOP   = 4;    // first variable row
        static constexpr uint16_t SCOPE_W    = 8;    // where the value lives
        // Wide enough for the longest site name today (18) plus headroom, since
        // the generated tables keep growing and a clipped name is unusable.
        static constexpr uint16_t NAME_W     = 24;   // variable-name column
        static constexpr uint16_t UNIT_W     = 6;    // physical-unit column
        static constexpr uint16_t OVR_W      = 9;    // forced value, or "-"
        static constexpr uint16_t VAL_CAP    = 48;   // formatted value buffer
        static constexpr uint16_t LINE_CAP   = Config::TABLE_LINE_LEN;
        // Detail panel below the table: bottom rule + blank spacer +
        // Description + Mapping + Quality + blank spacer, then the legend and its
        // own trailing blank (see _footer_row()).
        static constexpr uint16_t PANEL_H    = 7;
        /** @brief Longest override the field accepts. A forced reading is a
         *  number or an enum name; nothing typed here is a sentence. */
        static constexpr uint16_t OVR_INPUT_LEN = 48;
        /** @brief Row arrays the field lays out over. It is single-line, so one
         *  row is all it ever uses; the spares cost 36 bytes. */
        static constexpr uint16_t OVR_EDIT_ROWS = 4;
        /** @brief Column order, for the derived cell positions below. */
        enum { C_SCOPE = 0, C_NAME, C_VALUE, C_UNIT, C_OVR };
        /** @} */

        /** @name Views
         *  The base's view-mode machinery (scroll_table.h): RIGHT steps
         *  forward, LEFT back. The two narrow views select on exactly what the
         *  VALUE column already flags per row, [I] and [O], so the badge and
         *  the letters cannot say different things.
         *  @{ */
        static constexpr uint8_t MODE_ALL      = 0;  // every published row
        static constexpr uint8_t MODE_INVALID  = 1;  // the reading is not good
        static constexpr uint8_t MODE_OVERRIDE = 2;  // a force is standing
        static constexpr uint8_t MODE_COUNT    = 3;

        uint8_t _mode_count() const override { return MODE_COUNT; }
        const char* _mode_label(uint8_t i) const override;
        /** @brief A new view keeps a different set of rows, so the caches the
         *  old one filled say nothing - and neither does the pinned selection.
         *  @see _on_filter_changed for why the pin is dropped HERE. */
        void _on_mode_changed() override {
            _invalidate_rows();
            _sel_ok = false;
            _dirty  = true;
        }
        /** @brief True while the view selects on LIVE state, which moves with no
         *  keypress behind it: its row set has to be re-counted every refresh. */
        bool _mode_is_live() const { return _mode != MODE_ALL; }
        /** @} */

        /** @name State
         *  @{ */
        /** @brief Sequential-scan memo for _ref_at(), and the cached row total.
         *  Rows are drawn top to bottom, so the last (row -> index) pair turns
         *  the walk into a resume.
         *  @note Invalidated by whatever moves the mapping: the filter, the
         *  view, and every refresh in a live view. */
        mutable uint16_t    _memo_row  = 0;
        mutable uint16_t    _memo_idx  = 0;
        mutable bool        _memo_ok   = false;

        /** @brief The row the highlighted line was DRAWN from, pinned there.
         *
         *  A live view (`invalid`, `override`) selects on readings, so the set
         *  can change with no keypress: a variable that goes good between the
         *  paint and the keystroke shifts every row under the cursor, and a
         *  fresh _ref_at(_selected) would then name a different variable from
         *  the one on screen. The operator acts on what they can see. */
        mutable Vars::VarRef _sel_ref = { Vars::VAR_NONE };
        /** @brief Which selection index @c _sel_ref was resolved for. A cursor
         *  MOVE is the operator changing their mind and re-resolves; the set
         *  moving under a still cursor must not. */
        mutable uint16_t     _sel_row = 0;
        mutable bool         _sel_ok  = false;

        /** @brief The row a key acts on: the pin while the cursor has not
         *  moved, a fresh walk the moment it has. */
        Vars::VarRef _selected_ref() const;
        mutable uint16_t    _total_n   = 0;
        /** @brief Which filter _total_n was counted for; 0 = none yet, and how
         *  _invalidate_rows() drops it.
         *  Holding the generation rather than a valid flag makes the
         *  invalidation structural: a path that moves the filter without going
         *  through _on_filter_changed() still cannot leave a stale count
         *  behind. */
        mutable uint16_t    _total_gen = 0;
        /** @brief Not const: ^O / ^R / ^S write overrides through it. */
        Vars::Bank*           _io;
        /** @brief How to turn a Modbus unit id into the plant's own tag for it.
         *  A function pointer rather than an RtuClient*, so the page depends on
         *  "something can name a device", not on the field bus. */
        const char* (*_device_name)(const void*, uint8_t) = nullptr;
        const void*           _device_ctx = nullptr;
        /** @brief A nav happened: redraw on the next tick rather than waiting for
         *  the value cadence, so scrolling/selection feels immediate. */
        bool                _dirty    = true;
        /** @brief Uptime of the last value refresh (see tick()). */
        uint64_t            _last_refresh_ms = 0;
        /** @} */

        /** @name Override field (^O)
         *  @{ */
        /** buffers the editor borrows; declared before it */
        Terminal::EditorBuffers<OVR_INPUT_LEN, OVR_EDIT_ROWS> _editor_bufs;
        InputEditor         _editor;
        bool                _editing = false;
        /** @brief The row being forced. Held as a REF, not a display row: the
         *  filter can renumber the table under an open field. */
        Vars::VarRef        _edit_ref = { Vars::VAR_NONE };
        bool                _edit_error = false;   ///< last commit rejected
        /** @brief What the last override key did, and when it stops being said.
         *  The bottom row is otherwise blank, and ^R / ^S change nothing a
         *  short table shows. */
        char                _said[48] = {0};
        uint64_t            _said_until_ms = 0;
        /** @} */

    public:
        /** @brief @p console owns the content geometry; @p io is the variable
         *  table this shows and forces (null -> empty page). */
        MonitorPage(Console& console,
                    Vars::Bank* io)
            : ScrollTablePage(console), _io(io),
              _editor(&console, _editor_bufs.storage()) {}

        /** @brief Teach the page the plant's name for a Modbus unit. Without it
         *  the Mapping row just says "dev 34". */
        void set_device_names(const char* (*fn)(const void*, uint8_t),
                              const void* ctx) {
            _device_name = fn;
            _device_ctx  = ctx;
        }

        const char* name() const override { return "MONITOR"; }

        /** @name Input
         *  @{ */

        void handle_command(Command cmd, uint16_t count = 1) override;

        void handle_text(char c) override;

        bool handle_cancel() override {
            if (_editing) { _cancel_edit(); return true; }   // Ctrl+C leaves the field
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
        // Model

        void _on_filter_changed() override {
            _dirty = true;
            _invalidate_rows();    // the row -> index mapping just moved
            // ...and so did what the pin means. _selected_ref() holds its
            // answer while the cursor INDEX has not moved, and a filter forces
            // the cursor to row 0 - which is where it already is when the
            // operator starts typing one. The pin then survived a completely
            // renumbered row set until the next draw re-took it, and ^V / ^R /
            // ^S acted on the pre-filter variable. Dropped here rather than in
            // _invalidate_rows(), which tick() also calls on every refresh of a
            // live view, where keeping the pin is the point.
            _sel_ok = false;
        }

        /** @brief Drop the row caches: the (row -> index) memo and the total.
         *  _total_gen is a filter generation, and those are never 0. */
        void _invalidate_rows() const { _memo_ok = false; _total_gen = 0; }

        /** @brief Whether row @p i is in the table at all, past the filter and
         *  kept by the view: a published row whose name holds the typed text,
         *  and which reads dead / is forced when the view asks for that. */
        bool _visible(uint16_t i) const;

        uint16_t _total() const;

        /** @brief Legend row and bottom rule row. The legend sits one row up
         *  from the bottom so it has a blank line each side (a badge jammed
         *  against the alarm bar does not read as one). The bottom rule sits
         *  PANEL_H rows above it, reserving the detail panel. */
        uint16_t _footer_row() const { return (uint16_t)(_console.rows() - 2); }
        uint16_t _brule_row() const {
            uint16_t f = _footer_row();
            return (f > (uint16_t)(DATA_TOP + PANEL_H)) ? (uint16_t)(f - PANEL_H) : DATA_TOP;
        }

        /** @brief Visible variable rows (table shrinks to leave room for the
         *  detail panel below the bottom rule). */
        uint16_t _window() const override {
            uint16_t brule = _brule_row();
            return (brule > DATA_TOP) ? (uint16_t)(brule - DATA_TOP) : 0;
        }

        /** @name ScrollTablePage geometry
         *  @{ */
        uint16_t _data_top()  const override { return DATA_TOP; }
        uint16_t _row_count() const override { return _total(); }
        const char* _selected_cell() const override;
        /** @} */

        uint8_t _columns(Col* out) const override;
        /** @brief VALUE stretches: it is the cell whose contents vary. */
        uint8_t _flex_col() const override { return C_VALUE; }

        /** @brief The reference for variable @p r; @p ok is false when @p r is
         *  past the end. */
        Vars::VarRef _ref_at(uint16_t r, bool& ok) const;

        // Rendering

        /** @brief Copy @p s into @p out unless it already IS @p out (var_format
         *  returns flash / pool text for an enum or a string). */
        static void _adopt(char* out, size_t cap, const char* s) {
            if (s != out) { std::strncpy(out, s, cap - 1); out[cap - 1] = 0; }
        }

        /** @brief Build the full "  | scope | name | value | unit | override |"
         *  row. Resolves the variable ONCE: the row is drawn per visible line
         *  every refresh, and _at() is a walk over the group table. */
        uint16_t _row_text(uint16_t r, char* out, size_t cap) const;

        /** @brief Draw data row @p r at screen @p row, inverting the NAME field
         *  for the selection cursor when @p selected. */
        void _draw_row(uint16_t r, uint16_t row, bool selected);

        /** @brief One line saying where a reading comes from. */
        void _describe_binding(const Vars::Binding& b, char* out, size_t cap) const;

        /** @brief Format a row's LIVE value - what the driver or the program
         *  last put there, with any override ignored. The VALUE column shows
         *  this; the OVERRIDE column shows the force. */
        const char* _live_text(Vars::Var h, char* buf, size_t cap) const;
        /** @brief Put @p tag in front of what is already in @p buf. */
        static void _prefix(char* buf, size_t cap, const char* tag);
        /** @brief Mark a VALUE cell: `[O]` forced, `[I]` the reading underneath
         *  is not good, `[OI]` both. */
        static void _flag_value(Vars::Var h, char* buf, size_t cap);

        /** @brief Draw both rules, capping with '^' / 'v' when rows are hidden
         *  above / below the window. */
        void _draw_rules();

        /** @brief Detail panel below the bottom rule: blank, Description, Notes,
         *  blank for the selected variable. */
        void _draw_panel();

        void _draw_footer();

        // Overrides

        /** @brief Row the override field / its report sits on. */
        uint16_t _prompt_row() const { return (uint16_t)(_console.rows() - 1); }

        /** @brief How long the prompt row keeps what it was told to say (ms). */
        static constexpr uint32_t SAID_MS = 4000;

        /** @brief Say @p text on the prompt row for SAID_MS. */
        void _say(const char* text);

        /** @brief " name = " label the field is indented past. */
        uint16_t _edit_label(char* buf, size_t cap) const;

        void _draw_prompt_row();
        void _draw_error_marker();
        void _clear_error();

        /** @brief ^O: open the field on the selected row, seeded with what the
         *  row reads now. Refused on a T_STR row, which has no forced form. */
        void _start_edit();
        void _cancel_edit();
        void _commit_edit();
        void _handle_edit_command(Command cmd, uint16_t count);

        /** @brief ^V: flip the forced value between reading good and reading
         *  dead. With nothing forced it takes the current reading first, which
         *  is the one-key way to make a sensor read failed. */
        void _toggle_validity();

        /** @brief ^R: release the selected row's override. */
        void _clear_override();
        /** @brief ^S: write the selected row's override to flash. */
        void _save_override();

        /** @brief Draw the static chrome (blank top margin + header); the rules and
         *  footer carry live state and are drawn separately each tick. */
        void _draw_structure() {
            _blank_line(TOP_ROW);
            _draw_header(HEADER_ROW);
        }

    };

}}

#endif // PAGES_MONITOR_H
