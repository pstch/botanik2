#ifndef PAGES_HOME_H
#define PAGES_HOME_H

/**
 * @file home.h
 * @brief HomePage - landing screen (template base): ASCII logo, a compact
 *        system summary, and a small live log tail along the bottom.
 *
 * Templated on the site PLC output payload so derived pages get type-safe access
 * via data(). The base lays out the logo and the payload-independent summary (PLC
 * link, uptime); derived classes add site readings via the payload hooks. Drawing
 * is diffed by MetricPage; row 0 is the status bar.
 *
 * It is also a LogWriter: the bottom HOME_SCROLLBACK_LINES rows show a live tail
 * (no scroll-up), replayed from a small HomeScrollback. begin_redraw() over the
 * whole content area bottom-aligns the lines, so the band needs no bespoke render.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/logs/format.h"
#include "libbotanik/logs/writer.h"
#include "libbotanik/pages/metric_page.h"
#include "libbotanik/pages/scrollback.h"

namespace Botanik { namespace Pages {

    /** @brief Landing page base (see file header). */
    template<typename OutputData>
    class HomePage : public MetricPage, public Logs::BufferedLogWriter {
    protected:
        /** @name State
         *  @{ */

        /** @brief Typed PLC output payload (may be null). */
        const OutputData* _data;
        /** @brief Bottom log tail: most recent lines, bottom-aligned on the screen. */
        HomeScrollback _scrollback;
        /** @brief Mirrors the latest tick(active); gates band repaints in write(). */
        bool _log_active = false;

        /** @} */

        /** @brief First content row available to derived payload rendering. */
        static constexpr uint16_t PAYLOAD_ROW = 11;

        /** @brief First screen row of the bottom log band. */
        uint16_t log_band_top() const {
            uint16_t rows = _terminal.rows();
            return (rows > Config::HOME_SCROLLBACK_LINES)
                       ? (uint16_t)(rows - Config::HOME_SCROLLBACK_LINES) : 1;
        }

        /** @name Draw hooks - override to customize the page.
         *  @{ */

        /** @brief Draw the ASCII logo / banner block. */
        virtual void draw_logo() {
            // Content starts at row 1; row 0 belongs to the terminal status bar.
            static const char* const LOGO[] = {
                "  _           _              _ _   ",
                " | |__   ___ | |_ __ _ _ __ (_) | __",
                " | '_ \\ / _ \\| __/ _` | '_ \\| | |/ /",
                " | |_) | (_) | || (_| | | | | |   < ",
                " |_.__/ \\___/ \\__\\__,_|_| |_|_|_|\\_\\  v0.13.12",
            };
            uint16_t row = 1;
            for (const char* line : LOGO) {
                if (row >= _terminal.rows()) return;
                _label(row, line);   // col 2 + clear-to-eol; paced via _static_step
                row++;
            }
        }

        /** @brief Draw the divider above the bottom log band: a reverse-video bar
         *  with a normal-video " LOGS " tab near the left. Static (drawn once). */
        void draw_log_header(uint16_t row) {
            if (row >= _terminal.rows()) return;
            if (!_static_step()) return;     // paced as one static element
            uint16_t cols = _terminal.cols();

            // A run of spaces to paint the reverse-video background in one write.
            char spaces[96];
            uint16_t fill = (cols > (uint16_t)sizeof(spaces)) ? (uint16_t)sizeof(spaces) : cols;
            memset(spaces, ' ', fill);

            static const char TAB[] = " LOGS ";
            const uint16_t    lead   = 3;                          // inverted pad before the tab
            const uint16_t    tablen = (uint16_t)(sizeof(TAB) - 1);

            _terminal.move_to({row, 0});
            // Leading reverse-video pad, then the normal-video tab.
            _terminal.set_inverted(true);
            _terminal.write_chars(spaces, lead);
            _terminal.set_inverted(false);
            _terminal.write_chars(TAB, tablen);
            // Reverse-video fill to the end of the line.
            _terminal.set_inverted(true);
            uint16_t used = (uint16_t)(lead + tablen);
            uint16_t rest = (cols > used) ? (uint16_t)(cols - used) : 0;
            if (rest > fill) rest = fill;
            _terminal.write_chars(spaces, rest);
            _terminal.set_inverted(false);
        }

        void draw_static() override {
            draw_logo();
            _label(8, "PLC link");
            _label(9, "Uptime");
            draw_payload_static();
            // Divider above the bottom log band (the band rows themselves are
            // painted from the scrollback, see focus()/tick()).
            uint16_t band_top = log_band_top();
            if (band_top >= 2) draw_log_header((uint16_t)(band_top - 1));
        }

        void draw_dynamic() override {
            // Cold fields only: the uptime counter is hot (see draw_hot()).
            const SystemContext& ctx = _terminal.context();
            _value(8, ctx.plc ? plc_str(ctx.plc->status()) : "n/a");
            draw_payload_dynamic();
        }

        /** @brief Uptime counter - hot field, refreshed once per second. Drawn
         *  un-highlighted since it changes every tick (see _field()). */
        void draw_hot() override {
            char buf[40];
            fmt_uptime(buf, sizeof(buf), HAL::uptime_ms());
            _value(9, buf, false);
        }

        /** @brief Site-specific labels/headers (drawn once). Base: no-op. */
        virtual void draw_payload_static() {}
        /** @brief Site-specific values (refreshed periodically). Base: no-op. */
        virtual void draw_payload_dynamic() {}

        /** @brief Typed PLC output payload for derived rendering (may be null). */
        const OutputData* data() const { return _data; }

        /** @} */

    public:
        /** @brief Construct a HomePage bound to a terminal and PLC payload. */
        HomePage(AnsiTerminal& terminal, const OutputData* data)
            : MetricPage(terminal, 14), _data(data) {
            _refresh_ms = Config::HOME_REFRESH_MS;   // cold-field redraw cadence
            _hot_ms     = Config::PAGE_HOT_MS;       // uptime ticks every second
        }

        const char* name() const override { return "HOME"; }

        /** @name Focus & update (also drive the log band)
         *  @{ */

        void focus() override {
            MetricPage::focus();                 // logo, readings, divider
            _log_active = true;
            // Stage the bottom band (drained by tick()); spanning the whole content
            // area bottom-aligns the lines against the last row.
            _scrollback.begin_redraw((uint16_t)(_terminal.rows() - 1));
        }

        void deactivate() override {
            MetricPage::deactivate();
            _log_active = false;
        }

        void tick(bool active) override {
            MetricPage::tick(active);            // logo/readings (paced) refresh
            _log_active = active;
            if (active) {
                _scrollback.drain_redraw(_terminal);
                // Hold off the live band until the staged replay finishes - a DL
                // scroll would shift rows the replay already drew.
                if (_scrollback.redraw_pending()) return;
            }
            // Bank queued entries into the scrollback (marks pending when active), then
            // paint the pending lines, paced, into the bottom log band.
            drain();
            if (active) {
                uint16_t band_top = log_band_top();
                _scrollback.drain_tail(_terminal, band_top,
                                       (uint16_t)(_terminal.rows() - band_top));
            }
        }

        /** @} */

    private:
        /** @name BufferedLogWriter interface
         *  @{ */

        size_t format(const PlcIO::LogEntry_t* entry, char* buf, size_t len) override {
            return Logs::format_log_entry(entry, buf, len);
        }

        size_t write(const char* buf, size_t len) override {
            // Store-only: bank every entry into the scrollback and, when active, mark
            // the new row(s) pending so tick()'s drain_tail paints them paced into the
            // bottom band. In the background a later focus() replays the tail. Never
            // blocks, never drops, so always report the whole entry consumed.
            uint16_t n = _scrollback.store_wrapped(buf, len, (uint16_t)_terminal.cols(),
                Config::SHELL_LOG_CONT_PREFIX,
                (uint16_t)strlen(Config::SHELL_LOG_CONT_PREFIX));
            if (_log_active) _scrollback.note_pending(n);
            return len;
        }

        /** @} */
    };

}}

#endif // PAGES_HOME_H
