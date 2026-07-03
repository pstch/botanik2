// REVIEW_35: TODO
#ifndef PAGES_HOME_H
#define PAGES_HOME_H

/**
 * @file home.h
 * @brief HomePage - landing screen (template base): ASCII logo, a compact
 *        system summary, and a small live log tail along the bottom.
 *
 * Templated on the site PLC output payload for type-safe data() access. The
 * base lays out the logo and the payload-independent summary (PLC link,
 * uptime); derived classes add site readings via the payload hooks. Drawing is
 * diffed by MetricPage.
 *
 * Also a LogWriter: the bottom HOME_SCROLLBACK_LINES rows show a live tail (no
 * scroll-up), replayed from a small HomeScrollback. begin_redraw() over the
 * whole content area bottom-aligns the lines, so the band needs no bespoke
 * render.
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

// Build-stamped git short hash (generated header); "unknown" when absent
// (Arduino .ino deploy doesn't run CMake).
#if defined(__has_include) && __has_include("botanik_gitversion.h")
#  include "botanik_gitversion.h"
#endif
#ifndef BOTANIK_GIT_HASH
#  define BOTANIK_GIT_HASH "unknown"
#endif

namespace Botanik { namespace Pages {

    /** @brief Landing page base (see file header). */
    template<typename OutputData>
    class HomePage : public MetricPage, public Logs::BufferedLogWriter {
    protected:
        /** @name State
         *  @{ */

        /** @brief Typed PLC output payload (may be null). */
        const OutputData* _data;
        /** @brief Bottom log tail: most recent lines, bottom-aligned on the
         *         screen. */
        HomeScrollback _scrollback;
        /** @brief Mirrors tick(active); gates band repaints in write(). */
        bool _log_active = false;

        /** @} */

        /** @brief First content row available to derived payload rendering. */
        static constexpr uint16_t PAYLOAD_ROW = 10;

        /** @brief First screen row of the bottom log band. Clamped so it never
         *  starts above the fixed content (logo + readings, payload at
         *  PAYLOAD_ROW); on a too-short screen the band collapses to empty
         *  (band_top == rows). */
        uint16_t log_band_top() const {
            uint16_t rows = _console.rows();
            uint16_t top  = (rows > Config::HOME_SCROLLBACK_LINES)
                       ? (uint16_t)(rows - Config::HOME_SCROLLBACK_LINES) : 1;
            if (top < PAYLOAD_ROW) top = (rows > PAYLOAD_ROW) ? PAYLOAD_ROW : rows;
            return top;
        }

        /** @name Draw hooks - override to customize the page.
         *  @{ */

        /** @brief Draw the ASCII logo / banner block. */
        virtual void draw_logo() {
            static const char* const LOGO[] = {
                "  _           _              _ _   ",
                " | |__   ___ | |_ __ _ _ __ (_) | __",
                " | '_ \\ / _ \\| __/ _` | '_ \\| | |/ /",
                " | |_) | (_) | || (_| | | | | |   < ",
                " |_.__/ \\___/ \\__\\__,_|_| |_|_|_|\\_\\",
            };
            uint16_t row = 1;
            for (const char* line : LOGO) {
                if (row >= _console.rows()) return;
                _label(row, line);   // col 2 + clear-to-eol; paced via static
                row++;
            }
        }

        /** @brief Firmware identity block (site name, version, commit),
         *  right-aligned top-right (rows 3-5), clear of the logo. Dot leaders
         *  align the colons. Static (compile-time values). Drawn after the
         *  logo, whose clear-to-eol would wipe it. */
        virtual void draw_id_block() {
            char vbuf[48];
            snprintf(vbuf, sizeof(vbuf), "v%s", Config::FIRMWARE_VERSION);
            const char* const labels[3] = { "Site name", "Version", "Commit" };
            const char* const values[3] = { Config::SITE_NAME, vbuf, BOTANIK_GIT_HASH };

            // col of the space before ":"
            static const uint16_t COLON_AT = 16;
            static const uint16_t SEP = 3;         // " : " separator width

            // Fixed value-field width = widest value, commit reserving
            // "-dirty".
            // Padding to this gives a flush right edge identical for clean and
            // dirty builds.
            static const char DIRTY[] = "-dirty";
            uint16_t vwidth = 0;
            for (int i = 0; i < 3; ++i) {
                uint16_t l = (uint16_t)strlen(values[i]);
                if (i == 2) {                       // commit: reserve "-dirty"
                    uint16_t dl = (uint16_t)(sizeof(DIRTY) - 1);
                    if (l < dl || strcmp(values[i] + (l - dl), DIRTY) != 0)
                        l = (uint16_t)(l + dl);
                }
                if (l > vwidth) vwidth = l;
            }

            char lines[3][64];
            const uint16_t cap = (uint16_t)(sizeof(lines[0]) - 1);
            // trailing pad past the value
            static const uint16_t PAD = 1;
            // full (padded) row length
            uint16_t target = (uint16_t)(COLON_AT + SEP + vwidth + PAD);
            if (target > cap) target = cap;
            for (int i = 0; i < 3; ++i) {
                char* p = lines[i];
                uint16_t n = (uint16_t)snprintf(p, sizeof(lines[i]), "%s ", labels[i]);
                while (n < COLON_AT && n < cap) p[n++] = '.';
                int more = snprintf(p + n, sizeof(lines[i]) - n, " : %s", values[i]);
                n = (uint16_t)(n + (more > 0 ? more : 0));
                if (n > cap) n = cap;
                while (n < target) p[n++] = ' ';    // right-pad to fixed field
                p[n] = '\0';
            }
            // Right-align with a one-column margin; same left column for all
            // three rows keeps the colons aligned.
            uint16_t cols = _terminal.cols();
            uint16_t left = (cols > (uint16_t)(target + 1))
                                ? (uint16_t)(cols - target - 1) : 0;
            for (int i = 0; i < 3; ++i)
                _label_at((uint16_t)(3 + i), left, lines[i]);
        }

        /** @brief Draw the divider above the bottom log band: a reverse-video
         *  bar with a normal-video " LOGS " tab near the left. Static (drawn
         *  once). */
        void draw_log_header(uint16_t row) {
            if (row >= _console.rows()) return;
            if (!_static_step()) return;     // paced as one static element
            uint16_t cols = _terminal.cols();

            // Spaces to paint the reverse-video background in one write.
            char spaces[96];
            uint16_t fill = (cols > (uint16_t)sizeof(spaces))
                                ? (uint16_t)sizeof(spaces) : cols;
            memset(spaces, ' ', fill);

            static const char TAB[] = " LOGS ";
            // inverted pad before the tab
            const uint16_t    lead   = 3;
            const uint16_t    tablen = (uint16_t)(sizeof(TAB) - 1);

            _terminal.move_to({row, 0});
            // Reverse-video pad, normal-video tab, then reverse fill to EOL.
            _terminal.set_inverted(true);
            _terminal.write_chars(spaces, lead);
            _terminal.set_inverted(false);
            _terminal.write_chars(TAB, tablen);
            _terminal.set_inverted(true);
            uint16_t used = (uint16_t)(lead + tablen);
            uint16_t rest = (cols > used) ? (uint16_t)(cols - used) : 0;
            // sanity bound
            if (rest > Config::TERMINAL_MAX_COLS) rest = Config::TERMINAL_MAX_COLS;
            // Chunk so a terminal wider than the local buffer still fills to
            // EOL.
            while (rest) {
                uint16_t n = (rest > fill) ? fill : rest;
                _terminal.write_chars(spaces, n);
                rest -= n;
            }
            _terminal.set_inverted(false);
        }

        void draw_static() override {
            draw_logo();
            draw_id_block();
            _label(7, "PLC link");
            _label(8, "Uptime");
            draw_payload_static();
            // Divider above the bottom log band (band rows are painted from the
            // scrollback, see focus()/tick()).
            uint16_t band_top = log_band_top();
            if (band_top >= 2) draw_log_header((uint16_t)(band_top - 1));
        }

        void draw_dynamic() override {
            // Cold fields only: the uptime counter is hot (see draw_hot()).
#if BOTANIK_FEATURE_PLC
            const SystemContext& ctx = _console.context();
            _value(7, ctx.plc ? plc_str(ctx.plc->status()) : "n/a");
#else
            _value(7, "n/a");
#endif
            draw_payload_dynamic();
        }

        /** @brief Uptime counter - hot field, refreshed every second,
         *  un-highlighted (it changes every tick). */
        void draw_hot() override {
            char buf[40];
            fmt_uptime(buf, sizeof(buf), HAL::Clock::uptime_ms());
            _value(8, buf, false);
        }

        /** @brief Site-specific labels/headers (drawn once). Base: no-op. */
        virtual void draw_payload_static() {}
        /** @brief Site-specific values (refreshed periodically). Base:
         *  no-op. */
        virtual void draw_payload_dynamic() {}

        /** @brief Typed PLC output payload for derived rendering (may be
         *  null). */
        const OutputData* data() const { return _data; }

        /** @} */

    public:
        HomePage(Console& console, const OutputData* data)
            : MetricPage(console, 14), _data(data) {
            // cold-field redraw cadence
            _refresh_ms = Config::HOME_REFRESH_MS;
            // uptime ticks every second
            _hot_ms     = Config::PAGE_HOT_MS;
        }

        const char* name() const override { return "HOME"; }

        /** @name Focus & update (also drive the log band)
         *  @{ */

        void focus() override {
            MetricPage::focus();                 // logo, readings, divider
            _log_active = true;
            // Stage the bottom band (drained by tick()); spanning the content
            // area bottom-aligns the lines against the last row.
            _scrollback.begin_redraw((uint16_t)(_console.rows() - 1));
        }

        void deactivate() override {
            MetricPage::deactivate();
            _log_active = false;
        }

        void tick(bool active) override {
            // logo/readings (paced) refresh
            MetricPage::tick(active);
            _log_active = active;
            // Drain any staged focus replay first; bail until it finishes so the live
            // band's DL scroll can't shift rows the replay already drew.
            if (_scrollback.tick_redraw_gate(_terminal, active)) return;
            // Bank queued entries (pending when active), then paint them paced into the
            // bottom log band.
            drain();
            if (active) {
                uint16_t band_top = log_band_top();
                _scrollback.drain_tail(_terminal, band_top,
                                       (uint16_t)(_console.rows() - band_top));
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
            // Store-only: bank the entry, mark pending when active for the
            // paced drain_tail. Never blocks/drops, so report the whole entry
            // consumed.
            _scrollback.ingest(buf, len, (uint16_t)_terminal.cols(),
                Config::SHELL_LOG_CONT_PREFIX,
                (uint16_t)strlen(Config::SHELL_LOG_CONT_PREFIX), _log_active);
            return len;
        }

        /** @} */
    };

}}

#endif // PAGES_HOME_H
