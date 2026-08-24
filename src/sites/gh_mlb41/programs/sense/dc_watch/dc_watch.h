/**
 * @file dc_watch.h
 * @brief The declaration half of the `dc_watch` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_DC_WATCH_H
#define SITE_PROGRAM_DC_WATCH_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

namespace Site {

using namespace Botanik;

/**
 * @brief The DC rails and the two batteries, watched - and the plant-wide
 *        lockout they raise.
 */
class DcWatch final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<float>    dc1;
    Vars::RefIn<float>    dc2;
    Vars::RefIn<float>    ups_bat;
    Vars::RefIn<float>    gen_bat;
    Vars::RefIn<uint16_t> dc1_avg;
    Vars::RefIn<uint16_t> dc2_avg;
    Vars::RefIn<uint16_t> ups_avg;
    Vars::RefIn<uint16_t> gen_avg;
    Vars::RefIn<float>    dc1_lo;
    Vars::RefIn<float>    dc2_lo;
    Vars::RefIn<float>    ups_lo;
    Vars::RefIn<float>    gen_lo;
    Vars::RefIn<bool>     low_bat;

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<float> out_dc1;
    Vars::RefOut<float> out_dc2;
    Vars::RefOut<float> out_ups;
    Vars::RefOut<float> out_gen;
    Vars::RefOut<bool>  ok;
    Vars::RefOut<bool>  lockout;
    Vars::RefOut<bool>  ps1_flt;
    Vars::RefOut<bool>  ps2_flt;
    Vars::RefOut<bool>  ups_flt;
    Vars::RefOut<bool>  gen_flt;

    /** @} */

private:

    /** @name VAR - function blocks
     *  @{ */

    /** @brief The rail is judged on the AVERAGE, so one noisy sample cannot
     *  raise a supply fault. One per divider, each with its own time
     *  constant. */
    Plc::Ewma     ps1_mean;
    /** @brief ...and how far the average must move to be worth a status
     *  write */
    Plc::Deadband ps1_band;
    Plc::Ewma     ps2_mean;
    Plc::Deadband ps2_band;
    Plc::Ewma     ups_mean;
    Plc::Deadband ups_band;
    Plc::Ewma     gen_mean;
    Plc::Deadband gen_band;
    Plc::Ton      low_for;  ///< debounce: a contact bounces, a UPS does not

    /** @} */

    /** @name VAR - what this law remembers
     *  @{ */

    /** @brief Per rail, whether its divider has EVER answered. @see
     *  AcSupply */
    bool _seen[4] = { false, false, false, false };
    /** @brief What this law last told the registry, so the plant-wide hold
     *  is pushed on the EDGE and not walked over every task every beat. */
    bool _safe_held = false;

    /** @} */

    Vars::VarRef _watch_rows[5];  ///< swept for validity before each activation

public:
    DcWatch(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "dc_watch", Tasks::GROUP_SLOW, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
    void observe(ProgramContext& c) override;  ///< reports in EVERY state
};

inline void DcWatch::bind(Vars::Bank& io) {
    dc1     = io.resolve_in<float>("pwr_dc1_V");
    dc2     = io.resolve_in<float>("pwr_dc2_V");
    ups_bat = io.resolve_in<float>("pwr_upsBat_V");
    gen_bat = io.resolve_in<float>("pwr_genBat_V");
    dc1_avg = io.resolve_in<uint16_t>("pwr_dc1_avg_ms");
    dc2_avg = io.resolve_in<uint16_t>("pwr_dc2_avg_ms");
    ups_avg = io.resolve_in<uint16_t>("pwr_upsBat_avg_ms");
    gen_avg = io.resolve_in<uint16_t>("pwr_genBat_avg_ms");
    dc1_lo  = io.resolve_in<float>("pwr_dc1Lo_V");
    dc2_lo  = io.resolve_in<float>("pwr_dc2Lo_V");
    ups_lo  = io.resolve_in<float>("pwr_upsBatLo_V");
    gen_lo  = io.resolve_in<float>("pwr_genBatLo_V");
    low_bat = io.resolve_in<bool>("pwr_upsLowBat");
    out_dc1 = io.resolve_out<float>("sys_dc1_V");
    out_dc2 = io.resolve_out<float>("sys_dc2_V");
    out_ups = io.resolve_out<float>("sys_upsBat_V");
    out_gen = io.resolve_out<float>("sys_genBat_V");
    ok      = io.resolve_out<bool>("sys_dc_ok");
    lockout = io.resolve_out<bool>("sys_supply_lockout");
    ps1_flt = io.resolve_out<bool>("alarm_dc_ps1");
    ps2_flt = io.resolve_out<bool>("alarm_dc_ps2");
    ups_flt = io.resolve_out<bool>("alarm_dc_ups");
    gen_flt = io.resolve_out<bool>("alarm_generator_battery");

    // What the base sweeps before each activation, so an input that has
    // stopped being believable is DEGRADED without the law asking.
    _watch_rows[0] = dc1.r;
    _watch_rows[1] = dc2.r;
    _watch_rows[2] = ups_bat.r;
    _watch_rows[3] = gen_bat.r;
    _watch_rows[4] = low_bat.r;
    set_inputs(_watch_rows, 5);
    set_health_row(io.resolve_out<uint8_t>("prog_dc_watch"));
}

}  // namespace Site

#endif // SITE_PROGRAM_DC_WATCH_H
