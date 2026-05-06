#ifndef PAGES_SHELL_H
#define PAGES_SHELL_H

#include "libbotanik/pages/page.h"
#include "libbotanik/utils/serial.h"
#include "libbotanik/plcio/vars.h"

namespace Botanik { namespace Pages {

    class ShellPage : public Page {
    private:
        using BufferedSerial = Utils::BufferedSerial;
        BufferedSerial& _serial;

        void _draw_header();
        void _draw_menu();
    public:
        // TODO: implement log-entry rendering into the shell page
        void flush_log_entry(PlcIO::LogEntry_t* entry);
    };

}}

#endif // PAGES_SHELL_H