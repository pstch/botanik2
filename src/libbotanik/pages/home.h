#ifndef PAGES_HOME_H
#define PAGES_HOME_H

/**
 * @file home.h
 * @brief HomePage - landing screen (template base): ASCII logo, a compact
 *        system summary, and a small live log tail along the bottom.
 *
 * Templated on the site PLC output payload so derived pages get type-safe access
 * to it via data(). The base lays out the logo and the payload-independent
 * summary (PLC link, uptime) from the terminal's SystemContext; derived classes
 * add site-specific readings by overriding the payload hooks. Drawing is split
 * and diffed by MetricPage. Row 0 is the terminal status bar.
 *
 * It is also a LogWriter: the bottom @ref Botanik::Config::HOME_SCROLLBACK_LINES
 * rows show the most recent log lines, replayed from a small
 * @ref Botanik::Pages::HomeScrollback. There is
 * no scroll-up - it is a live tail, not the full Logs page. begin_redraw() over
 * the whole content area bottom-aligns the (few) lines against the screen bottom,
 * so the band needs no bespoke rendering.
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
                _terminal.move_to({row, 2});
                _terminal.write_chars(line, (uint16_t)strlen(line));
                _terminal.clear_to_eol();
                row++;
            }
        }

        /** @brief Draw the divider above the bottom log band: a full-width
         *  reverse-video bar, three blank columns then a normal-video " LOGS " tab,
         *  with the rest of the line filled in reverse video. Static (drawn once on
         *  focus, after the screen is cleared). */
        void draw_log_header(uint16_t row) {
            if (row >= _terminal.rows()) return;
            uint16_t cols = _terminal.cols();
            if (_terminal.tx_available() < (uint16_t)(cols + 16)) return;

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
         *  un-highlighted: it ticks every second, so the change highlight would only
         *  re-emit settled subfields each tick (see _field()). */
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
            // Stage the bottom band; drained incrementally by tick(). Spanning the
            // whole content area bottom-aligns the (<= HOME_SCROLLBACK_LINES) lines
            // against the last row.
            _scrollback.begin_redraw((uint16_t)(_terminal.rows() - 1));
        }

        void deactivate() override {
            MetricPage::deactivate();
            _log_active = false;
        }

        void tick(bool active) override {
            MetricPage::tick(active);            // periodic refresh of the readings
            _log_active = active;
            if (active) {
                _scrollback.drain_redraw(_terminal);
                // Hold off live entries until the staged replay finishes - they
                // would otherwise restage it mid-draw.
                if (_scrollback.redraw_pending()) return;
            }
            // Drain the queue either way: active repaints the band, background just
            // banks entries into the scrollback for a later focus() replay.
            drain();
        }

        /** @} */

    private:
        /** @name BufferedLogWriter interface
         *  @{ */

        size_t format(const PlcIO::LogEntry_t* entry, char* buf, size_t len) override {
            return Logs::format_log_entry(entry, buf, len);
        }

        size_t write(const char* buf, size_t len) override {
            // In the background: just bank the entry so a later focus() replays an
            // up-to-date tail. Nothing reaches the screen, so there's no TX
            // back-pressure to honour - store the whole entry and report it consumed.
            if (!_log_active) {
                _scrollback.store_wrapped(buf, len, (uint16_t)_terminal.cols(),
                    Config::SHELL_LOG_CONT_PREFIX,
                    (uint16_t)strlen(Config::SHELL_LOG_CONT_PREFIX));
                return len;
            }

            // Active: back-pressure first so a burst can't flood the TX buffer.
            // Returning 0 makes BufferedLogWriter hold the entry and retry on a
            // later tick - don't store it yet, or the retry would double-bank it.
            if (!_terminal.tx_empty()) return 0;

            uint16_t n = _scrollback.store_wrapped(buf, len, (uint16_t)_terminal.cols(),
                Config::SHELL_LOG_CONT_PREFIX,
                (uint16_t)strlen(Config::SHELL_LOG_CONT_PREFIX));
            // Scroll the bottom log band up and paint the new line(s) at its bottom,
            // leaving the logo and summary above the band untouched.
            uint16_t band_top = log_band_top();
            _scrollback.emit_tail(_terminal, n, band_top,
                                  (uint16_t)(_terminal.rows() - band_top));
            return len;
        }

        /** @} */
    };

}}

#endif // PAGES_HOME_H
