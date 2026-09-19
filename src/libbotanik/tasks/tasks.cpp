/**
 * @file tasks.cpp
 * @brief The task registry: registration, the derived offsets, and phase-major
 *        dispatch (see tasks.h).
 */

#include "libbotanik/tasks/tasks.h"

#include <cstdio>

#include "libbotanik/hal/platform.h"
#include "libbotanik/hal/storage.h"
#include "libbotanik/utils/assert.h"
#include "libbotanik/utils/text.h"

namespace Botanik { namespace Tasks {

    const char* phase_name(Phase p) {
        switch (p) {
            case PHASE_INPUT:   return "input";
            case PHASE_CONFIG:  return "config";
            case PHASE_SERVICE: return "service";
            case PHASE_CONTROL: return "control";
            case PHASE_OUTPUT:  return "output";
            case PHASE_PRESENT: return "present";
            default:            return "?";
        }
    }

    void Registry::setup(uint64_t now_ms) {
        // Phase-major, like run_phase(), so init obeys the same data-flow order
        // the beat does. setup() is idempotent, so a component registered in
        // three phases still sets up once - and a registration is what runs it:
        // without this a self-registering component would run its per-beat work
        // having never been set up, which is how HealthMonitor's POST, and with
        // it the binding of every platform-health alarm, silently stopped
        // happening.
        for (uint8_t p = 0; p < PHASE_COUNT; p++)
            for (uint8_t i = 0; i < _count; i++)
                if (_tasks[i].phase == (Phase)p)
                    component_setup(_tasks[i].owner, now_ms);

        // Registered setup handlers, in registration order: bring-up ordering is
        // a property of the registry rather than of the composition root.
        Tick tick; tick.now_ms = now_ms;
        for (uint8_t i = 0; i < _count; i++)
            if (_tasks[i].phase == PHASE_COUNT && _tasks[i].handler != nullptr)
                _tasks[i].handler(_tasks[i].owner, tick);

        // A program the operator took out stays out across a power cycle. Read
        // AFTER the handlers: control authority is still withheld here, so
        // nothing runs on the answer before the first beat.
    }

    Task* Registry::_claim(const char* name, uint32_t period_ms, Phase phase) {
        if (_count >= Config::MAX_TASKS) {
            // A refused registration is INVISIBLE: it fails no build and no
            // unit test, the work simply never runs. The counter and `tasks`
            // report it on hardware; this is what stops a sim or a test run
            // walking past it. @see Config::MAX_TASKS
            BOTANIK_ASSERT(!"task table full: raise MAX_TASKS");
            if (_overflow < 0xFF) _overflow++;
            return nullptr;
        }
        // EVERY_BEAT is "as often as there is", which is the beat itself.
        if (period_ms == 0) period_ms = Config::TASK_PERIOD_MS;

        // Offset by how many tasks already share this PERIOD, so each lands a
        // beat further along (stagger_offset(), shared with the scan cycle). A
        // setup handler is not in the beat, so it takes no slot in the spread.
        uint8_t rank = 0;
        if (phase != PHASE_COUNT)
            for (uint8_t i = 0; i < _count; i++)
                if (_tasks[i].period_ms == period_ms &&
                    _tasks[i].phase != PHASE_COUNT) rank++;

        Task& t = _tasks[_count++];
        t.name  = name;
        t.phase = phase;
        t.period_ms = period_ms;
        t.timer.set_offset(stagger_offset(rank, Config::TASK_PERIOD_MS,
                                          period_ms));
        return &t;
    }

    bool Registry::add(Component& owner, void (*handler)(Component*, const Tick&),
                       const char* name, uint32_t period_ms, Phase phase) {
        if (handler == nullptr) {
            BOTANIK_ASSERT(!"a task registered with no handler");
            if (_overflow < 0xFF) _overflow++;
            return false;
        }
        Task* t = _claim(name, period_ms, phase);
        if (t == nullptr) return false;
        t->owner   = &owner;
        t->handler = handler;
        t->health  = component_verdict_slot(&owner);
        return true;
    }

    bool Registry::add(Component& owner, const char* name,
                       uint32_t period_ms, Phase phase) {
        // A null handler is the SIGNAL here, where the overload above treats it
        // as the defect it is there: run_phase() reads it as "call the owner's
        // hook for this phase".
        Task* t = _claim(name, period_ms, phase);
        if (t == nullptr) return false;
        t->owner   = &owner;
        t->handler = nullptr;
        t->health  = component_verdict_slot(&owner);
        return true;
    }

    bool Registry::add_setup(Component& owner,
                             void (*handler)(Component*, const Tick&),
                             const char* name) {
        // PHASE_COUNT: registered, never dispatched by run_phase().
        return add(owner, handler, name, EVERY_BEAT, PHASE_COUNT);
    }

    void Registry::run_phase(Phase phase, uint64_t now_ms) {
        for (uint8_t i = 0; i < _count; i++) {
            Task& t = _tasks[i];
            if (t.phase != phase) continue;
            // A program setup could not wire never runs. Plc::Runtime::Plant
            // keeps that verdict now and returns early on it, so the entry
            // itself no longer carries one.
            uint32_t dt = 0; uint16_t missed = 0;
            if (!t.timer.due(now_ms, t.period_ms, dt, missed)) continue;
            t.timer.ran(now_ms);

            // The HRT, not uptime_us(): on the OPTA that reads a 32.768 kHz
            // low-power clock, so every task under ~30 us would time as 0.
            const uint32_t t0 = HAL::Clock::hrt_ticks();
            Tick tick;
            tick.now_ms = now_ms; tick.dt_ms = dt; tick.missed = missed;
            tick.first  = t.first_activation;
            // A task registered without a handler is one whose work IS its
            // component's hook for the phase it sits in.
            if (t.handler != nullptr) {
                t.handler(t.owner, tick);
                // step() samples for itself; a handler task bypasses it, and
                // its entry still points at the same slot.
                if (t.owner != nullptr) component_sample_verdict(t.owner);
            } else {
                component_step(t.owner, now_ms, t.phase);
            }
            // Cleared after the run: the flag describes the activation that just
            // happened, so a handler reads it for its own run and no other.
            t.first_activation = false;
            t.cost.record(HAL::Clock::hrt_us(HAL::Clock::hrt_ticks() - t0), missed);
        }
    }

}}  // namespace Botanik::Tasks
