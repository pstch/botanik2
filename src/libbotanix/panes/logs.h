#ifndef PANES_LOGS_H
#define PANES_LOGS_H

#include "libbotanix/panes/pane.h"
#include "libbotanix/utils/serial.h"

using namespace Botanix::Utils;

namespace Botanix { namespace Panes {
    class LogsPane : public Pane {
    private:
        BufferedSerial& _serial;
        
        void _draw_header();
        void _draw_menu();
    };

}}

#endif // PANES_LOGS_H