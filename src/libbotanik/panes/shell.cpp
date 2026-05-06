#include "libbotanik/plcio/vars.h"
#include "libbotanik/panes/shell.h"
#include "libbotanik/terminal/ansi.h"

namespace Botanik { namespace Panes {

    // TODO: implement flush_log_entry
    // Draft algorithm (members not yet wired up):
    //   1. format entry into a char buffer (_calculate_line_count, _render_log_formatted)
    //   2. save _editor.current_position() / _editor.occupied_rows()
    //   3. move to _editor.end_position() and write N newlines to scroll
    //   4. move up (N + editor_rows - 1) and to line begin
    //   5. _terminal.insert_lines(N) and write the formatted log
    //   6. _terminal.move_cursor(initial_pos) to restore the editor cursor
    void ShellPane::flush_log_entry(PlcIO::LogEntry_t* /* entry */) {}

}}
