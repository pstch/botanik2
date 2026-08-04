/**
 * @file tasks.cpp
 * @brief The task registry: registration, the derived offsets, and phase-major
 *        dispatch (see tasks.h).
 */

#include "tasks.h"

#include "../hal/platform.h"

namespace Botanik { namespace Scheduler {

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
            case PHASE_EXECUTE: return "execute";
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
    }

    bool Registry::add(const char* name, GroupId group, Phase phase,
                       void (*run)(void* self, uint64_t now_ms), void* self,
                       void (*setup)(void* self, uint64_t now_ms)) {
        if (_count >= Config::MAX_TASKS || run == nullptr) {
            if (_overflow < 0xFF) _overflow++;
            return false;
        }

        // Derive the offset from how many members this group already has, so
        // each one lands a beat further along. Wrapped into the period: an
        // offset at or past it would skip a whole activation rather than shift
        // one, and a group with more members than beats in its period runs out
        // of distinct slots (they then share, which is no worse than the
        // phase-locked default).
        uint8_t rank = 0;
        for (uint8_t i = 0; i < _count; i++)
            if (_tasks[i].group == group) rank++;

        Task& t = _tasks[_count++];
        t.name  = name;
        t.group = group;
        t.phase = phase;
        t.run   = run;
        t.setup = setup;
        t.self  = self;

        const uint32_t period = group_period_ms(group);
        if (period != 0)
            t.timer.set_offset((uint32_t)rank * Config::TASK_PERIOD_MS % period);

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
            t.run(t.self, now_ms);
            t.cost.record((uint32_t)(HAL::Clock::uptime_us() - t0));
        }
    }

}}  // namespace Botanik::Scheduler
