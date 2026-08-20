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
 *   rows 4..N-3      one row per status variable (window into the full set)
 *   row N-8          bottom rule (corners cap to 'v' when hidden below)
 *   rows N-7..N-3    detail panel: Description / Notes / Mapping / Quality
 *   row N-2          legend: [filter] badge + position + hint
 *   row N-1          the override field, or what the last override key did
 *
 * The one page that FORCES a row as well as showing it: `^O` opens an override
 * field on the bottom row (ENTER commits, ESC abandons), `^R` releases the
 * selected row's override and `^S` writes it to flash. All three act on the row
 * under the cursor. An override is live the moment it commits and is lost at
 * the next boot until saved.
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

        /** @name State
         *  @{ */
        /** @brief Sequential-scan memo for _ref_at(), and the cached row total.
         *
         *  _ref_at() and _total() both walk the table applying _visible() (a
         *  substring search per row), and _ref_at() runs once per DRAWN row: a
         *  12-row window over a 259-row table is ~3100 searches a refresh. Rows
         *  are drawn top to bottom, so remembering the last (row -> index) pair
         *  turns the walk into a resume. Invalidated by _on_filter_changed(),
         *  the only thing that moves the mapping - the table's rows and scopes
         *  are fixed at build time. */
        mutable uint16_t    _memo_row  = 0;
        mutable uint16_t    _memo_idx  = 0;
        mutable bool        _memo_ok   = false;
        mutable uint16_t    _total_n   = 0;
        /** @brief Which filter _total_n was counted for; 0 = none yet.
         *  Holding the generation rather than a valid flag makes the
         *  invalidation structural: a path that moves the filter without going
         *  through _on_filter_changed() still cannot leave a stale count
         *  behind. */
        mutable uint16_t    _total_gen = 0;
        /** @brief Not const: ^O / ^R / ^S write overrides through it. */
        Vars::Bank*           _io;
        /** @brief How to turn a row's writer index into a program name, and the
         *  thing that knows. Null when the build has no scan. A function
         *  pointer rather than a `Scan::Driver*`, so the page depends on
         *  "something can name a writer" and not on the scan cycle or its
         *  feature flag. */
        const char* (*_writer_name)(const void*, uint8_t) = nullptr;
        const void*           _writer_ctx = nullptr;
        /** @brief How to turn a Modbus unit id into the plant's own tag for it.
         *  Same shape and same reason as _writer_name: the page depends on
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

        /** @brief Teach the page to name the program that computes a row. Wired
         *  by the firmware, the only thing holding both the page and the scan
         *  driver. Without it a computed row just reads "computed". */
        void set_writer_names(const char* (*fn)(const void*, uint8_t),
                              const void* ctx) {
            _writer_name = fn;
            _writer_ctx  = ctx;
        }

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
            _memo_ok = false;      // the row -> index mapping just moved
        }

        /** @brief Whether row @p i is in the table at all, and past the filter:
         *  every published row whose name holds the typed text. */
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
