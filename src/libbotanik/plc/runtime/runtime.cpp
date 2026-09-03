/**
 * @file runtime.cpp
 * @brief HAND-WRITTEN - the join between the beat and the generated ST.
 */

#include "libbotanik/plc/runtime/runtime.h"
#include "libbotanik/config/base.h"
#include "libbotanik/config/scheduler.h"

#include <cstdio>
#include "libbotanik/plc/runtime/bank.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/utils/assert.h"
#include "libbotanik/hal/platform.h"

#if BOTANIK_FEATURE_PLC

namespace Botanik { namespace Plc { namespace Runtime {

    
    void Plant::on_setup(uint64_t now_ms) {
        (void)now_ms;
        wire();
    }

    void Plant::wire() {
        io = &_io;
        _now_ref = _io.find("sys_now_ms");
        _day_ref = _io.find("sys_today");
        _control_ref = _io.find("sys_control");
        _park_timeout_ref = _io.find("plc_parkTimeout_sec");
        BOTANIK_ASSERT(PROGRAM_COUNT <=
                       (uint8_t)(sizeof _slot / sizeof _slot[0]));

        char row[40];
        for (uint8_t i = 0; i < PROGRAM_COUNT; i++) {
            const Program& p = programs[i];
            // The parking pair, where the program declared one. Generated
            // names, so a miss here is a table that was regenerated without
            // its rows - loud rather than a program that never parks.
            _slot[i].park_row   = (p.park   != nullptr) ? _io.find(p.park)
                                                        : Vars::VarRef{ Vars::VAR_NONE };
            _slot[i].parked_row = (p.parked != nullptr) ? _io.find(p.parked)
                                                        : Vars::VarRef{ Vars::VAR_NONE };

            // One AUTHOR per row is checked at generate time now, where it is
            // static and total: the bank's writer claim could not do it, since
            // an ST row resolves VAR_IN_OUT and records no owner.
            const uint16_t missed = _io.unresolved_count();
            p.init();

            // Counted per program, so a name one of them wanted and did not
            // get holds back that one and nothing else.
            const bool wired = (_io.unresolved_count() == missed);
            _slot[i].health = wired ? Health::HEALTHY
                                    : Health::INVALID;

            const int n = snprintf(row, sizeof row, "prog_%s", p.name);
            if (n > 0 && (size_t)n < sizeof row) {
                const Vars::VarRef r = _io.find(row, (uint16_t)n);
                _slot[i].health_row = r;
                if (r.index != Vars::VAR_NONE)
                    _io.var(r).set_i((int64_t)_slot[i].health);
            }
            if (!wired && _logger != nullptr)
                _logger->critical(Logs::BOOT,
                                  "%s: a name it binds resolves to no row; "
                                  "it will not run", p.name);
        }
    }

    void Plant::register_tasks() {
        for (uint8_t i = 0; i < PROGRAM_COUNT; i++) {
            if (!_tasks->add(*this, programs[i].task, programs[i].name,
                             programs[i].period_ms, Tasks::PHASE_CONTROL))
                continue;
            // The verdict this entry reports. An ST program is not a
            // Plc::Program, so the task keeps a POINTER to where its health
            // lives rather than the object: the TASKS page asks the entry, not
            // the kind of thing behind it.
            _tasks->last().health = &_slot[i].health;
        }
    }

    void Plant::scan(uint8_t i, uint64_t now_ms) {
        if (i >= PROGRAM_COUNT) return;
        _now_ms = now_ms;
        // The CLOCK, before any body reads it. Each block differences it
        // against its own last reading, so one row serves every period.
        if (_now_ref.index != Vars::VAR_NONE)
            _io.var(_now_ref).set_i((int64_t)(uint32_t)now_ms);
        // ...and the DATE beside it. Below the floor the RTC has never been
        // set, so there is no date and 0 is what says so - a schedule counts
        // in these and refuses to run against zero.
        if (_day_ref.index != Vars::VAR_NONE) {
            const uint64_t wall = HAL::Clock::wall_ms();
            _io.var(_day_ref).set_i(
                (wall < 1600000000000ull) ? 0 : (int64_t)(wall / 86400000ull));
        }

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
            if (_io.var(_slot[i].parked_row).as_bool()) { _park_end(i, true);  return; }
            if (now_ms >= _slot[i].park_until_ms)       { _park_end(i, false); return; }
            programs[i].run();
            return;
        }
        // Back in service: a park that was in flight is abandoned where it got
        // to, which is where the law is about to take over from anyway.
        if (_slot[i].parking) {
            _slot[i].parking = false;
            _io.var(_slot[i].park_row).set(false);
        }
        _slot[i].ran = true;

        programs[i].run();
    }

    bool Plant::_park_begin(uint8_t i, uint64_t now_ms) {
        if (_slot[i].park_row.index == Vars::VAR_NONE) return false;
        _slot[i].parking       = true;
        _slot[i].park_until_ms = now_ms + _park_timeout_ms();
        _io.var(_slot[i].park_row).set(true);
        _io.var(_slot[i].parked_row).set(false);
        if (_logger != nullptr)
            _logger->notice(Logs::HEALTH, "%s: parking", programs[i].name);
        return true;
    }

    void Plant::_park_end(uint8_t i, bool answered) {
        _slot[i].parking = false;
        _io.var(_slot[i].park_row).set(false);
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
        if (_park_timeout_ref.index == Vars::VAR_NONE) return 60000u;
        return (uint32_t)_io.var(_park_timeout_ref).as<uint16_t>() * 1000u;
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
        return (i < PROGRAM_COUNT) ? _slot[i].health : Health::INVALID;
    }

    /** @brief Drop everything program @p i drives to its declared safe state.
     *
     *  By NAME, off the generated roster: an ST row resolves as VAR_IN_OUT and
     *  claims no exclusive writer, so the bank cannot be asked whose it is.
     *  A find() per row is fine here - this runs on the edge of an operator
     *  action, never in the beat. */
    uint16_t Plant::_safe_outputs(uint8_t i) {
        uint16_t n = 0;
        for (uint8_t k = 0; k < programs[i].write_count; k++) {
            const Vars::VarRef r = _io.find(programs[i].writes[k]);
            if (r.index == Vars::VAR_NONE) continue;
            _io.var(r).safe();
            n++;
        }
        return n;
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
        return _control_ref.index == Vars::VAR_NONE ||
               _io.var(_control_ref).as_bool();
    }

    // The two sources are INDEPENDENT and the stricter wins: the operator's
    // standing choice (sys_control, then this program's own gate) and the
    // system's hold (the lifecycle). A program that could not be wired never
    // runs whatever either of them says - its references are inert sinks, so
    // every value it produced would be a zero nothing could tell from a
    // reading.
    bool Plant::running(uint8_t i) const {
        if (i >= PROGRAM_COUNT) return false;
        if (_slot[i].health != Health::HEALTHY) return false;
        if (!_slot[i].enabled) return false;
        if (!control_allowed()) return false;
        return _state == nullptr || *_state == Scheduler::RunState::RUN;
    }

}}}  // namespace Botanik::Plc::Runtime

#endif // BOTANIK_FEATURE_PLC
