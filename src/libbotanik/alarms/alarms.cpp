// REVIEW_35: TODO
/**
 * @file alarms.cpp
 * @brief Out-of-line AlarmCondition::step (the per-condition model loop body).
 *
 * Kept here rather than the header: it is the one non-trivial method on the
 * otherwise header-only alarm types, and it needs the complete AlarmSink.
 */

#include "alarms.h"

namespace Botanik { namespace Alarms {

    void AlarmCondition::step(bool raw_on, uint32_t dt, uint64_t now,
                              const AlarmSink& sink) {
        // 1. Drain the two wrap-immune latch countdowns, then refresh the
        //    off-delay while the raw bit is set: the off-delay holds active for
        //    off_delay_s after the raw bit clears, the minimum-ON hold keeps it
        //    active >= min_on_s from onset (anti-flicker). `on` holds until the
        //    raw bit and both latches are clear.
        off_delay_rem_ms = drain_ms(off_delay_rem_ms, dt);
        min_on_rem_ms    = drain_ms(min_on_rem_ms, dt);
        if (raw_on && off_delay_s > 0)
            off_delay_rem_ms = (uint32_t)off_delay_s * 1000u;
        bool on = raw_on || off_delay_rem_ms > 0 || min_on_rem_ms > 0;

        // 2. On a transition, stamp the edge and reset the per-episode
        //    bookkeeping. The *_on_sent flags reset on the ON edge (not OFF) so
        //    they survive the OFF episode and tell a clean end from a FLAP below.
        if (on != active) {
            edge_ms = now;
            if (on) {                       // fresh ON episode
                off_sent = false;
                on_sent  = false;
                user_ack   = false;         // a fresh episode is unacknowledged
                started_ms = now;           // onset, for start-time ordering
                if (min_on_s > 0) min_on_rem_ms = (uint32_t)min_on_s * 1000u;
            }
        }
        active   = on;

        // 3. Announce the transition once its dwell threshold has elapsed: a
        //    single event drives both the ALARM-topic log line and the notifier
        //    fan-out. The OFF side reports a FLAP when no matching ON event
        //    fired this episode.
        uint64_t dwell = now - edge_ms;
        const char* label = desc ? desc : (name ? name : "");
        if (on) {
            if (due(on_sent, on_s, dwell)) {
                sink.log_start(label);
                sink.notify_on(*this);
                on_sent = true;
            }
        } else {
            if (due(off_sent, off_s, dwell)) {
                if (is_flap(on_sent, on_s)) { sink.log_flap(label); sink.notify_flap(*this); }
                else                        { sink.log_end(label);  sink.notify_off(*this); }
                off_sent = true;
            }
        }
    }

}}
