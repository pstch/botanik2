/**
 * @file home.cpp
 * @brief HomePage implementation — minimal extensible landing-page base:
 *        logo + status block + menu layout, periodic status refresh, and the
 *        draw hooks derived classes override.
 */
#include <cstring>

#include "libbotanik/hal/platform.h"
#include "libbotanik/pages/home.h"

namespace Botanik { namespace Pages {

    HomePage::HomePage(AnsiTerminal& terminal) : _terminal(terminal) {}

    void HomePage::draw_logo() {
        // Content starts at row 1; row 0 belongs to the terminal status bar.
        static const char* const LOGO[] = {
            "  _           _              _ _   ",
            " | |__   ___ | |_ __ _ _ __ (_) | __",
            " | '_ \\ / _ \\| __/ _` | '_ \\| | |/ /",
            " | |_) | (_) | || (_| | | | | |   < ",
            " |_.__/ \\___/ \\__\\__,_|_| |_|_|_|\\_\\",
        };
        uint16_t row = 1;
        for (const char* line : LOGO) {
            if (row >= _terminal.rows()) return;
            _terminal.move_to({row, 2});
            _terminal.write_chars(line, (uint16_t)strlen(line));
            _terminal.clear_to_eol();
            row++;
        }
    }

    void HomePage::draw_status() {
        const uint16_t row = 7;
        if (row >= _terminal.rows()) return;
        _terminal.move_to({row, 2});
        const char* hdr = "System status";
        _terminal.write_chars(hdr, (uint16_t)strlen(hdr));
        _terminal.clear_to_eol();
        refresh_status();
    }

    void HomePage::draw_menu() {
        const uint16_t row = 9;
        if (row >= _terminal.rows()) return;
        _terminal.move_to({row, 2});
        const char* hdr = "Menu";
        _terminal.write_chars(hdr, (uint16_t)strlen(hdr));
        _terminal.clear_to_eol();
        // Entries are supplied by subclasses via menu_count()/draw hooks.
    }

    void HomePage::focus() {
        _active = true;
        draw_logo();
        draw_status();
        draw_menu();
        draw_extra();
    }

    void HomePage::tick(bool active) {
        _active = active;
        if (!active) return;
        uint64_t now = HAL::uptime_ms();
        if (now - _status_last_ms >= Config::STATUS_REFRESH_MS) {
            _status_last_ms = now;
            refresh_status();
        }
    }

    void HomePage::handle_text(char) {}

    void HomePage::handle_command(Command cmd, uint16_t) {
        uint8_t n = menu_count();
        if (n == 0) return;
        switch (cmd) {
            case Command::UP:
                if (_menu_selection > 0) _menu_selection--;
                draw_menu();
                break;
            case Command::DOWN:
                if (_menu_selection + 1 < n) _menu_selection++;
                draw_menu();
                break;
            default:
                break;
        }
    }

    void HomePage::deactivate() { _active = false; }

}}
