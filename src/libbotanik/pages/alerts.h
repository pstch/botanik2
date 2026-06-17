#ifndef PAGES_ALERTS_H
#define PAGES_ALERTS_H

/**
 * @file alerts.h
 * @brief AlertsPage - scrollable table of active alert conditions.
 *
 * Two rows of header (title + counts, then column labels) sit at the top; the
 * table fills the rest and scrolls through a shared Scrollback (UP/DOWN and
 * Ctrl+P/Ctrl+N, a line at a time). SPACE toggles between two modes: the
 * pre-masked set (active and unmasked, the same set the StatusPage ribbon shows)
 * and all active alerts (masked or not). Columns: code, ON-notified, OFF-notified,
 * description. The table is rebuilt only when the alert set or its notification
 * flags change, so a scrolled view stays put.
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/alerts/alerts.h"
#include "libbotanik/pages/page.h"
#include "libbotanik/pages/scrollback.h"
#include "libbotanik/terminal/terminal.h"

namespace Botanik { namespace Pages {

    /** @brief Full-screen scrollable alert table (see file header). */
    class AlertsPage : public Page {
    private:
        using AnsiTerminal = Terminal::AnsiTerminal;
        using Command      = Terminal::InputEvent::Command;

        /** @name State
         *  @{ */

        /** @brief Owning terminal. */
        AnsiTerminal&            _terminal;
        /** @brief Alert source (may be null). */
        const Alerts::AlertModel* _model;
        /** @brief Formatted table rows, reused for scroll storage. */
        AlertsScrollback         _sb;
        /** @brief Mirrors the latest tick(active). */
        bool                     _active = false;
        /** @brief Mode: false = unmasked only, true = all active. */
        bool                     _show_all = false;
        /** @brief Last built signatures, to rebuild only when the set/flags change. */
        uint64_t                 _last_raw = 0;
        uint64_t                 _last_on  = 0;
        uint64_t                 _last_off = 0;
        bool                     _built    = false;

        /** @} */

        /** @brief Two header rows; the table starts below them. */
        static constexpr uint16_t TABLE_TOP = 3;

        /** @brief Visible table rows (rows TABLE_TOP..rows-1). */
        uint16_t _window() const {
            uint16_t rows = _terminal.rows();
            return (rows > TABLE_TOP) ? (uint16_t)(rows - TABLE_TOP) : 1;
        }

        /** @brief True if the active set or any ON/OFF flag changed since the last
         *  build (so a steady view is left untouched, scroll position preserved). */
        bool _changed() {
            if (_model == nullptr) return false;
            uint64_t on = 0, off = 0;
            for (uint8_t i = 0; i < _model->count(); i++) {
                const Alerts::AlertCondition& c = _model->condition(i);
                if (!c.active) continue;
                if (c.on_notified)  on  |= Alerts::alert_bit(i);
                if (c.off_notified) off |= Alerts::alert_bit(i);
            }
            return !_built || _model->raw() != _last_raw ||
                   on != _last_on || off != _last_off;
        }

        /** @brief Rebuild the table rows from the model into the scrollback. */
        void _build() {
            _sb.clear();
            if (_model != nullptr) {
                char buf[Config::SCROLLBACK_LINE_LEN];
                uint64_t on = 0, off = 0;
                for (uint8_t i = 0; i < _model->count(); i++) {
                    const Alerts::AlertCondition& c = _model->condition(i);
                    if (!c.active) continue;
                    if (c.on_notified)  on  |= Alerts::alert_bit(i);
                    if (c.off_notified) off |= Alerts::alert_bit(i);
                    if (!_show_all && !c.unmasked) continue;   // masked: hidden in this mode
                    int n = snprintf(buf, sizeof(buf), "%-9s %-3s %-3s %s",
                                     c.code ? c.code : "?",
                                     c.on_notified  ? "Y" : "-",
                                     c.off_notified ? "Y" : "-",
                                     c.desc ? c.desc : "");
                    if (n < 0) n = 0;
                    _sb.store(buf, std::min((size_t)n, sizeof(buf) - 1));
                }
                _last_raw = _model->raw();
                _last_on  = on;
                _last_off = off;
            }
            _built = true;
        }

        /** @brief Paint the two header rows (title + counts, then column labels). */
        void _draw_header() {
            char buf[80];
            const char* mode = _show_all ? "ALL ACTIVE" : "UNMASKED";
            unsigned shown  = _sb.count();
            unsigned active = _model ? _model->active_count() : 0;
            snprintf(buf, sizeof(buf), "ALERTS  [%s]  %u shown / %u active",
                     mode, shown, active);
            _terminal.move_to({1, 0});
            _terminal.write_chars(buf, (uint16_t)strlen(buf));
            _terminal.clear_to_eol();

            snprintf(buf, sizeof(buf), "%-9s %-3s %-3s %s",
                     "CODE", "ON", "OFF", "DESCRIPTION");
            _terminal.move_to({2, 0});
            _terminal.write_chars(buf, (uint16_t)strlen(buf));
            _terminal.clear_to_eol();
        }

        /** @brief Rebuild rows + header and stage a fresh table replay. */
        void _rebuild() {
            _build();
            _draw_header();
            _sb.begin_redraw(_window(), TABLE_TOP);
        }

    public:
        /** @brief Bind to a terminal and alert model. */
        AlertsPage(AnsiTerminal& terminal, const Alerts::AlertModel* model)
            : _terminal(terminal), _model(model) {}

        /** @name Page interface
         *  @{ */

        const char* name() const override { return "ALERTS"; }

        void handle_text(char c) override {
            if (c == ' ') {                 // toggle masked-only / all-active
                _show_all = !_show_all;
                _rebuild();
            }
        }

        void handle_command(Command cmd, uint16_t = 1) override {
            const uint16_t window = _window();
            switch (cmd) {
                case Command::ENTER:
                    _sb.scroll_to_bottom(window);
                    _rebuild();
                    break;
                case Command::UP:
                case Command::CTRL_P:
                    _sb.scroll(_terminal, /*up=*/true, 1, TABLE_TOP, window);
                    break;
                case Command::DOWN:
                case Command::CTRL_N:
                    _sb.scroll(_terminal, /*up=*/false, 1, TABLE_TOP, window);
                    break;
                default: break;
            }
        }

        void deactivate() override { _active = false; }

        void focus() override {
            _active = true;
            _rebuild();   // screen was cleared on switch: header + table from scratch
        }

        void tick(bool active) override {
            _active = active;
            if (!active) return;
            _sb.drain_redraw(_terminal);
            if (_sb.redraw_pending()) return;   // let the staged replay finish first
            // Rebuild only when the set or its flags moved, so a scrolled view holds.
            if (_changed()) _rebuild();
        }

        /** @} */
    };

}}

#endif // PAGES_ALERTS_H
