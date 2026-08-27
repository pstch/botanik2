/**
 * @file temp_rh_sensors.h
 * @brief The declaration half of the `temp_rh_sensors` POU: its rows, its
 *        blocks, and the bind() that resolves them.
 */

#ifndef SITE_PROGRAM_TEMP_RH_SENSORS_H
#define SITE_PROGRAM_TEMP_RH_SENSORS_H

#include <cstdint>

#include "libbotanik.h"

#include "../../../vars.h"

#include "../../../blocks/zone_probes/zone_probes.h"

namespace Site {

using namespace Botanik;

/**
 * @brief The four measurement locations, each voted down to one reading and
 *        filtered: what the greenhouse IS, before any law is set in it.
 */
class TempRHSensors final : public Plc::Program {

public:

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<uint16_t> tau_ms;
    Vars::RefIn<float>    t_hi[3];
    Vars::RefIn<float>    h_hi[3];
    Vars::RefIn<float>    t_lo[3];
    Vars::RefIn<float>    h_lo[3];
    Vars::RefIn<float>    t_cs[3];
    Vars::RefIn<float>    h_cs[3];
    Vars::RefIn<float>    t_ext[3];
    Vars::RefIn<float>    h_ext[3];

    /** @} */

    /** @name VAR_OUTPUT
     *  @{ */

    Vars::RefOut<float> temp_hi;
    Vars::RefOut<float> rh_hi;
    Vars::RefOut<float> temp_lo;
    Vars::RefOut<float> rh_lo;
    Vars::RefOut<float> temp_cs;
    Vars::RefOut<float> rh_cs;
    Vars::RefOut<float> temp_ext;
    Vars::RefOut<float> rh_ext;
    Vars::RefOut<bool>  fault_hi;
    Vars::RefOut<bool>  fault_lo;
    Vars::RefOut<bool>  fault_cs;
    Vars::RefOut<bool>  fault_ext;

    /** @} */

private:

    /** @name VAR - function blocks
     *  @{ */

    ZoneProbes ceiling;
    ZoneProbes floor;
    ZoneProbes crawlspace;
    ZoneProbes exterior;

    /** @} */

    /** @name Sizes - what a body loops over
     *  @{ */

    static const uint8_t N_T_HI  = 3;
    static const uint8_t N_H_HI  = 3;
    static const uint8_t N_T_LO  = 3;
    static const uint8_t N_H_LO  = 3;
    static const uint8_t N_T_CS  = 3;
    static const uint8_t N_H_CS  = 3;
    static const uint8_t N_T_EXT = 3;
    static const uint8_t N_H_EXT = 3;

    /** @} */

public:
    TempRHSensors(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "temp_rh_sensors", Tasks::GROUP_FAST, io) {}

protected:
    /** @brief Resolve every name. An output is CLAIMED here. */
    void bind(Vars::Bank& io) override;
    void run(ProgramContext& c) override;
    void on_bind();  ///< wire a block to a row it was handed
};

inline void TempRHSensors::bind(Vars::Bank& io) {
    tau_ms    = _bind_input<uint16_t>(io, "cli_sensorTau_ms");
    t_hi[0]   = _bind_input<float>(io, "cli_temp_hi1");
    t_hi[1]   = _bind_input<float>(io, "cli_temp_hi2");
    t_hi[2]   = _bind_input<float>(io, "cli_temp_hi3");
    h_hi[0]   = _bind_input<float>(io, "cli_rh_hi1");
    h_hi[1]   = _bind_input<float>(io, "cli_rh_hi2");
    h_hi[2]   = _bind_input<float>(io, "cli_rh_hi3");
    t_lo[0]   = _bind_input<float>(io, "cli_temp_lo1");
    t_lo[1]   = _bind_input<float>(io, "cli_temp_lo2");
    t_lo[2]   = _bind_input<float>(io, "cli_temp_lo3");
    h_lo[0]   = _bind_input<float>(io, "cli_rh_lo1");
    h_lo[1]   = _bind_input<float>(io, "cli_rh_lo2");
    h_lo[2]   = _bind_input<float>(io, "cli_rh_lo3");
    t_cs[0]   = _bind_input<float>(io, "cli_temp_cs1");
    t_cs[1]   = _bind_input<float>(io, "cli_temp_cs2");
    t_cs[2]   = _bind_input<float>(io, "cli_temp_cs3");
    h_cs[0]   = _bind_input<float>(io, "cli_rh_cs1");
    h_cs[1]   = _bind_input<float>(io, "cli_rh_cs2");
    h_cs[2]   = _bind_input<float>(io, "cli_rh_cs3");
    t_ext[0]  = _bind_input<float>(io, "cli_temp_ext1");
    t_ext[1]  = _bind_input<float>(io, "cli_temp_ext2");
    t_ext[2]  = _bind_input<float>(io, "cli_temp_ext3");
    h_ext[0]  = _bind_input<float>(io, "cli_rh_ext1");
    h_ext[1]  = _bind_input<float>(io, "cli_rh_ext2");
    h_ext[2]  = _bind_input<float>(io, "cli_rh_ext3");
    temp_hi   = _bind_output<float>(io, "cli_temp_hi");
    rh_hi     = _bind_output<float>(io, "cli_rh_hi");
    temp_lo   = _bind_output<float>(io, "cli_temp_lo");
    rh_lo     = _bind_output<float>(io, "cli_rh_lo");
    temp_cs   = _bind_output<float>(io, "cli_temp_cs");
    rh_cs     = _bind_output<float>(io, "cli_rh_cs");
    temp_ext  = _bind_output<float>(io, "cli_temp_ext");
    rh_ext    = _bind_output<float>(io, "cli_rh_ext");
    fault_hi  = _bind_output<bool>(io, "alarm_sns_ceiling");
    fault_lo  = _bind_output<bool>(io, "alarm_sns_floor");
    fault_cs  = _bind_output<bool>(io, "alarm_sns_crawlspace");
    fault_ext = _bind_output<bool>(io, "alarm_sns_exterior");

    set_health_row(_bind_output<uint8_t>(io, "prog_temp_rh_sensors"));
    on_bind();
}

}  // namespace Site

#endif // SITE_PROGRAM_TEMP_RH_SENSORS_H
