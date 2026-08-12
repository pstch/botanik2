/**
 * @file tasks.cpp
 * @brief The task registry: registration, the derived offsets, and phase-major
 *        dispatch (see tasks.h).
 */

#include "libbotanik/tasks/tasks.h" // Registry / Task

#include "libbotanik/hal/platform.h"    // HAL::Clock::uptime_us

namespace Botanik { namespace Tasks {

    uint32_t group_period_ms(GroupId g) {
        switch (g) {
            case GROUP_FAST:       return Config::GROUP_FAST_MS;
            case GROUP_MAIN:       return Config::GROUP_MAIN_MS;
            case GROUP_SLOW:       return Config::GROUP_SLOW_MS;
            case GROUP_BACKGROUND: return Config::GROUP_BACKGROUND_MS;
            default:               return Config::GROUP_FAST_MS;
        }
    }

    const char* group_name(GroupId g) {
        switch (g) {
            case GROUP_FAST:       return "fast";
            case GROUP_MAIN:       return "main";
            case GROUP_SLOW:       return "slow";
            case GROUP_BACKGROUND: return "bkgnd";
            default:               return "?";
        }
    }

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
        for (uint8_t p = 0; p < PHASE_COUNT; p++)
            for (uint8_t i = 0; i < _count; i++)
                if (_tasks[i].phase == (Phase)p && _tasks[i].setup != nullptr)
                    _tasks[i].setup(_tasks[i].self, now_ms);

        // Registered setup handlers, in registration order: bring-up ordering is
        // a property of the registry rather than of the composition root.
        Tick tick; tick.now_ms = now_ms;
        for (uint8_t i = 0; i < _count; i++)
            if (_tasks[i].phase == PHASE_COUNT && _tasks[i].handler != nullptr)
                _tasks[i].handler(_tasks[i].owner, tick);
    }

    Task* Registry::_claim(const char* name, GroupId group, Phase phase) {
        if (_count >= Config::MAX_TASKS) {
            if (_overflow < 0xFF) _overflow++;
            return nullptr;
        }
        // Offset by how many members this group has, so each lands a beat
        // further along (stagger_offset(), shared with the scan cycle). A setup
        // handler is not in the beat, so it takes no slot in the spread.
        uint8_t rank = 0;
        if (phase != PHASE_COUNT)
            for (uint8_t i = 0; i < _count; i++)
                if (_tasks[i].group == group && _tasks[i].phase != PHASE_COUNT) rank++;

        Task& t = _tasks[_count++];
        t.name  = name;
        t.group = group;
        t.phase = phase;
        t.timer.set_offset(stagger_offset(rank, Config::TASK_PERIOD_MS,
                                          group_period_ms(group)));
        return &t;
    }

    bool Registry::add(Component& owner, void (*handler)(Component*, const Tick&),
                       const char* name, GroupId group, Phase phase) {
        if (handler == nullptr) { if (_overflow < 0xFF) _overflow++; return false; }
        Task* t = _claim(name, group, phase);
        if (t == nullptr) return false;
        t->owner   = &owner;
        t->handler = handler;
        // The same one-time init add_component() gives the legacy form: a
        // registration is what runs a component's on_setup() and announce().
        // Without it a self-registering component runs its per-beat work having
        // never been set up - which is how HealthMonitor's POST, and with it the
        // binding of every platform-health alarm, silently stopped happening.
        // Not for a setup registration (PHASE_COUNT): that one names its own.
        if (phase != PHASE_COUNT) {
            t->self  = &owner;
            t->setup = &component_setup;
        }
        return true;
    }

    bool Registry::add_setup(Component& owner,
                             void (*handler)(Component*, const Tick&),
                             const char* name) {
        // PHASE_COUNT: registered, never dispatched by run_phase().
        return add(owner, handler, name, GROUP_FAST, PHASE_COUNT);
    }

    bool Registry::add_program(Component& owner,
                               void (*handler)(Component*, const Tick&),
                               const char* name, GroupId group) {
        if (!add(owner, handler, name, group, PHASE_CONTROL)) return false;
        _tasks[_count - 1].is_program = true;
        return true;
    }

    bool Registry::add(const char* name, GroupId group, Phase phase,
                       void (*run)(void* self, uint64_t now_ms), void* self,
                       void (*setup)(void* self, uint64_t now_ms)) {
        if (run == nullptr) { if (_overflow < 0xFF) _overflow++; return false; }
        Task* t = _claim(name, group, phase);
        if (t == nullptr) return false;
        t->run   = run;
        t->setup = setup;
        t->self  = self;
        return true;
    }

    void Registry::run_phase(Phase phase, uint64_t now_ms) {
        for (uint8_t i = 0; i < _count; i++) {
            Task& t = _tasks[i];
            if (t.phase != phase) continue;

            uint32_t dt = 0; uint16_t missed = 0;
            if (!t.timer.due(now_ms, group_period_ms(t.group), dt, missed)) continue;
            t.timer.ran(now_ms);

            const uint64_t t0 = HAL::Clock::uptime_us();
            if (t.handler != nullptr) {
                Tick tick;
                tick.now_ms = now_ms; tick.dt_ms = dt; tick.missed = missed;
                t.handler(t.owner, tick);
            } else {
                t.run(t.self, now_ms);
            }
            t.cost.record((uint32_t)(HAL::Clock::uptime_us() - t0), missed);
        }
    }

}}  // namespace Botanik::Tasks
