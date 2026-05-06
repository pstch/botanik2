#ifndef PAGES_HOME_H
#define PAGES_HOME_H

#include "libbotanik/pages/page.h"
#include "libbotanik/utils/serial.h"

namespace Botanik { namespace Pages {

    class HomePage : public Page {
    private:
        using BufferedSerial = Utils::BufferedSerial;
        BufferedSerial& _serial;

        // Internal state for the home screen (e.g., cursor selection in a menu)
        uint8_t _menu_selection = 0;

        void _draw_header();
        void _draw_menu();
    };

}}

#endif // PAGES_HOME_H