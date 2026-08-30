#ifndef SITE_BLOCKS_VALVE_CONTROLLER_H
#define SITE_BLOCKS_VALVE_CONTROLLER_H

/**
 * @file valve_controller.h
 * @brief HAND-WRITTEN - one two-position valve, as a block a program holds.
 */

#include <cstdint>

#include "libbotanik.h"

namespace Site {

using namespace Botanik;

struct ValveRefs {
    /** @brief The coil this valve drives. Its row's DEFAULT is the fail
     *  direction, so the table stays the one place it is declared. */
    Vars::RefOut<bool> coil;
};

/**
 * @brief What to do with a valve this scan: the block's VAR_INPUT, built at
 *        the call site by one of the three named forms below.
 *
 * A struct rather than an argument list because the three are not variations
 * on one signature - a pulse needs a period and a discrete command does not -
 * and a struct rather than a mode flag because `v(ValveCmd::parked())` says at
 * the call site what `v(0.0f, 0, 0, dt, true, true)` would not.
 */
struct ValveCmd {
    enum Mode : uint8_t { DRIVE, PULSE, PARK };

    Mode     mode      = DRIVE;
    bool     open      = false;
    bool     current   = true;
    float    duty      = 0.0f;
    uint32_t period_ms = 0;
    uint32_t min_on_ms = 0;
    uint32_t dt_ms     = 0;

    /** @brief Two-position: open or shut. @p current is the quality the coil
     *  is written with, forwarded rather than decided by the valve - a law is
     *  what knows whether it still has the inputs it commanded on. */
    static ValveCmd drive(bool open, bool current) {
        ValveCmd c; c.mode = DRIVE; c.open = open; c.current = current;
        return c;
    }
    /** @brief @p duty of @p period_ms, floored at @p min_on_ms - under which it
     *  stays shut, because a pulse that short does not lift the seat. */
    static ValveCmd pulse(float duty, uint32_t period_ms, uint32_t min_on_ms,
                          uint32_t dt_ms) {
        ValveCmd c; c.mode = PULSE; c.duty = duty; c.period_ms = period_ms;
        c.min_on_ms = min_on_ms; c.dt_ms = dt_ms;
        return c;
    }
    /** @brief Where a dead controller would leave it. */
    static ValveCmd parked() { ValveCmd c; c.mode = PARK; return c; }
};

/**
 * @brief One two-position valve: its coil, the position it fails to, and the
 *        pulse train that turns a fractional demand into an on-time.
 *
 * Not a Program - a valve is a part OF one, and both the irrigation cycle and
 * the wet skirt hold several.
 */
class ValveController : public Plc::Block {

    Vars::RefOut<bool> _coil;
    bool     _fail_open = false;
    uint32_t _phase_ms  = 0;     ///< into the current modulation period

public:
    /** @brief Take the coil, and its declared safe state as the fail
     *  direction. */
    void bind(const ValveRefs& r);

    /** @brief Drive the valve for this scan.
     *  @return the on-time fraction now in force: 1 or 0 for a discrete
     *          command, and what the pulse train actually serves otherwise. */
    float operator()(const ValveCmd& c);

    bool fails_open() const { return _fail_open; }

    /** @brief Drop the pulse phase, so the next period starts from shut. */
    void reset() { _phase_ms = 0; }
};

BOTANIK_PLC_BLOCK(ValveController);

}  // namespace Site

#endif // SITE_BLOCKS_VALVE_CONTROLLER_H
