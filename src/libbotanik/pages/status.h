#ifndef PAGES_STATUS_H
#define PAGES_STATUS_H

/**
 * @file status.h
 * @brief StatusPage — full-screen system status display.
 */

#include "libbotanik/pages/page.h"
#include "libbotanik/utils/serial.h"

namespace Botanik { namespace Pages {

    /** @brief Full-screen system status display. */
    class StatusPage : public Page {
    private:
        using BufferedSerial = Utils::BufferedSerial;
        BufferedSerial& _serial;

        /** @brief Draw the page header row. */
        void _draw_header();
        /** @brief Draw the status body. */
        void _draw_menu();

        /** @brief Render the full status screen. */
        void draw();
    };

}}

#endif // PAGES_STATUS_H