#ifndef TERMINAL_TERMINAL_H
#define TERMINAL_TERMINAL_H

#include <cstdint>

#include "libbotanik/panes/home.h"
#include "libbotanik/panes/shell.h"
#include "libbotanik/panes/logs.h"
#include "libbotanik/panes/status.h"

#include "libbotanik/utils/serial.h"

namespace Botanik { namespace Terminal {


    class AnsiTerminal {
        using BufferedSerial = Utils::BufferedSerial;
    private:
        BufferedSerial&     _serial;
        InputParser         _parser;

        //Panes::HomePane     _home;
        //Panes::ShellPane    _shell;
        //Panes::LogsPane     _logs;
        //Panes::StatusPane   _status;

        Panes::Pane*        _active_pane;
        uint8_t             _current_index;

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