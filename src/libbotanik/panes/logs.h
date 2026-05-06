#ifndef PANES_LOGS_H
#define PANES_LOGS_H

#include "libbotanik/panes/pane.h"
#include "libbotanik/utils/serial.h"

namespace Botanik { namespace Panes {

    class LogsPane : public Pane {
    private:
        using BufferedSerial = Utils::BufferedSerial;
        BufferedSerial& _serial;

        void _draw_header();
        void _draw_menu();
    };

}}

#endif // PANES_LOGS_H