/**
 * @file program_rig.h
 * @brief The fixtures the site's program tests are written against.
 *
 * One live bank, one registry, and a Cycle over whichever programs a case
 * needs. Shared because the three program test files drive the same site.
 */

#ifndef TESTS_PROGRAM_RIG_H
#define TESTS_PROGRAM_RIG_H

#include <cstdint>
#include <cmath>
#include <cstdio>

#include "doctest.h"

#include "libbotanik.h"
#include "libbotanik_site.h"
#include BOTANIK_SITE_HEADER
#include "sites/gh_mlb41/programs.h"

#include "scan_fixture.h"

namespace site_programs {

using namespace Botanik;
using namespace Botanik::Vars;

using TestScan::Cycle;
using TestScan::Live;

// INLINE, not an anonymous namespace: the three program test files each use a
// SUBSET of these, and an unused static function warns where an unused inline
// one does not.

/** @brief Summer, as the modulation ladder would report it: full watering. The
 *  ladder is its own program now, so a cycle case states the factor rather than
 *  driving the weather that would produce it. */
inline void summer(Live& f) {
    f.bank.var(f.ref("irr_mod")).set(1.0f);
}

/** @brief Take the soak out of the cycle, which `irr_soak_sec` 0 is for.
 *  A case about the fill or the drain states its own configuration; the soak
 *  has a case of its own and the scenarios run at the site's real figure. */
inline void no_soak(Live& f) {
    f.bank.param_set(f.bank.var(f.ref("irr_soak_sec")), "0");
}

/** @brief Put the plant in the end-of-night watering slot. A cycle may only
 *  START in one of the two the spec allows, so every irrigation case has to
 *  say which one it is in. */
inline void slot(Live& f) {
    f.bank.var(f.ref("cli_phase")).set_i((int64_t)Site::PHASE_DAWN_BREEZE);
}

/** @brief Where the roof parks, from the row that declares it. */
inline float safe_open(Live& f) {
    const Vars::Descriptor& d = f.bank.descriptor(f.ref("cli_window1_tgt").index);
    return Vars::as_f32(d.type, d.def);
}

/** @brief Publish one field-read row as if the bus had just answered it. */
inline void field(Live& f, const char* row, float v, uint64_t at = 0) {
    f.bank.var(f.ref(row)).publish(from_f32(T_F32, v), at);
}

/** @brief The house at two levels, and the row derived from them.
 *
 *  The cooling ladder controls on `cli_temp_in` and reports its verdict on
 *  the levels behind it, so a case stating one without the other is
 *  stating half a house. WORST CASE, as temp_rh_zone makes it. */
inline void interior(Live& f, float hi, float lo) {
    field(f, "cli_temp_hi", hi);
    field(f, "cli_temp_lo", lo);
    field(f, "cli_temp_in", hi > lo ? hi : lo);
}

/** @brief A weather station reporting fine weather, so a case that is not
 *  about the interlocks is not shut in by one. */
inline void fine_weather(Live& f) {
    field(f, "cli_rain_mmph", 0.0f);
    field(f, "cli_wind_mps", 1.0f);
    field(f, "cli_windGust_mps", 2.0f);
}
/** @brief A live bank with ONE program on the beat, which is what a law's own
 *  case needs: `prog` is the program, `d` the cycle over it. */
template <class P>
struct Rig {
    Live                live;
    P                   prog{Botanik::SetupContext(live.reg), live.bank};
    Plc::Program* const progs[1] = { &prog };
    Cycle               d{progs, 1, &live.bank, live.reg};
};

/** @brief The sensing chain: the probes are voted and filtered, then the
 *  psychrometrics are taken off the two interior levels. Split across two
 *  programs, so a case about what a reading BECOMES needs both. */
struct ZoneRig {
    Live                live;
    Site::TempRHSensors sensors{Botanik::SetupContext(live.reg), live.bank};
    Site::TempRHZone    zone   {Botanik::SetupContext(live.reg), live.bank};
    Plc::Program* const progs[2] = { &sensors, &zone };
    Cycle               d{progs, 2, &live.bank, live.reg};
};

/** @brief The cooling chain: the ladder makes a demand, the gates decide
 *  whether crawlspace air would help serve it. Two programs because they are
 *  two networks - the ladder reads the interior, the gates read the crawlspace
 *  probe and the demand the ladder just published. */
struct CoolingRig {
    Live                     live;
    Site::CoolingController  cooling{Botanik::SetupContext(live.reg), live.bank};
    Site::IntakeGates        gates  {Botanik::SetupContext(live.reg), live.bank};
    Plc::Program* const      progs[2] = { &cooling, &gates };
    Cycle                    d{progs, 2, &live.bank, live.reg};
};

/** @brief The irrigation chain: the meters feed the schedule, the schedule
 *  arms the cycle. Nothing here works with fewer than all three. */
struct IrrigationRig {
    Live                     live;
    Site::IrrMtrInput        in_meter {Botanik::SetupContext(live.reg), live.bank};
    Site::IrrMtrOutput       out_meter{Botanik::SetupContext(live.reg), live.bank};
    Site::IrrigationSchedule sched    {Botanik::SetupContext(live.reg), live.bank};
    Site::IrrigationCycle    irr      {Botanik::SetupContext(live.reg), live.bank};
    Site::IrrValves          valves   {Botanik::SetupContext(live.reg), live.bank};
    Plc::Program* const      progs[5] = { &in_meter, &out_meter, &sched, &irr,
                                          &valves };
    Cycle                    d{progs, 5, &live.bank, live.reg};
};

/** @brief The roof: the lockout runs FIRST, so the windows see the interlock
 *  it publishes on the same beat rather than the one before. */
struct RoofRig {
    Live                    live;
    Site::RoofLockout       lock_{Botanik::SetupContext(live.reg), live.bank};
    Site::WindowsController wc   {Botanik::SetupContext(live.reg), live.bank};
    Plc::Program* const     progs[2] = { &lock_, &wc };
    Cycle                   d{progs, 2, &live.bank, live.reg};
};

}  // namespace site_programs

#endif // TESTS_PROGRAM_RIG_H
