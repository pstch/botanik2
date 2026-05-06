#ifndef PAGES_STATUS_H
#define PAGES_STATUS_H

#include "libbotanik/pages/page.h"
#include "libbotanik/utils/serial.h"

namespace Botanik { namespace Pages {

    class StatusPage : public Page {
    private:
        using BufferedSerial = Utils::BufferedSerial;
        BufferedSerial& _serial;

        void _draw_header();
        void _draw_menu();

        void draw();
    };

}}

#endif // PAGES_STATUS_H