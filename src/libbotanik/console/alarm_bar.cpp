// REVIEW_35: DONE
/**
 * @file alarm_bar.cpp
 * @brief AlarmBar implementation - bottom-row unmasked-alarm strip.
 */

#include <cstdint>
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_CONSOLE
#include <cstring>

#include "libbotanik/alarms/alarms.h"
#include "libbotanik/console/console.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/console/alarm_bar.h"

namespace Botanik { namespace Terminal {

    AlarmBar::AlarmBar(Console& console)
        : _console(console), _terminal(console.terminal()) {
        claim_row();
    }

    void AlarmBar::claim_row() {
        _terminal.set_row_priority(_row(), PRIO_CHROME);
    }

    uint16_t AlarmBar::_row() const {
        uint16_t rows = _terminal.rows_total();
        return rows > 0 ? (uint16_t)(rows - 1) : 0;
    }

    uint64_t AlarmBar::unmasked_set() const {
        uint64_t m = 0;
        const Alarms::AlarmController* alarms = _console.alarms();
        if (alarms) for (uint8_t i = 0; i < alarms->count(); i++) {
            const Alarms::AlarmCondition& c = alarms->condition(i);
            if (c.active && !c.user_ack) m |= Alarms::alarm_bit(i);
        }
        return m;
    }

    void AlarmBar::draw() {
        const Alarms::AlarmController* alarms = _console.alarms();
        if (alarms == nullptr || _terminal.rows_total() == 0 || _terminal.cols() == 0) return;

        const uint16_t row  = _row();   // physical last row, reserved for us
        const uint16_t cols = _terminal.cols();

        static const char hdr[] = " ALARMS : ";
        uint16_t used = (uint16_t)(sizeof(hdr) - 1);
        _terminal.move_to({row, 0});
        _terminal.set_inverted(true);
        _terminal.write_chars(hdr, used);

        for (uint8_t i = 0; i < alarms->count(); i++) {
            const Alarms::AlarmCondition& c = alarms->condition(i);
            const char* code = c.code();
            if (!c.active || c.user_ack || code == nullptr) continue;

            const uint16_t clen = (uint16_t)strlen(code);
            if ((uint16_t)(used + clen + 1) > cols) break;   // out of room

            _terminal.set_inverted(false);
            _terminal.write_chars(code, clen);
            _terminal.set_inverted(true);
            _terminal.write_chars(" ", 1);                   // inverted separator
            used = (uint16_t)(used + clen + 1);
        }

        // Reverse-video fill to the end of the row
        _terminal.fill_spaces(cols);
        _terminal.set_inverted(false);
    }

    void AlarmBar::step() {
        if (_console.alarms() == nullptr) return;
        const uint64_t sig = unmasked_set();
        if (sig != _sig) _dirty = true;
        if (!_dirty) return;
        draw();
        _sig   = sig;
        _dirty = false;
    }

}}

#endif // BOTANIK_FEATURE_CONSOLE
