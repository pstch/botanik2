// REVIEW_35: TODO
/**
 * @file alarm_bar.cpp
 * @brief AlarmBar implementation - bottom-row unmasked-alarm strip.
 */
#include <cstdint>
#include <cstring>

#include "libbotanik/console/alarm_bar.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/alarms/alarms.h"

namespace Botanik { namespace Terminal {

    uint64_t AlarmBar::unmasked_set() const {
        uint64_t m = 0;
        const Alarms::AlarmModel* alarms = _t.alarms();
        if (alarms)
            for (uint8_t i = 0; i < alarms->count(); i++) {
                const Alarms::AlarmCondition& c = alarms->condition(i);
                // An acknowledged alarm drops off the bar (still active, just no
                // longer nagging); it re-appears if the condition clears and
                // fires again, which resets the ack. The set is the bar's dirty
                // signal, so an ack repaints it.
                if (c.unmasked && !c.user_ack) m |= Alarms::alarm_bit(i);
            }
        return m;
    }

    void AlarmBar::draw() {
        const Alarms::AlarmModel* alarms = _t.alarms();
        if (alarms == nullptr || _t.rows_total() == 0 || _t.cols() == 0) return;
        // physical last row (reserved for us)
        uint16_t row  = (uint16_t)(_t.rows_total() - 1);
        uint16_t cols = _t.cols();

        static const char hdr[] = " ALARMS : ";
        uint16_t used = (uint16_t)(sizeof(hdr) - 1);

        _t.move_to({row, 0});
        _t.set_inverted(true);
        _t.write_chars(hdr, used);

        // Unmasked codes in normal video, each followed by an inverted space, so
        // they read as a set against the reverse-video bar (like the active
        // status tab).
        for (uint8_t i = 0; i < alarms->count(); i++) {
            const Alarms::AlarmCondition& c = alarms->condition(i);
            // Skip ack'd alarms - same filter as unmasked_set() (the dirty
            // signal), so the drawn codes match what triggered the repaint.
            if (!c.unmasked || c.user_ack || c.code == nullptr) continue;
            uint16_t clen = (uint16_t)strlen(c.code);
            // out of room on the row
            if ((uint16_t)(used + clen + 1) > cols) break;
            _t.set_inverted(false);
            _t.write_chars(c.code, clen);
            _t.set_inverted(true);
            // inverted separator
            _t.write_chars(" ", 1);
            used = (uint16_t)(used + clen + 1);
        }

        // Reverse-video fill to the end of the row (cursor tracks `used` here,
        // since the gated bar always fits the floor and cannot short-write).
        _t.fill_spaces(cols);
        _t.set_inverted(false);
    }

    void AlarmBar::step() {
        if (_t.alarms() == nullptr) return;
        uint64_t sig = unmasked_set();
        if (sig != _sig) _dirty = true;
        if (!_dirty) return;
        // cooperative: wait for a shallow ring
        if (!_t.tx_below_cold()) return;

        // Save the page cursor and its visibility: the bar paints on the bottom
        // row and must hand the cursor back where the page left it (a visible
        // shell cursor would otherwise blink on the bar). Mirrors the status bar.
        AnsiTerminal::CursorSnapshot snap = _t.save_cursor();
        _t.set_cursor_visible(false);
        draw();
        _t.restore_cursor(snap);
        _sig   = sig;
        _dirty = false;
    }

}}
