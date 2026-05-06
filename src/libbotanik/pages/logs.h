#ifndef PAGES_LOGS_H
#define PAGES_LOGS_H

#include "libbotanik/pages/page.h"
#include "libbotanik/utils/serial.h"

namespace Botanik { namespace Pages {

    class LogsPage : public Page {
    private:
        using BufferedSerial = Utils::BufferedSerial;
        BufferedSerial& _serial;

        void _draw_header();
        void _draw_menu();
    };

}}

#endif // PAGES_LOGS_H