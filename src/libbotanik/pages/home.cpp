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

    void HomePage::draw_static() {
        draw_logo();
        draw_id_block();
        _label(UPTIME_ROW, "Uptime");
        // Site home-field labels (three columns).
        for (uint8_t i = 0; i < _field_count; i++) {
            uint16_t row, lbl, val, w;
            if (!_field_place(i, row, lbl, val, w)) break;
            _label_at(row, lbl, _fields[i].label);
        }
    }

    void HomePage::draw_dynamic() {
        // Cold fields only: the uptime counter is hot (see draw_hot()).
        // No refs means bring-up has not resolved the names, so there is
        // nothing to read: leave the labels bare rather than have every row
        // show the first variable.
        if (_io == nullptr || _refs == nullptr) return;
        for (uint8_t i = 0; i < _field_count; i++) {
            uint16_t row, lbl, val, w;
            if (!_field_place(i, row, lbl, val, w)) break;
            Vars::Var h = _io->var(_refs[i]);
            char vb[32], buf[40];
            const char* s = Vars::var_format(*_io, h, vb, sizeof(vb));
            const char* u = h.meta()->unit;
            if (u && u[0]) snprintf(buf, sizeof(buf), "%s %s", s, u);
            else           snprintf(buf, sizeof(buf), "%s", s);
            _cell(row, val, buf, w);
        }
    }

    void HomePage::draw_hot() {
        char buf[40];
        fmt_uptime(buf, sizeof(buf), HAL::Clock::uptime_ms());
        // bounded (see draw_dynamic)
        _cell(UPTIME_ROW, UPTIME_VAL, buf, UPTIME_W, false);
    }

}}

#endif // BOTANIK_FEATURE_HOME_PAGE
