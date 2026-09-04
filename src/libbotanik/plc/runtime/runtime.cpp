/**
 * @file runtime.cpp
 * @brief HAND-WRITTEN - the join between the beat and the generated ST.
 */

#include "libbotanik/plc/runtime/runtime.h"
#include "libbotanik/config/base.h"
#include "libbotanik/config/scheduler.h"

#include "libbotanik/logs/logger.h"
#include "libbotanik/vars/rows.h"
#include "libbotanik/utils/assert.h"
#include "libbotanik/hal/platform.h"

#if BOTANIK_FEATURE_PLC

namespace Botanik { namespace Plc { namespace Runtime {

    
    void Plant::on_setup(uint64_t now_ms) {
        (void)now_ms;
        wire();
    }

    void Plant::wire() {
        // The one bank every generated read, write and bind reaches.
        Vars::runtime_bank = &_io;
        BOTANIK_ASSERT(PROGRAM_COUNT <=
                       (uint8_t)(sizeof _slot / sizeof _slot[0]));
        // The health row is the LAW's to write, and its declared FAULTED is
        // already what a program that has never run should read. Taken into the
        // cache here so the TASKS page says that too, rather than HEALTHY until
        // the first control beat.
        for (uint8_t i = 0; i < PROGRAM_COUNT; i++) {
            programs[i].init();
            _read_health(i);
        }
    }

    void Plant::register_tasks() {
        for (uint8_t i = 0; i < PROGRAM_COUNT; i++) {
            if (!_tasks->add(*this, programs[i].task, programs[i].name,
                             programs[i].period_ms, Tasks::PHASE_CONTROL))
                continue;
            // The verdict this entry reports, or none where the program did
            // not opt into health. An ST program is not a Plc::Program, so the
            // task keeps a POINTER to where its health lives rather than the
            // object: the TASKS page asks the entry, not the kind of thing
            // behind it.
            _tasks->last().health =
                (programs[i].health_row != Vars::VAR_NONE) ? &_slot[i].health
                                                           : nullptr;
        }
    }

    void Plant::scan(uint8_t i, uint64_t now_ms) {
        if (i >= PROGRAM_COUNT) return;
        _now_ms = now_ms;
        // The CLOCK, before any body reads it. Each block differences it
        // against its own last reading, so one row serves every period.
        _io.var(Vars::Row::sys_now_ms).set_i((int64_t)(uint32_t)now_ms);
        // ...and the DATE beside it. Below the floor the RTC has never been
        // set, so there is no date and 0 is what says so - a schedule counts
        // in these and refuses to run against zero.
        const uint64_t wall = HAL::Clock::wall_ms();
        _io.var(Vars::Row::sys_today).set_i(
            (wall < 1600000000000ull) ? 0 : (int64_t)(wall / 86400000ull));

        // DEFENCE IN DEPTH, at both ends. Before RUN the field has not
        // answered and every row still reads its DECLARED value; after RUN the
        // unit is on its way out and a law that keeps deciding is deciding
        // about a plant nobody is going to act on.
        //
        // The whole BODY is held, not each program's inputs, because every
        // stateful block seeds on its first call - a filter adopts the reading
        // it starts on, a timer datums its clock - so a held program starts
        // clean rather than climbing out of a fiction.
        if (!running(i)) {
            // On the EDGE, not every beat: a walk of the table per stopped
            // program per beat would cost more than the scan it replaced, and
            // nothing else writes a row this program exclusively claimed.
            if (_slot[i].ran) {
                _slot[i].ran = false;
                if (!_park_begin(i, now_ms)) { _drop_safe(i); return; }
            } else if (!_slot[i].parking) {
                return;                     // already safe, and staying there
            }
            // TRAVELLING. The body keeps being called, reading `park` and
            // driving its own actuators, until it answers or runs out of time.
            if (_io.var(programs[i].parked).as_bool()) { _park_end(i, true);  return; }
            if (now_ms >= _slot[i].park_until_ms)       { _park_end(i, false); return; }
            programs[i].run();
            return;
        }
        // Back in service: a park that was in flight is abandoned where it got
        // to, which is where the law is about to take over from anyway.
        if (_slot[i].parking) {
            _slot[i].parking = false;
            _io.var(programs[i].park).set(false);
        }
        _slot[i].ran = true;

        programs[i].run();
        _read_health(i);
    }

    void Plant::_read_health(uint8_t i) {
        if (programs[i].health_row == Vars::VAR_NONE) return;
        // Off the ROW, so an operator forcing it sees the TASKS page agree: the
        // page and the row are the same fact, and two readings of it that could
        // differ would be one too many.
        const uint64_t c = _io.var(programs[i].health_row).as<uint64_t>();
        _slot[i].health  = (health_label(c) != nullptr) ? (Health)c
                                                        : Health::FAULTED;
    }

    bool Plant::_park_begin(uint8_t i, uint64_t now_ms) {
        if (programs[i].park == Vars::VAR_NONE) return false;
        _slot[i].parking       = true;
        _slot[i].park_until_ms = now_ms + _park_timeout_ms();
        _io.var(programs[i].park).set(true);
        _io.var(programs[i].parked).set(false);
        if (_logger != nullptr)
            _logger->notice(Logs::HEALTH, "%s: parking", programs[i].name);
        return true;
    }

    void Plant::_park_end(uint8_t i, bool answered) {
        _slot[i].parking = false;
        _io.var(programs[i].park).set(false);
        // The declared values go on TOP of wherever the park left things: the
        // park is what put the actuator somewhere chosen, and `def` is what the
        // rows read once nothing is deciding. For a row the law parked, the two
        // agree - which is the point of declaring the safe state honestly.
        _drop_safe(i);
        if (_logger == nullptr) return;
        if (answered) _logger->notice(Logs::HEALTH, "%s: parked", programs[i].name);
        else _logger->warning(Logs::HEALTH, "%s: did not park in %lu s",
                              programs[i].name,
                              (unsigned long)(_park_timeout_ms() / 1000u));
    }

    void Plant::_drop_safe(uint8_t i) {
        const uint16_t n = _safe_outputs(i);
        if (_logger != nullptr)
            _logger->warning(Logs::HEALTH, "%s: out of the scan, %u row(s) safe",
                             programs[i].name, (unsigned)n);
    }

    uint32_t Plant::_park_timeout_ms() const {
        return (uint32_t)_io.var(Vars::Row::plc_parkTimeout_sec).as<uint16_t>() * 1000u;
    }

    bool Plant::parking() const {
        for (uint8_t i = 0; i < PROGRAM_COUNT; i++)
            if (_slot[i].parking) return true;
        return false;
    }

    void Plant::scan(uint64_t now_ms) {
        for (uint8_t i = 0; i < PROGRAM_COUNT; i++) scan(i, now_ms);
    }

    Health Plant::health(uint8_t i) const {
        return (i < PROGRAM_COUNT) ? _slot[i].health : Health::FAULTED;
    }

    /** @brief Drop everything program @p i drives to its declared safe state.
     *
     *  Off the generated roster: an ST row resolves as VAR_IN_OUT and claims no
     *  exclusive writer, so the bank cannot be asked whose it is. */
    uint16_t Plant::_safe_outputs(uint8_t i) {
        for (uint8_t k = 0; k < programs[i].write_count; k++)
            _io.var(programs[i].writes[k]).safe();
        return programs[i].write_count;
    }

    uint8_t Plant::count() const { return PROGRAM_COUNT; }

    const char* Plant::name(uint8_t i) const {
        return (i < PROGRAM_COUNT) ? programs[i].name : "";
    }

    bool Plant::enabled(uint8_t i) const {
        return (i < PROGRAM_COUNT) && _slot[i].enabled;
    }

    void Plant::set_enabled(uint8_t i, bool on) {
        if (i < PROGRAM_COUNT) _slot[i].enabled = on;
    }

    bool Plant::control_allowed() const {
        return _io.var(Vars::Row::sys_control).as_bool();
    }

    uint32_t Plant::running_mask() const {
        uint32_t m = 0;
        for (uint8_t i = 0; i < PROGRAM_COUNT; i++)
            if (running(i)) m |= (uint32_t)1u << i;
        return m;
    }

    // The two sources are INDEPENDENT and the stricter wins: the operator's
    // standing choice (sys_control, then this program's own gate) and the
    // system's hold (the lifecycle).
    bool Plant::running(uint8_t i) const {
        if (i >= PROGRAM_COUNT) return false;
        if (!_slot[i].enabled) return false;
        if (!control_allowed()) return false;
        return _state == nullptr || *_state == Scheduler::RunState::RUN;
    }

}}}  // namespace Botanik::Plc::Runtime

#endif // BOTANIK_FEATURE_PLC
