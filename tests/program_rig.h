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

/** @brief Publish one field-read row as if the bus had just answered it,
 *  and, where the row has a `_qlt` companion, say it was measured. A case
 *  giving a reading means the instrument answered; one rehearsing a dead
 *  instrument clears the companion itself. */
inline void field(Live& f, const char* row, float v, uint64_t at = 0) {
    f.bank.var(f.ref(row)).publish(from_f32(T_F32, v), at);
    char qlt[48];
    const int n = snprintf(qlt, sizeof qlt, "%s_qlt", row);
    if (n <= 0 || (size_t)n >= sizeof qlt) return;
    const Vars::VarRef q = f.bank.find(qlt, (uint16_t)n);
    if (q.index != Vars::VAR_NONE)
        f.bank.var(q).publish(from_bool(T_BOOL, true), at);
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

/** @brief Clear the interlocks that DEFAULT to their forbidding state.
 *
 *  A permissive boolean whose whole purpose is to forbid something defaults to
 *  the forbidding answer: a roof must not open on a supply nobody has measured,
 *  and the skirt must not humidify into air that has only just dried. dc_watch
 *  and humidity are what clear them, and a rig without those on the beat never
 *  does - so a case about anything else has to say so. */
inline void interlocks_clear(Live& f) {
    f.bank.var(f.ref("sys_supply_lockout")).set(false);
    f.bank.var(f.ref("cli_rotLockout")).set(false);
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

/** @brief The ST plant on its own.
 *
 *  There is no program object to hold a rig open on - destrat_controller is
 *  generated ST now - so the fixture drives the RESOURCE. `d.step()` keeps the
 *  name every other rig uses so a case reads the same. */
struct StRig {
    Live                live;
    /** PHASE_INPUT's job, which the beat does before PHASE_CONTROL: a probe
     *  that stopped answering reaches ST as a companion, never as a quality. */
    Vars::QualityMirror mirror{ Botanik::SetupContext(live.reg), live.bank };
    Site::StRuntime     st{ Botanik::SetupContext(live.reg), live.bank };

    StRig() {
        st.wire();
        mirror.wire();
        d.st = &st;
        d.mirror = &mirror;
        // The resource is the unit now, not the program: every ST program in
        // it runs on every scan. destrat publishes cli_stratDem, which is one
        // of the four floors under the fan ladder, so a case about any OTHER
        // floor has to put the gap where destrat asks for nothing. A case
        // about destrat sets the gap itself.
        live.bank.var(live.ref("cli_dTStrat_degC"))
            .publish(from_f32(T_F32, -0.1f), 0);
    }

    struct Driver {
        Site::StRuntime*     st = nullptr;
        Vars::QualityMirror* mirror = nullptr;
        void step(uint64_t now_ms) { mirror->scan(); st->scan(now_ms); }
    } d;
};

/** @brief The sensing chain: the probes are voted and filtered, then the
 *  psychrometrics are taken off the two interior levels. Split across two
 *  programs, so a case about what a reading BECOMES needs both. */
struct ZoneRig {
    Live                live;
    /** The voting and filtering half is ST now; the psychrometrics are not. */
    Site::StRuntime     st  {Botanik::SetupContext(live.reg), live.bank};
    Site::TempRHZone    zone{Botanik::SetupContext(live.reg), live.bank};
    Plc::Program* const progs[1] = { &zone };
    Cycle               cycle{progs, 1, &live.bank, live.reg};

    ZoneRig() { st.wire(); d.st = &st; d.cycle = &cycle; }

    struct Driver {
        Site::StRuntime* st = nullptr;
        Cycle*           cycle = nullptr;
        void step(uint64_t now_ms) { st->scan(now_ms); cycle->step(now_ms); }
    } d;
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
    /** The lockout is ST now, so it is the RESOURCE that runs first. */
    Site::StRuntime         st{Botanik::SetupContext(live.reg), live.bank};
    Site::WindowsController wc{Botanik::SetupContext(live.reg), live.bank};
    Plc::Program* const     progs[1] = { &wc };
    Cycle                   cycle{progs, 1, &live.bank, live.reg};

    RoofRig() { st.wire(); d.st = &st; d.cycle = &cycle; }

    /** @brief ST, then the programs - the order the beat uses. */
    struct Driver {
        Site::StRuntime* st = nullptr;
        Cycle*           cycle = nullptr;
        uint64_t         last = 0;
        void step(uint64_t now_ms) {
            st->scan(now_ms);
            cycle->step(now_ms);
        }
    } d;
};

}  // namespace site_programs

#endif // TESTS_PROGRAM_RIG_H
