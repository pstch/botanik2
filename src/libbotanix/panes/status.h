#ifndef PANES_STATUS_H
#define PANES_STATUS_H

#include "libbotanix/panes/pane.h"
#include "libbotanix/utils/serial.h"

using namespace Botanix::Utils;

namespace Botanix { namespace Panes {

    class StatusPane : public Pane {
    private:
        BufferedSerial& _serial;
        
        void _draw_header();
        void _draw_menu();

        void draw();
    };

}}

#endif // PANES_STATUS_H