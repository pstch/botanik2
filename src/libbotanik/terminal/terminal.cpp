#include <cstdint>

#include "serial.h"
#include "parser.h"
#include "shell.h"

#include "../panes/home.h"
#include "../panes/shell.h"
#include "../panes/logs.h"
#include "../panes/status.h"


class AnsiTerminal {
private:
    BufferedSerial& _serial;
    InputParser     _parser;

    HomePane        _home;
    ShellPane       _shell;
    LogsPane        _logs;
    StatusPane      _status;

    Pane*        _active_pane;
    uint8_t      _current_index;

public:
    AnsiTerminal(BufferedSerial& s) 
        : _serial(s), _home(s), _shell(s), _logs(s), _status(s) 
    {
        _active_pane = &_home;
        _current_index = 1;
    }

    // step through the terminal handling code
    void step() {
        char byte;
        while (_serial.read(&byte, 1)) {
            InputEvent ev = _parser.process(byte);
            
            if (ev.type == InputEvent::Type::VIEW_SWITCH) {
                switch_to(ev.value); // ev.value is 1, 2, 3, or 4
            } else if (ev.type == InputEvent::Type::CONTENT) {
                _active_pane->handle_text(ev.value);
            } else if (ev.type == InputEvent::Type::ACTION) {
                _active_pane->handle_command(ev.cmd);
            }
        }
        
        // Let the active pane do background work (e.g., Shell talking to M7)
        _active_pane->step();
    }

    void switch_to(uint8_t index) {
        if (index < 1 || index > 4 || index == _current_index) return;
        
        // 1. Clear Screen
        Ansi::clear_screen(_serial);
        
        // 2. Reassign active pane
        if (index == 1) _active_pane = &_home;
        if (index == 2) _active_pane = &_shell;
        if (index == 3) _active_pane = &_logs;
        if (index == 4) _active_pane = &_status;
        
        _current_index = index;
        
        // 3. Tell the pane to redraw itself
        _active_pane->focus();
    }
};