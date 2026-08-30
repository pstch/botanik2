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

/**
 * @brief One two-position valve: its coil, the position it fails to, and the
 *        pulse train that turns a fractional demand into an on-time.
 *
 * Not a Program - a valve is a part OF one, and both the irrigation cycle and
 * the wet skirt hold several. The fail direction comes from the ROW, so the
 * table stays the one place it is declared.
 */
struct ValveRefs {
    /** @brief The coil this valve drives. Its row's DEFAULT is the fail
     *  direction: park() reads it rather than being told. */
    Vars::RefOut<bool> coil;
};

/** @note No operator(): this one has four verbs, not a call. A valve is set,
 *  parked, or modulated, and a program says which - collapsing them into one
 *  invocation would need a mode argument nobody wants to read. */
class ValveController : public Plc::Block {

    Vars::RefOut<bool> _coil;
    bool     _fail_open = false;
    uint32_t _phase_ms  = 0;     ///< into the current modulation period

public:
    /** @brief Take @p coil, and its declared safe state as the fail direction. */
    void bind(const ValveRefs& r);

    /** @brief Drive the coil. @p current is the quality it is written with,
     *  forwarded rather than decided here: a valve is commanded by a law, and
     *  the law is what knows whether it still has the inputs it commanded on. */
    void set(bool open, bool current);
    /** @brief Drive it where a dead controller would leave it. */
    void park();
    bool fails_open() const { return _fail_open; }

    /** @brief Modulate: @p duty of @p period_ms, floored at @p min_on_ms -
     *  under which it stays shut, because a pulse that short does not lift the
     *  seat. @return the on-time fraction actually in force */
    float modulate(float duty, uint32_t period_ms, uint32_t min_on_ms,
                   uint32_t dt_ms);

    void reset() { _phase_ms = 0; }
};

BOTANIK_PLC_BLOCK(ValveController);

}  // namespace Site

#endif // SITE_BLOCKS_VALVE_CONTROLLER_H
