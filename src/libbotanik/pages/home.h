#ifndef PAGES_HOME_H
#define PAGES_HOME_H

/**
 * @file home.h
 * @brief HomePage — landing screen with a simple selectable menu.
 */

#include "libbotanik/pages/page.h"
#include "libbotanik/utils/serial.h"

namespace Botanik { namespace Pages {

    /** @brief Landing page with a simple selectable menu. */
    class HomePage : public Page {
    private:
        using BufferedSerial = Utils::BufferedSerial;
        BufferedSerial& _serial;

        /** @brief Currently highlighted menu entry. */
        uint8_t _menu_selection = 0;

        /** @brief Draw the page header row. */
        void _draw_header();
        /** @brief Draw the selectable menu body. */
        void _draw_menu();
    };

}}

#endif // PAGES_HOME_H