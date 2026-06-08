#ifndef PAGES_HOME_H
#define PAGES_HOME_H

/**
 * @file home.h
 * @brief HomePage — landing screen: ASCII logo, system-status block, a
 *        navigation menu, and a reserved region for the most recent logs.
 *
 * Intended as an extensible base: the base lays out the logo, status block and
 * menu (all screen access goes through AnsiTerminal so cursor/scroll tracking
 * stays correct, and row 0 is left to the terminal status bar). Derived classes
 * override the draw hooks to expose site-specific information.
 */

#include <cstdint>

#include "libbotanik_config.h"
#include "libbotanik/pages/page.h"
#include "libbotanik/terminal/terminal.h"

namespace Botanik { namespace Pages {

    /** @brief Landing page base (see file header). */
    class HomePage : public Page {
    protected:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Command      = Terminal::InputEvent::Command;

        AnsiTerminal& _terminal;

        uint8_t  _menu_selection = 0;   ///< highlighted menu entry
        bool     _active         = false;
        uint64_t _status_last_ms = 0;   ///< last periodic status refresh (uptime ms)

        /** @brief Rows reserved at the bottom of the screen for recent log lines. */
        static constexpr uint16_t LOG_REGION_ROWS = 4;

        /** @name Draw hooks — override to customize the page.
         *  @{ */
        /** @brief Draw the ASCII logo / banner block. */
        virtual void draw_logo();
        /** @brief Draw the (static part of the) system-status block. */
        virtual void draw_status();
        /** @brief Draw the navigation menu, highlighting @ref _menu_selection. */
        virtual void draw_menu();
        /** @brief Refresh the periodically-updated status while active. Base: no-op. */
        virtual void refresh_status() {}
        /** @brief Site-specific content drawn after the base layout. Base: no-op. */
        virtual void draw_extra() {}
        /** @brief Number of selectable menu entries. Override to populate the menu. */
        virtual uint8_t menu_count() const { return 0; }
        /** @} */

    public:
        explicit HomePage(AnsiTerminal& terminal);

        // Page interface
        const char* name() const override { return "HOME"; }
        void handle_text(char c) override;
        void handle_command(Command cmd, uint16_t count = 1) override;
        void deactivate() override;
        void focus() override;
        void tick(bool active) override;
    };

}}

#endif // PAGES_HOME_H
