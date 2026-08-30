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

    /** @name VAR - the four zones' rows
     *
     *  One struct per measurement location, which is the block's own declared
     *  interface: the pairing of three probes to the two rows they feed is
     *  structural here rather than positional in an attach() argument list.
     *  @{ */

    ZoneProbeRefs hi, lo, cs, ext;

    /** @} */

    /** @name VAR_INPUT
     *  @{ */

    Vars::RefIn<uint16_t> tau_ms;

    /** @} */

private:

    /** @name VAR - function blocks
     *  @{ */

    ZoneProbes ceiling;
    ZoneProbes floor;
    ZoneProbes crawlspace;
    ZoneProbes exterior;

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
    tau_ms = _bind_input<uint16_t>(io, "cli_sensorTau_ms");

    hi.t[0] = _bind_input<float>(io, "cli_temp_hi1");
    hi.t[1] = _bind_input<float>(io, "cli_temp_hi2");
    hi.t[2] = _bind_input<float>(io, "cli_temp_hi3");
    hi.h[0] = _bind_input<float>(io, "cli_rh_hi1");
    hi.h[1] = _bind_input<float>(io, "cli_rh_hi2");
    hi.h[2] = _bind_input<float>(io, "cli_rh_hi3");
    hi.temp  = _bind_output<float>(io, "cli_temp_hi");
    hi.rh    = _bind_output<float>(io, "cli_rh_hi");
    hi.fault = _bind_output<bool>(io, "alarm_sns_ceiling");

    lo.t[0] = _bind_input<float>(io, "cli_temp_lo1");
    lo.t[1] = _bind_input<float>(io, "cli_temp_lo2");
    lo.t[2] = _bind_input<float>(io, "cli_temp_lo3");
    lo.h[0] = _bind_input<float>(io, "cli_rh_lo1");
    lo.h[1] = _bind_input<float>(io, "cli_rh_lo2");
    lo.h[2] = _bind_input<float>(io, "cli_rh_lo3");
    lo.temp  = _bind_output<float>(io, "cli_temp_lo");
    lo.rh    = _bind_output<float>(io, "cli_rh_lo");
    lo.fault = _bind_output<bool>(io, "alarm_sns_floor");

    cs.t[0] = _bind_input<float>(io, "cli_temp_cs1");
    cs.t[1] = _bind_input<float>(io, "cli_temp_cs2");
    cs.t[2] = _bind_input<float>(io, "cli_temp_cs3");
    cs.h[0] = _bind_input<float>(io, "cli_rh_cs1");
    cs.h[1] = _bind_input<float>(io, "cli_rh_cs2");
    cs.h[2] = _bind_input<float>(io, "cli_rh_cs3");
    cs.temp  = _bind_output<float>(io, "cli_temp_cs");
    cs.rh    = _bind_output<float>(io, "cli_rh_cs");
    cs.fault = _bind_output<bool>(io, "alarm_sns_crawlspace");

    ext.t[0] = _bind_input<float>(io, "cli_temp_ext1");
    ext.t[1] = _bind_input<float>(io, "cli_temp_ext2");
    ext.t[2] = _bind_input<float>(io, "cli_temp_ext3");
    ext.h[0] = _bind_input<float>(io, "cli_rh_ext1");
    ext.h[1] = _bind_input<float>(io, "cli_rh_ext2");
    ext.h[2] = _bind_input<float>(io, "cli_rh_ext3");
    ext.temp  = _bind_output<float>(io, "cli_temp_ext");
    ext.rh    = _bind_output<float>(io, "cli_rh_ext");
    ext.fault = _bind_output<bool>(io, "alarm_sns_exterior");

    const float tau_s = 0.001f * (float)tau_ms.get();
    ceiling.bind(hi, tau_s);
    floor.bind(lo, tau_s);
    crawlspace.bind(cs, tau_s);
    exterior.bind(ext, tau_s);
}

}  // namespace Site

#endif // SITE_PROGRAM_TEMP_RH_SENSORS_H
