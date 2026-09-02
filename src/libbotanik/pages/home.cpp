/**
 * @file home.cpp
 * @brief HomePage implementation - identity, uptime, home fields, log tail.
 */
#include "libbotanik/pages/home.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_HOME_PAGE

#include <cstdio>
#include <cstring>

#include "libbotanik/hal/platform.h"

namespace Botanik { namespace Pages {

    uint16_t HomePage::log_band_top() const {
        const uint16_t rows = _console.rows();
        // The band is bounded below by the field grid AND by the " LOGS "
        // divider that labels it, which takes the row above the band. Clamping
        // to PAYLOAD_ROW alone put the divider on the last row of readings and
        // the band over the two below it - and since the labels are written
        // only on focus(), they never came back.
        const uint16_t first = (uint16_t)(PAYLOAD_ROW + 1);
        if (rows <= first) return 0;      // no room for a divider and a line
        uint16_t top = (rows > Config::HOME_SCROLLBACK_LINES)
                     ? (uint16_t)(rows - Config::HOME_SCROLLBACK_LINES) : first;
        if (top < first) top = first;     // a short screen gets a shorter band
        return top;
    }

    void HomePage::draw_logo() {
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

    void HomePage::draw_id_block() {
        char vbuf[48];
        snprintf(vbuf, sizeof(vbuf), "v%s", Config::FIRMWARE_VERSION);
        const char* const labels[3] = { "Site name", "Version", "Commit" };
        const char* const values[3] = { Config::SITE_NAME, vbuf, BOTANIK_GIT_HASH };

        // col of the space before ":"
        static const uint16_t COLON_AT = 16;
        static const uint16_t SEP = 3;         // " : " separator width

        // Fixed value-field width = widest value, the commit reserving
        // "-dirty", so clean and dirty builds share one right edge.
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

    void HomePage::draw_log_header(uint16_t row) {
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

    void HomePage::draw_static() {
        draw_logo();
        draw_id_block();
        _label(UPTIME_ROW, "Uptime");
        // Site home-field labels (three columns).
        for (uint8_t i = 0; i < _field_count; i++) {
            uint16_t row, lbl, val, w, slot;
            if (!_field_place(i, row, lbl, val, w, slot)) break;
            _label_at(row, lbl, _fields[i].label);
        }
        // Divider above the bottom log band (band rows are painted from the
        // scrollback, see focus()/tick()).
        const uint16_t band_top = log_band_top();
        if (band_top != 0) draw_log_header((uint16_t)(band_top - 1));
    }

    void HomePage::draw_dynamic() {
        // Cold fields only: the uptime counter is hot (see draw_hot()).
        // No refs means bring-up has not resolved the names, so there is
        // nothing to read: leave the labels bare rather than have every row
        // show the first variable.
        if (_io == nullptr || _refs == nullptr) return;
        for (uint8_t i = 0; i < _field_count; i++) {
            uint16_t row, lbl, val, w, slot;
            if (!_field_place(i, row, lbl, val, w, slot)) break;
            Vars::Var h = _io->var(_refs[i]);
            char vb[32], buf[40];
            const char* s = Vars::var_format(*_io, h, vb, sizeof(vb));
            const char* u = h.meta()->unit;
            if (u && u[0]) snprintf(buf, sizeof(buf), "%s %s", s, u);
            else           snprintf(buf, sizeof(buf), "%s", s);
            _cell(slot, row, val, buf, w);
        }
    }

    void HomePage::draw_hot() {
        char buf[40];
        fmt_uptime(buf, sizeof(buf), HAL::Clock::uptime_ms());
        // bounded (see draw_dynamic)
        _cell(UPTIME_SLOT, UPTIME_ROW, UPTIME_VAL, buf, UPTIME_W, false);
    }

    void HomePage::focus() {
        MetricPage::focus();                 // logo, readings, divider
        _log_active = true;
        // Stage the SAME band tick() drains into. Spanning the whole content
        // area instead bottom-aligned the replay against the last row and so
        // started it above the divider whenever the ring was full.
        const uint16_t band_top = log_band_top();
        if (band_top != 0)
            _scrollback.begin_redraw(band_top,
                                     (uint16_t)(_console.rows() - band_top));
    }

    void HomePage::tick(bool active) {
        // logo/readings (paced) refresh
        MetricPage::tick(active);
        _log_active = active;
        // Staged focus replay first, and bail until it finishes: the live
        // band's DL scroll must not shift rows the replay already drew.
        if (_scrollback.tick_redraw_gate(_terminal, active)) return;
        // Bank queued entries, then paint them paced into the bottom band.
        drain();
        const uint16_t band_top = log_band_top();
        if (active && band_top != 0) {
            _scrollback.drain_tail(_terminal, band_top,
                                   (uint16_t)(_console.rows() - band_top));
        }
    }

    size_t HomePage::write(const char* buf, size_t len) {
        // Store-only: bank the entry, mark pending when active for the paced
        // drain_tail. Never blocks or drops, so report it all consumed.
        _scrollback.ingest(buf, len, (uint16_t)_terminal.cols(),
            Logs::LOG_CONT_PREFIX,
            (uint16_t)strlen(Logs::LOG_CONT_PREFIX), _log_active);
        return len;
    }
}}

#endif // BOTANIK_FEATURE_HOME_PAGE
