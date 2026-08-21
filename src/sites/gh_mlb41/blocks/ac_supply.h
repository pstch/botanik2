#ifndef SITE_BLOCKS_AC_SUPPLY_H
#define SITE_BLOCKS_AC_SUPPLY_H

/**
 * @file ac_supply.h
 * @brief HAND-WRITTEN - one AC supply, mirrored and judged.
 */

#include <cstdint>

#include "libbotanik.h"

namespace Site {

using namespace Botanik;

/** @name Publish deadbands
 *  How far a reading has to move to be worth a status write. Fixed here rather
 *  than as parameters: they are about the METER's last digit, not about the
 *  plant, and a supply an operator has to re-tune to keep quiet is a defect.
 *  @{ */

static const float SUPPLY_V_BAND  = 0.5f;
static const float SUPPLY_A_BAND  = 0.1f;
static const float SUPPLY_W_BAND  = 5.0f;
static const float SUPPLY_HZ_BAND = 0.05f;

/**
 * @brief One AC supply, watched: four readings mirrored onto status rows and
 *        judged against their bands.
 *
 * Not a Program - a supply is what a program watches, and there are two of
 * them. Held by value, so the grid's deadbands cannot become the local
 * supply's. Bound from either program's `Refs` by template: the two declare
 * the same member names, and the compiler is what holds them to it.
 *
 * The mirror exists because a field-read row carries no Modbus register of its
 * own - only parameters, status values and alarms are mapped - so an external
 * master cannot see the meter without one.
 */
class AcSupply {

    /** @name Blocks
     *  One deadband per quantity: the meter answers every few seconds and its
     *  last digit never settles, which would otherwise be a telemetry publish
     *  and a register write per poll.
     *  @{ */

    Plc::Deadband _v, _a, _w, _hz;
    /** @brief Whether this meter has EVER answered. Before it has, silence is
     *  bring-up; after, it is a fault. */
    bool          _seen = false;

    /** @} */
    /** @name Variables
     *  @{ */

    Vars::RefIn<float>  _in_v, _in_a, _in_w, _in_hz;
    Vars::RefIn<float>  _lo_v, _hi_v, _lo_hz, _hi_hz;
    Vars::RefOut<float> _out_v, _out_a, _out_w, _out_hz;
    Vars::RefOut<bool>  _ok;

    /** @} */

public:
    /** @brief Adopt @p r's references. Any `Refs` struct spelling the members
     *  the same way will do, which is the point: one law, two supplies. */
    template <typename R>
    void attach(const R& r) {
        _in_v = r.volts;  _in_a  = r.amps;  _in_w   = r.watts; _in_hz = r.hz;
        _lo_v = r.lo_V;   _hi_v  = r.hi_V;  _lo_hz  = r.lo_Hz; _hi_hz = r.hi_Hz;
        _out_v = r.out_V; _out_a = r.out_A; _out_w  = r.out_W; _out_hz = r.out_Hz;
        _ok    = r.ok;
    }

    /** @brief One reading onto its status row, through the deadband, or the row
     *  marked unmeasured. */
    static void _mirror(Vars::RefOut<float> out, Plc::Deadband& band,
                        Vars::RefIn<float> in, float width);

    /** @brief Mirror the four readings and judge them.
     *  @param fault set when this supply is out of spec, or has gone silent
     *         after answering at least once
     *  @return whether the meter answered, so the caller can report itself
     *         degraded rather than publishing a verdict on nothing */
    bool step(bool& fault);

    void reset();
};

}  // namespace Site

#endif // SITE_BLOCKS_AC_SUPPLY_H
