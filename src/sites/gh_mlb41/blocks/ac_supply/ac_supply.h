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

/** @} */

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
struct AcSupplyRefs {
    /** @name VAR_INPUT
     *  @{ */
    Vars::RefIn<float>  volts, amps, watts, hz;
    Vars::RefIn<float>  lo_V, hi_V, lo_Hz, hi_Hz;
    /** @} */
    /** @name VAR_OUTPUT
     *  @{ */
    Vars::RefOut<float> out_V, out_A, out_W, out_Hz;
    Vars::RefOut<bool>  ok;
    Vars::RefOut<bool>  fault;
    /** @} */
};

class AcSupply : public Plc::Block {

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

    AcSupplyRefs _r;

    /** @} */

    /** @brief One reading onto its status row, through the deadband, or the row
     *  marked unmeasured. */
    static void _mirror(Vars::RefOut<float> out, Plc::Deadband& band,
                        Vars::RefIn<float> in, float width);

public:
    /** @brief Adopt the rows the program resolved. One law, two supplies: the
     *  struct is what says which. */
    void bind(const AcSupplyRefs& r) { _r = r; }

    /** @brief Mirror the four readings, judge them, and drive the verdict and
     *  the alarm.
     *  @return whether EVERY quantity answered - wider than the verdict, which
     *          rests on volts and frequency alone. */
    bool operator()();

    void reset();
};
BOTANIK_PLC_BLOCK(AcSupply);

}  // namespace Site

#endif // SITE_BLOCKS_AC_SUPPLY_H
