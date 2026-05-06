#ifndef PANES_SHELL_H
#define PANES_SHELL_H

#include "libbotanik/panes/pane.h"
#include "libbotanik/utils/serial.h"
#include "libbotanik/plcio/vars.h"

namespace Botanik { namespace Panes {

    class ShellPane : public Pane {
    private:
        using BufferedSerial = Utils::BufferedSerial;
        BufferedSerial& _serial;

        void _draw_header();
        void _draw_menu();
    public:
        // TODO: implement log-entry rendering into the shell pane
        void flush_log_entry(PlcIO::LogEntry_t* entry);
    };

}}

#endif // PANES_SHELL_H