// REVIEW_35: DONE
/**
 * @file alarm_bar.cpp
 * @brief AlarmBar implementation - bottom-row unmasked-alarm strip.
 */

#include <cstdint>                          // uint8_t/uint16_t/uint64_t
#include <cstring>                          // strlen

#include "libbotanik/alarms/alarms.h"       // Alarms::AlarmController
#include "libbotanik/console/console.h"     // Console::alarms
#include "libbotanik/console/terminal.h"    // AnsiTerminal
#include "libbotanik/console/alarm_bar.h"   // AlarmBar

namespace Botanik { namespace Terminal {

    AlarmBar::AlarmBar(Console& console)
        : _console(console), _terminal(console.terminal()) {
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
        // fetch alarm controller from the console
        const Alarms::AlarmController* alarms = _console.alarms();

        // bail-out if things look too weird
        if (alarms == nullptr || _terminal.rows_total() == 0 || _terminal.cols() == 0) return;

        // physical last row (reserved for us)
        uint16_t row  = _row();
        uint16_t cols = _terminal.cols();

        // write bar header
        static const char hdr[] = " ALARMS : ";
        uint16_t used = (uint16_t)(sizeof(hdr) - 1);
        _terminal.move_to({row, 0});
        _terminal.set_inverted(true);
        _terminal.write_chars(hdr, used);

        // for each alarm condition...
        for (uint8_t i = 0; i < alarms->count(); i++) {
            const Alarms::AlarmCondition& c = alarms->condition(i);
            // skip ACK'd alarms
            if (!c.active || c.user_ack || c.code == nullptr) continue;
            
            // break if out of room on the row
            uint16_t clen = (uint16_t)strlen(c.code);
            if ((uint16_t)(used + clen + 1) > cols) break;

            // write out the alarm's code
            _terminal.set_inverted(false);
            _terminal.write_chars(c.code, clen);
            _terminal.set_inverted(true);
            
            // inverted separator
            _terminal.write_chars(" ", 1);
            used = (uint16_t)(used + clen + 1);
        }

        // Reverse-video fill to the end of the row
        _terminal.fill_spaces(cols);
        _terminal.set_inverted(false);
    }

    void AlarmBar::step() {
        if (_console.alarms() == nullptr) return;   // bail-out if we have no alarms controller
        uint64_t sig = unmasked_set();              // get the bitset of active alarms
        if (sig != _sig) _dirty = true;             // determine if we need to redraw
        if (!_dirty) return;                        // if not, we just bail out
        draw();                                     // redraw the alarm bar
        _sig   = sig;                               // update bitset of active alarms
        _dirty = false;                             // reset dirty flag
    }

}}
