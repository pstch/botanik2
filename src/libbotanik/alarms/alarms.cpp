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
        // 1. Drain the hold, then re-arm it for as long as the raw bit is up.
        //    A FORCED condition follows the force immediately: the hold exists
        //    to smooth a real signal that flaps, and a hand-held row does not.
        const int16_t hold = hold_s();
        hold_rem_ms = drain_ms(hold_rem_ms, dt);
        if (forced)                      hold_rem_ms = 0;
        else if (raw_on && hold > 0)     hold_rem_ms = (uint32_t)hold * 1000u;

        // How long the RAW bit has been up, which is what the ON dwell asks
        // about. Not the episode: the hold above keeps an episode alive past
        // the signal, so a dwell measured from it can never filter anything.
        if (!raw_on)                      raw_ms = 0;
        else if (raw_ms <= 0xFFFFFFFFu - dt) raw_ms += dt;

        const bool on = raw_on || hold_rem_ms > 0;

        // 2. On a transition, stamp the edge and reset the per-episode bookkeeping.
        //    An ack belongs to ONE episode, so either edge ends it: the alarm
        //    going down is what the operator acknowledged happening, and a
        //    cleared condition carrying an ack would mask the next episode
        //    until its own ON edge got round to dropping it.
        if (on != active) {
            edge_ms  = now;
            user_ack = false;
            if (on) {                       // fresh ON episode
                off_sent = false;
                on_sent  = false;
                started_ms = now;           // onset, for start-time ordering
            }
        }
        active = on;

        // 3. Announce the transition once its dwell threshold has elapsed. ON
        //    is measured on the raw bit (above); OFF on the episode, which is
        //    when the hold finished draining and the alarm actually went down.
        const uint64_t dwell = now - edge_ms;
        const char* d = desc();
        const char* n = name();
        const char* label = d ? d : (n ? n : "");
        if (on) {
            if (due_on(raw_ms)) {
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
        _tasks->add(*this, &AlarmController::_service_task, "sys_alarms", Tasks::GROUP_SLOW,
              Tasks::PHASE_SERVICE);
    }

}}  // namespace Botanik::Alarms
