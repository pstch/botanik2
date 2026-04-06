#ifndef PANES_HOME_H
#define PANES_HOME_H

#include "libbotanik/panes/pane.h"
#include "libbotanik/utils/serial.h"

using namespace Botanik::Utils;

namespace Botanik { namespace Panes {

    class HomePane : public Pane {
    private:
        BufferedSerial& _serial;
        
        // Internal state for the home screen (e.g., cursor selection in a menu)
        uint8_t _menu_selection = 0;

        void _draw_header();
        void _draw_menu();
    };

}}

#endif // PANES_HOME_H