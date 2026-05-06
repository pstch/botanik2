#ifndef PANES_STATUS_H
#define PANES_STATUS_H

#include "libbotanik/panes/pane.h"
#include "libbotanik/utils/serial.h"

namespace Botanik { namespace Panes {

    class StatusPane : public Pane {
    private:
        using BufferedSerial = Utils::BufferedSerial;
        BufferedSerial& _serial;

        void _draw_header();
        void _draw_menu();

        void draw();
    };

}}

#endif // PANES_STATUS_H