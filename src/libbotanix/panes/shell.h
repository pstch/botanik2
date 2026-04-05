#ifndef PANES_SHELL_H
#define PANES_SHELL_H

#include "libbotanix/panes/pane.h"
#include "libbotanix/utils/serial.h"

namespace Botanix { namespace Panes {

    class ShellPane : public Pane {
    private:
        BufferedSerial& _serial;
        
        void _draw_header();
        void _draw_menu();
    };

}}

#endif // PANES_SHELL_H