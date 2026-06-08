#ifndef PAGES_STATUS_H
#define PAGES_STATUS_H

/**
 * @file status.h
 * @brief StatusPage — full-screen runtime status display.
 *
 * Extensible scaffolding base: the layout splits into a *static* part drawn once
 * on focus()/redraw() and a *dynamic* part refreshed periodically while active.
 * Derived classes override draw_static()/draw_dynamic() to fill in the actual
 * metrics. All screen access goes through AnsiTerminal; row 0 is the status bar.
 */

#include <cstdint>

#include "libbotanik_config.h"
#include "libbotanik/pages/page.h"
#include "libbotanik/terminal/terminal.h"

namespace Botanik { namespace Pages {

    /** @brief Full-screen status display base (see file header). */
    class StatusPage : public Page {
    protected:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Command      = Terminal::InputEvent::Command;

        AnsiTerminal& _terminal;

        bool     _active          = false;
        uint64_t _refresh_last_ms = 0;   ///< last dynamic refresh (uptime ms)

        /** @brief Draw the parts that only change on (re)entry/resize (labels,
         *  section headers). Override to extend. */
        virtual void draw_static();
        /** @brief Refresh the periodically-updated metrics. Base: no-op. */
        virtual void draw_dynamic() {}

    public:
        explicit StatusPage(AnsiTerminal& terminal);

        // Page interface
        const char* name() const override { return "STATUS"; }
        void handle_text(char c) override;
        void handle_command(Command cmd, uint16_t count = 1) override;
        void deactivate() override;
        void focus() override;
        void tick(bool active) override;
    };

}}

#endif // PAGES_STATUS_H
