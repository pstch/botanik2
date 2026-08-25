/**
 * @file tasks.cpp
 * @brief The task registry: registration, the derived offsets, and phase-major
 *        dispatch (see tasks.h).
 */

#include "libbotanik/tasks/tasks.h"

#include <cstdio>

#include "libbotanik/hal/platform.h"
#include "libbotanik/hal/storage.h"
// The complete Program, and ONLY so add_program() can convert one to the
// Component& it registers as. Nothing here calls a Program method.
#include "libbotanik/plc/program.h"
#include "libbotanik/utils/assert.h"
#include "libbotanik/utils/text.h"

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
                    component_setup(_tasks[i].owner, now_ms, _tasks[i].name,
                                    _tasks[i].group, _tasks[i].phase);

        // Registered setup handlers, in registration order: bring-up ordering is
        // a property of the registry rather than of the composition root.
        Tick tick; tick.now_ms = now_ms;
        for (uint8_t i = 0; i < _count; i++)
            if (_tasks[i].phase == PHASE_COUNT && _tasks[i].handler != nullptr)
                _tasks[i].handler(_tasks[i].owner, tick);

        // A program the operator took out stays out across a power cycle. Read
        // AFTER the handlers: control authority is still withheld here, so
        // nothing runs on the answer before the first beat.
        load_programs();
    }

    Task* Registry::_claim(const char* name, GroupId group, Phase phase) {
        if (_count >= Config::MAX_TASKS) {
            // A refused registration is INVISIBLE: it fails no build and no
            // unit test, the work simply never runs. The counter and `tasks`
            // report it on hardware; this is what stops a sim or a test run
            // walking past it. @see Config::MAX_TASKS
            BOTANIK_ASSERT(!"task table full: raise MAX_TASKS");
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
        if (handler == nullptr) {
            BOTANIK_ASSERT(!"a task registered with no handler");
            if (_overflow < 0xFF) _overflow++;
            return false;
        }
        Task* t = _claim(name, group, phase);
        if (t == nullptr) return false;
        t->owner   = &owner;
        t->handler = handler;
        return true;
    }

    bool Registry::add(Component& owner, const char* name,
                       GroupId group, Phase phase) {
        // A null handler is the SIGNAL here, where the overload above treats it
        // as the defect it is there: run_phase() reads it as "call the owner's
        // hook for this phase".
        Task* t = _claim(name, group, phase);
        if (t == nullptr) return false;
        t->owner   = &owner;
        t->handler = nullptr;
        return true;
    }

    bool Registry::add_setup(Component& owner,
                             void (*handler)(Component*, const Tick&),
                             const char* name) {
        // PHASE_COUNT: registered, never dispatched by run_phase().
        return add(owner, handler, name, GROUP_FAST, PHASE_COUNT);
    }

    bool Registry::add_program(Plc::Program& program,
                               void (*handler)(Component*, const Tick&),
                               const char* name, GroupId group) {
        // The slot BEFORE the entry exists, so it is the index this program
        // will have in program(): the registry is the list, so a program's
        // position in it IS the writer id every row it drives is stamped with.
        // Nothing else can assign it now that no site table survives.
        const uint8_t slot = _prog_count;
        if (!add(program, handler, name, group, PHASE_CONTROL)) return false;
        _tasks[_count - 1].program = &program;
        _prog[_prog_count++] = (uint8_t)(_count - 1);
        program.set_slot(slot);
        return true;
    }

    void Registry::request_safe(bool on) {
        _safe_asked = on;
        _push_safe();
    }

    void Registry::hold_safe(bool on) {
        _safe_hold = on;
        _push_safe();
    }

    void Registry::_push_safe() {
        const bool on = safe_requested();
        for (uint8_t i = 0; i < _count; i++)
            if (_tasks[i].program != nullptr) _tasks[i].program->request_safe(on);
    }

    bool Registry::all_safe() const {
        return unsafe_count() == 0;
    }

    uint8_t Registry::unsafe_count() const {
        uint8_t n = 0;
        for (uint8_t i = 0; i < _count; i++) {
            const Plc::Program* p = _tasks[i].program;
            // A program that is not being activated cannot arrive, so a request
            // made while control is withdrawn is answered by the gate having
            // already forced every output to its safe state.
            if (p != nullptr && !p->is_parked()) n++;
        }
        return n;
    }

    void Registry::set_programs_enabled(bool enabled, uint64_t now_ms) {
        if (enabled == _programs_enabled) return;
        _programs_enabled = enabled;
        // Withdrawal is the gate and nothing else: a program that is not called
        // cannot act on what it remembers, and nothing outside it reads that.
        // What has gone stale is dropped on the way back IN, where the program
        // is looking at a fresh image rather than the one the edge happened on.
        //
        // Its HEALTH is the exception, because that IS read from outside. A
        // program stops being activated and would otherwise keep reporting the
        // verdict of whatever beat happened to be its last, which for a
        // supervisor reads as a plant still in hand.
        if (!enabled) {
            for (uint8_t i = 0; i < _count; i++)
                if (_tasks[i].program != nullptr) _tasks[i].program->set_safe();
            return;
        }

        for (uint8_t i = 0; i < _count; i++) {
            if (_tasks[i].program == nullptr) continue;
            // reseed() is "treat this as having just run", so the next
            // activation is a full period away rather than immediate, and
            // reports its nominal period rather than the outage. A task that
            // has never run is left alone - its first activation is due
            // immediately by design, which is also the bring-up case.
            _tasks[i].timer.reseed(now_ms);
            _tasks[i].first_activation = true;
        }
    }

    void Registry::run_phase(Phase phase, uint64_t now_ms) {
        for (uint8_t i = 0; i < _count; i++) {
            Task& t = _tasks[i];
            if (t.phase != phase) continue;
            // Control authority, and the ONLY thing it gates. The phases around
            // this one must not be gated with it: withholding control writes
            // safe states into the image, and something still has to carry them
            // out to the field.
            // Two independent gates, and the stricter wins: the system's control
            // authority, and the operator's own choice for this one program.
            // A program the operator switched off keeps being activated until it
            // has ARRIVED at its fail position: it reads inhibit_requested() and
            // drives its own actuators there, and one that stops running cannot
            // close a valve. The plant-wide gate parks them itself
            // (set_programs_enabled), so those are already is_parked() here.
            if (t.program != nullptr && (!_programs_enabled || !t.enabled) &&
                t.program->is_parked()) continue;

            uint32_t dt = 0; uint16_t missed = 0;
            if (!t.timer.due(now_ms, group_period_ms(t.group), dt, missed)) continue;
            t.timer.ran(now_ms);

            const uint64_t t0 = HAL::Clock::uptime_us();
            Tick tick;
            tick.now_ms = now_ms; tick.dt_ms = dt; tick.missed = missed;
            tick.first  = t.first_activation;
            // A task registered without a handler is one whose work IS its
            // component's hook for the phase it sits in.
            if (t.handler != nullptr) t.handler(t.owner, tick);
            else                      t.owner->step(now_ms, t.phase);
            // Cleared after the run: the flag describes the activation that just
            // happened, so a handler reads it for its own run and no other.
            t.first_activation = false;
            t.cost.record((uint32_t)(HAL::Clock::uptime_us() - t0), missed);
        }
    }

    Task* Registry::_program_task(uint8_t i) {
        return (i < _prog_count) ? &_tasks[_prog[i]] : nullptr;
    }
    const Task* Registry::_program_task(uint8_t i) const {
        return (i < _prog_count) ? &_tasks[_prog[i]] : nullptr;
    }

    Plc::Program* Registry::program(uint8_t i) const {
        const Task* t = _program_task(i);
        return (t == nullptr) ? nullptr : t->program;
    }

    uint8_t Registry::program_count() const { return _prog_count; }

    // Per-program enable

    namespace {
        /** "prog.<name>", one key per program: no layout to version, like the
         *  parameter and override keys. False when the name would not fit -
         *  a truncated key would collide with another program's. */
        bool _prog_key(const char* name, char* buf, size_t cap) {
            const int n = snprintf(buf, cap, "prog.%s", name);
            return n > 0 && (size_t)n < cap;
        }
    }

    int16_t Registry::find_program(const char* name, uint16_t len) const {
        if (name == nullptr) return -1;
        for (uint8_t i = 0; i < _prog_count; i++) {
            const char* nm = _tasks[_prog[i]].name;
            if (Utils::equals_bounded(nm, name, len)) return (int16_t)i;
        }
        return -1;
    }

    bool Registry::program_enabled(uint8_t i) const {
        const Task* t = _program_task(i);
        return (t != nullptr) && t->enabled;
    }

    bool Registry::program_unsaved(uint8_t i) const {
        const Task* t = _program_task(i);
        return (t != nullptr) && t->enable_unsaved;
    }

    bool Registry::set_program_enabled(uint8_t i, bool on, uint64_t now_ms) {
        Task* tp = _program_task(i);
        if (tp == nullptr) return false;
        Task& t = *tp;
        if (t.enabled == on) return true;      // idempotent, and not a new edit
        t.enabled        = on;
        t.enable_unsaved = true;
        // The operator's own switch, beside the system's. Asking rather than
        // just gating: the program has to run to drive its actuators to the
        // fail position, and run_phase() keeps activating it until it has.
        if (t.program != nullptr) t.program->request_disable(!on);
        // The reseed control authority does, for the same reason: without it the
        // first activation back reports the whole outage as its dt, and a dwell
        // timer handed an hour of it fires the instant the program returns.
        if (on) { t.timer.reseed(now_ms); t.first_activation = true; }
        return true;
    }

    bool Registry::save_program(uint8_t i) {
        Task* tp = _program_task(i);
        if (tp == nullptr || tp->name == nullptr) return false;
        Task& t = *tp;
        char key[HAL::Storage::KEY_MAX];
        if (!_prog_key(t.name, key, sizeof(key))) return false;
        const uint8_t byte = t.enabled ? 1u : 0u;
        if (!HAL::Storage::set(key, &byte, 1)) return false;
        t.enable_unsaved = false;              // cleared only on a write that landed
        return true;
    }

    uint8_t Registry::save_programs(uint8_t* refused) {
        uint8_t saved = 0, bad = 0;
        for (uint8_t i = 0; i < _prog_count; i++) {
            if (!program_unsaved(i)) continue;
            if (save_program(i)) saved++;
            else if (bad < 0xFF)  bad++;
        }
        if (refused != nullptr) *refused = bad;
        return saved;
    }

    void Registry::load_programs() {
        for (uint8_t i = 0; i < _prog_count; i++) {
            Task& t = _tasks[_prog[i]];
            if (t.name == nullptr) continue;
            char key[HAL::Storage::KEY_MAX];
            if (!_prog_key(t.name, key, sizeof(key))) continue;
            uint8_t byte = 0; size_t got = 0;
            // Nothing stored is the normal case: a program runs unless it was
            // deliberately taken out.
            if (!HAL::Storage::get(key, &byte, 1, &got) || got != 1) continue;
            t.enabled        = (byte != 0);
            t.enable_unsaved = false;
        }
    }

}}  // namespace Botanik::Tasks
