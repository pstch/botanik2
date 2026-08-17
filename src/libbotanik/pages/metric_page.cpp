/**
 * @file metric_page.cpp
 * @brief MetricPage implementation - the field/cell drawing helpers.
 */
#include "libbotanik/pages/metric_page.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_CONSOLE

#include <cstdio>
#include <cstring>

#include "libbotanik/hal/platform.h"
#include "libbotanik/utils/assert.h"

namespace Botanik { namespace Pages {

    void MetricPage::_header(uint16_t row, uint16_t col, const char* title, uint16_t width) {
        if (row >= _console.rows()) return;
        char buf[96];
        if (width > (uint16_t)(sizeof(buf) - 1))
            width = (uint16_t)(sizeof(buf) - 1);
        uint16_t n = 0;
        for (int i = 0; i < 4 && n < width; ++i) buf[n++] = '=';
        if (n < width) buf[n++] = ' ';
        for (const char* p = title; *p && n < width; ++p) buf[n++] = *p;
        if (n < width) buf[n++] = ' ';
        while (n < width) buf[n++] = '=';
        _terminal.move_to({row, col});
        _terminal.set_inverted(false);
        _terminal.write_chars(buf, n);
    }

    void MetricPage::_label_at(uint16_t row, uint16_t col, const char* s) {
        if (row >= _console.rows()) return;
        _terminal.move_to({row, col});
        _terminal.set_inverted(false);
        _terminal.write_chars(s, (uint16_t)strlen(s));
    }

    void MetricPage::fmt_uptime(char* buf, size_t cap, uint64_t ms) {
        uint64_t s = ms / 1000;
        unsigned d   = (unsigned)(s / 86400); s %= 86400;
        unsigned h   = (unsigned)(s / 3600);  s %= 3600;
        unsigned m   = (unsigned)(s / 60);
        unsigned sec = (unsigned)(s % 60);
        snprintf(buf, cap, "%03ud %02u:%02u:%02u", d, h, m, sec);
    }

    void MetricPage::tick(bool active) {
        if (!active) return;
        uint64_t now = HAL::Clock::uptime_ms();

        // Fresh screen: draw the static chrome, force every value fresh, then
        // paint hot + cold once.
        if (_repaint_all) {
            draw_static();
            _invalidate();
            draw_hot();
            draw_dynamic();
            _hot_last_ms     = now;
            _refresh_last_ms = now;
            _cold_due        = false;
            _repaint_all     = false;
            return;
        }

        // Hot values (e.g. a clock) on their own cadence; cold values on the
        // refresh period or a forced ENTER. Both diff-gated, so a steady value
        // spends no TX.
        if (_hot_ms && now - _hot_last_ms >= _hot_ms) {
            _hot_last_ms = now;
            draw_hot();
        }
        if (_cold_due || now - _refresh_last_ms >= _refresh_ms) {
            _refresh_last_ms = now;
            _cold_due = false;
            draw_dynamic();
        }
    }
}}

#endif // BOTANIK_FEATURE_CONSOLE
