#ifndef TASKS_PHASE_H
#define TASKS_PHASE_H

/**
 * @file phase.h
 * @brief Phase - where in the beat something runs.
 *
 * Its own header, and not part of tasks.h, because `core/` needs it: a
 * Component declares one hook per phase, so the bottom of the stack has to name
 * a phase without acquiring the Registry, the Task table or the scheduler
 * config that come with them.
 *
 * Depends on nothing at all, like task_timer.h beside it.
 */

namespace Botanik { namespace Tasks {

    /** @brief Where in the beat a task runs. The order is the data flow, fixed
     *  here and not registerable: commit the field, run the programs, collect
     *  what they drove, then report. */
    enum Phase {
        PHASE_INPUT = 0,   ///< field commit, serial RX, expiry
        PHASE_CONFIG,      ///< parameter commit, before anything reads config
        PHASE_SERVICE,     ///< health, network - background state machines
        PHASE_CONTROL,     ///< the control programs
        PHASE_OUTPUT,      ///< collect driven outputs, hand them to the field
        PHASE_PRESENT,     ///< log drain, render, serial TX
        PHASE_COUNT
    };

    /** @brief Display name of @p p ("input", "control"...). */
    const char* phase_name(Phase p);

}}  // namespace Botanik::Tasks

#endif // TASKS_PHASE_H
