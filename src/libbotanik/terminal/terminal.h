#ifndef TERMINAL_TERMINAL_H
#define TERMINAL_TERMINAL_H

#include <cstdint>

#include "libbotanik/panes/home.h"
#include "libbotanik/panes/shell.h"
#include "libbotanik/panes/logs.h"
#include "libbotanik/panes/status.h"

#include "libbotanik/utils/serial.h"

using namespace Botanik::Utils;
using namespace Botanik::Panes;


namespace Botanik { namespace Terminal {

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
        // Constructor
        AnsiTerminal(BufferedSerial& s);

        // Step through the terminal handling code
        void step();

        // Switch to the given pane index
        void switch_to(uint8_t index);

    };
    
}}

#endif // TERMINAL_TERMINAL_H