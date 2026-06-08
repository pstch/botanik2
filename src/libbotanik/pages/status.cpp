/**
 * @file status.cpp
 * @brief StatusPage implementation — minimal extensible status-page base: a
 *        static scaffold drawn on focus()/redraw() and a periodic dynamic
 *        refresh, with hooks for derived classes.
 */
#include <cstring>

#include "libbotanik/hal/platform.h"
#include "libbotanik/pages/status.h"

namespace Botanik { namespace Pages {

    StatusPage::StatusPage(AnsiTerminal& terminal) : _terminal(terminal) {}

    void StatusPage::draw_static() {
        // Content starts at row 1; row 0 belongs to the terminal status bar.
        const uint16_t row = 1;
        if (row >= _terminal.rows()) return;
        _terminal.move_to({row, 2});
        const char* hdr = "System status";
        _terminal.write_chars(hdr, (uint16_t)strlen(hdr));
        _terminal.clear_to_eol();
    }

    void StatusPage::focus() {
        _active = true;
        draw_static();   // scaffold (redrawn on every focus()/redraw())
        draw_dynamic();  // initial fill of the periodic metrics
    }

    void StatusPage::tick(bool active) {
        _active = active;
        if (!active) return;
        uint64_t now = HAL::uptime_ms();
        if (now - _refresh_last_ms >= Config::STATUS_REFRESH_MS) {
            _refresh_last_ms = now;
            draw_dynamic();
        }
    }

    void StatusPage::handle_text(char) {}
    void StatusPage::handle_command(Command, uint16_t) {}
    void StatusPage::deactivate() { _active = false; }

}}
