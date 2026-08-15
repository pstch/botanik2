// REVIEW_35: DONE
/**
 * @file alarms.cpp
 * @brief Out-of-line AlarmCondition::step (the per-condition model loop body).
 *
 * Kept here rather than the header: it is the one non-trivial method on the
 * otherwise header-only alarm types, and it needs the complete AlarmSink.
 */

#include "libbotanik/tasks/tasks.h"
#include "libbotanik/alarms/alarms.h"

namespace Botanik { namespace Alarms {

    void AlarmCondition::step(bool raw_on, bool forced, uint32_t dt, uint64_t now,
                              const AlarmSink& sink) {
        // 1. Drain the latch countdowns & refresh the hold-on delay
        const int16_t hold_s = hold_on_s();
        hold_on_rem_ms = drain_ms(hold_on_rem_ms, dt);
        min_on_rem_ms  = drain_ms(min_on_rem_ms, dt);
        if (raw_on && hold_s > 0 && !forced)
            hold_on_rem_ms = (uint32_t)hold_s * 1000u;

        // A FORCED condition follows the force immediately: the latches exist to
        // smooth a real signal that flaps, and a hand-held row does not flap.
        if (forced) { hold_on_rem_ms = 0; min_on_rem_ms = 0; }

        const bool on = raw_on || hold_on_rem_ms > 0 || min_on_rem_ms > 0;

        // 2. On a transition, stamp the edge and reset the per-episode bookkeeping.
        if (on != active) {
            edge_ms = now;
            if (on) {                       // fresh ON episode
                off_sent = false;
                on_sent  = false;
                user_ack   = false;         // a fresh episode is unacknowledged
                started_ms = now;           // onset, for start-time ordering
                const int16_t min_s = min_on_s();
                if (min_s > 0 && !forced)
                    min_on_rem_ms = (uint32_t)min_s * 1000u;
            }
        }
        active = on;

        // 3. Announce the transition once its dwell threshold has elapsed
        const uint64_t dwell = now - edge_ms;
        const char* d = desc();
        const char* n = name();
        const char* label = d ? d : (n ? n : "");
        if (on) {
            if (due_on(dwell)) {
                sink.log_start(label);
                sink.notify_on(*this);
                on_sent = true;
            }
        } else {
            if (due_off(dwell)) {
                if (!on_sent && on_s() >= 0) {
                    sink.log_flap(label);
                    sink.notify_flap(*this);
                } else {
                    sink.log_end(label);
                    sink.notify_off(*this);
                }
                off_sent = true;
            }
        }
    }

}}

namespace Botanik { namespace Alarms {

    void AlarmController::_service_task(Component* c, const Tasks::Tick& t) {
        static_cast<AlarmController*>(c)->on_service(t.now_ms);
    }

    void AlarmController::register_tasks() {
        // Its own row on the TASKS page: it was stepped inside HealthMonitor's
        // task, so its cost was charged to health and it had no rate of its own.
        _tasks->add(*this, &AlarmController::_service_task, "alarms", Tasks::GROUP_SLOW,
              Tasks::PHASE_SERVICE);
    }

}}  // namespace Botanik::Alarms
