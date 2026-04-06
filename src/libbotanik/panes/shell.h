#ifndef PANES_SHELL_H
#define PANES_SHELL_H

#include "libbotanik/panes/pane.h"
#include "libbotanik/utils/serial.h"

using namespace Botanik::Utils;

namespace Botanik { namespace Panes {

    class ShellPane : public Pane {
    private:
        BufferedSerial& _serial;
        
        void _draw_header();
        void _draw_menu();
    };

}}

#endif // PANES_SHELL_H