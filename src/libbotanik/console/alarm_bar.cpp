// REVIEW_35: TODO
/**
 * @file alarm_bar.cpp
 * @brief AlarmBar implementation - bottom-row unmasked-alarm strip.
 */
#include <cstdint>
#include <cstring>

#include "libbotanik/console/alarm_bar.h"
#include "libbotanik/console/console.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/alarms/alarms.h"

namespace Botanik { namespace Terminal {

    AlarmBar::AlarmBar(Console& console)
        : _c(console), _t(console.terminal()) {
        // The strip frames the page from the bottom row, so page rows above it
        // would all go first on a strict top-down walk. Chrome outranks the page
        // to stop that; the set changes rarely enough that it costs the page
        // nothing.
        _t.set_row_priority(_row(), PRIO_CHROME);
    }

    uint16_t AlarmBar::_row() const {
        uint16_t rows = _t.rows_total();
        return rows > 0 ? (uint16_t)(rows - 1) : 0;
    }

    uint64_t AlarmBar::unmasked_set() const {
        uint64_t m = 0;
        const Alarms::AlarmModel* alarms = _c.alarms();
        if (alarms)
            for (uint8_t i = 0; i < alarms->count(); i++) {
                const Alarms::AlarmCondition& c = alarms->condition(i);
                // An acknowledged alarm drops off the bar (still active, just no
                // longer nagging); it re-appears if the condition clears and
                // fires again, which resets the ack. The set is the bar's dirty
                // signal, so an ack repaints it.
                if (c.active && !c.user_ack) m |= Alarms::alarm_bit(i);
            }
        return m;
    }

    void AlarmBar::draw() {
        const Alarms::AlarmModel* alarms = _c.alarms();
        if (alarms == nullptr || _t.rows_total() == 0 || _t.cols() == 0) return;
        // physical last row (reserved for us)
        uint16_t row  = _row();
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
            if (!c.active || c.user_ack || c.code == nullptr) continue;
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
        if (_c.alarms() == nullptr) return;
        uint64_t sig = unmasked_set();
        if (sig != _sig) _dirty = true;
        if (!_dirty) return;
        // Redraw the whole bar immediate-mode; the terminal diff pares the bytes.
        draw();
        _sig   = sig;
        _dirty = false;
    }

}}
