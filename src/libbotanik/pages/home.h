// REVIEW_35: TODO
#ifndef PAGES_HOME_H
#define PAGES_HOME_H

/**
 * @file home.h
 * @brief HomePage - landing screen: ASCII logo, a compact system summary, and
 *        a small live log tail along the bottom.
 *
 * Site-independent: it shows the logo, the firmware identity block, the PLC
 * link status and uptime, and the log tail - nothing from the site payload. A
 * site adds nothing here (per-site readings live on the MONITOR page). Drawing
 * is diffed by MetricPage.
 *
 * Also a LogWriter: the bottom HOME_SCROLLBACK_LINES rows show a live tail (no
 * scroll-up), replayed from a small HomeScrollback. begin_redraw() over the
 * whole content area bottom-aligns the lines, so the band needs no bespoke
 * render.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../../libbotanik_config.h"
#include "../hal/platform.h"
#include "../logs/format.h"
#include "../logs/writer.h"
#include "metric_page.h"
#include "scrollback.h"
#include "../core/scalar.h"   // HomeField / scalar_format

// Build-stamped git short hash (generated header); "unknown" when absent
// (Arduino .ino deploy doesn't run CMake).
#if defined(__has_include) && __has_include("botanik_gitversion.h")
#  include "botanik_gitversion.h"
#endif
#ifndef BOTANIK_GIT_HASH
#  define BOTANIK_GIT_HASH "unknown"
#endif

namespace Botanik { namespace Pages {

    /** @brief Landing page (see file header). */
    class HomePage : public MetricPage, public Logs::BufferedLogWriter {
    protected:
        /** @name State
         *  @{ */

        /** @brief Bottom log tail: most recent lines, bottom-aligned on the
         *         screen. */
        HomeScrollback _scrollback;
        /** @brief Mirrors tick(active); gates band repaints in write(). */
        bool _log_active = false;

        /** @brief Site home-field table (label + status variable) + the payload
         *  they read. Up to LEFT_MAX + RIGHT_MAX shown; extras ignored. */
        const HomeField* _fields      = nullptr;
        uint8_t                   _field_count = 0;
        const void*               _base        = nullptr;

        /** @} */

        /** @name Home-field two-column layout
         *  Left column: LEFT_MAX rows from LEFT_TOP; right column: RIGHT_MAX rows
         *  from RIGHT_TOP (below the identity block). Labels left-justified,
         *  values in a fixed field so columns stay put.
         *  @{ */
        static constexpr uint8_t  LEFT_MAX  = 5,  RIGHT_MAX = 8;
        // Published as Config::HOME_FIELDS_MAX for generated home-field tables to
        // static_assert against: extras past the two columns are silently ignored
        // by _slot(), so a site must be able to catch that at build time.
        static_assert(LEFT_MAX + RIGHT_MAX == Config::HOME_FIELDS_MAX,
                      "Config::HOME_FIELDS_MAX must track the two columns' capacity");
        static constexpr uint16_t LEFT_TOP  = 10, RIGHT_TOP = 7;
        static constexpr uint16_t L_LBL = 2,  L_VAL = 15, L_W = 22;
        static constexpr uint16_t R_LBL = 42, R_VAL = 55, R_W = 22;
        /** @} */

        /** @brief First content row below the fixed identity block. */
        static constexpr uint16_t PAYLOAD_ROW = 10;

        /** @brief First screen row of the bottom log band. Clamped so it never
         *  starts above the fixed content (logo + summary); on a too-short
         *  screen the band collapses to empty (band_top == rows). */
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

        /** @brief Place site home-field @p i: its label row + column, value
         *  column + width, and diff-cache slot. @return false past the two
         *  columns' capacity (LEFT_MAX + RIGHT_MAX). Slots 10..14 (left) and
         *  15..22 (right) are disjoint from the base's 7/8. */
        bool _field_place(uint8_t i, uint16_t& row, uint16_t& lbl, uint16_t& val,
                          uint16_t& w, uint16_t& slot) const {
            if (i < LEFT_MAX) {
                row = (uint16_t)(LEFT_TOP + i); lbl = L_LBL; val = L_VAL; w = L_W;
                slot = (uint16_t)(10 + i);
            } else {
                uint8_t r = (uint8_t)(i - LEFT_MAX);
                if (r >= RIGHT_MAX) return false;
                row = (uint16_t)(RIGHT_TOP + r); lbl = R_LBL; val = R_VAL; w = R_W;
                slot = (uint16_t)(15 + r);
            }
            return true;
        }

        void draw_static() override {
            draw_logo();
            draw_id_block();
            _label(8, "Uptime");
            // Site home-field labels (two columns).
            for (uint8_t i = 0; i < _field_count; i++) {
                uint16_t row, lbl, val, w, slot;
                if (!_field_place(i, row, lbl, val, w, slot)) break;
                _label_at(row, lbl, _fields[i].label);
            }
            // Divider above the bottom log band (band rows are painted from the
            // scrollback, see focus()/tick()).
            uint16_t band_top = log_band_top();
            if (band_top >= 2) draw_log_header((uint16_t)(band_top - 1));
        }

        void draw_dynamic() override {
            // Cold fields only: the uptime counter is hot (see draw_hot()).
            // Site home-field values (formatted off the live payload + unit).
            if (_base == nullptr) return;
            for (uint8_t i = 0; i < _field_count; i++) {
                uint16_t row, lbl, val, w, slot;
                if (!_field_place(i, row, lbl, val, w, slot)) break;
                const ScalarField& f = _fields[i].var;
                char vb[32], buf[40];
                const char* s = scalar_format(f, _base, vb, sizeof(vb));
                if (f.unit && f.unit[0]) snprintf(buf, sizeof(buf), "%s %s", s, f.unit);
                else                     snprintf(buf, sizeof(buf), "%s", s);
                _cell(slot, row, val, buf, w);
            }
        }

        /** @brief Uptime counter - hot field, refreshed every second,
         *  un-highlighted (it changes every tick). */
        void draw_hot() override {
            char buf[40];
            fmt_uptime(buf, sizeof(buf), HAL::Clock::uptime_ms());
            _cell(8, 8, L_VAL, buf, L_W, false);   // bounded (see draw_dynamic)
        }

        /** @} */

    public:
        /** @brief @p fields (+ @p count) is the site home-field table and @p base
         *  the payload it reads (all borrowed; may be null/0 for a bare home
         *  screen). */
        HomePage(Console& console, const HomeField* fields = nullptr,
                 uint8_t count = 0, const void* base = nullptr)
            : MetricPage(console, 14),
              _fields(fields), _field_count(count), _base(base) {
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

        size_t format(const Logs::LogEntry* entry, char* buf, size_t len) override {
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
