#include "libbotanik/plcio/vars.h"
#include "libbotanik/panes/shell.h"
#include "libbotanik/terminal/ansi.h"

namespace Botanik { namespace Panes {

    void ShellPane::flush_log_entry(PlcIO::LogEntry_t* entry) {
        // TODO: format the log entry into a char buffer
        char*    msg_buf[Config::LOG_FORMATTED_LEN];
        uint16_t msg_len;

        uint16_t N = _calculate_line_count(msg_len);
        
        // 1. Save the initial state to restore later
        Terminal::Ansi::CursorPos initial_pos = _editor.current_position();
        uint16_t editor_rows = _editor.occupied_rows();

        // 2. Move to the very last line of the input
        // This ensures that any \n triggers a scroll from the "bottom" of our context
        _terminal.move_cursor(_editor.end_position());

        // 3. Trigger N line feeds
        // The terminal handles the hardware scroll here if we hit the bottom
        for (uint16_t i = 0; i < N; ++i) _terminal.write("\n");

        // 4. Relative Jump Back to the start of the Editor
        // We move UP by (N feeds + the height of the editor - 1)
        // This lands us exactly on the first line of the prompt.
        uint16_t move_up_dist = N + editor_rows - 1;
        Ansi::move_up(_serial, move_up_dist);
        Ansi::move_line_begin(_serial);

        // 5. Insert N lines & Write Log
        // This pushes the Editor back down to its original visual spot
        _terminal.insert_lines(N);
        _render_log_formatted(log); 

        // 6. Restore Cursor
        // We restore the original CursorPos. 
        // Note: Because of the insertion logic, initial_pos is still valid!
        _terminal.move_cursor(initial_pos);
    }
}    

}}