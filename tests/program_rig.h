/**
 * @file program_rig.h
 * @brief The fixtures the site's program tests are written against.
 *
 * One live bank, one registry, and the ST plant over the site's own programs.
 * Shared: the three program test files drive the same site.
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
#include "live_fixture.h"


namespace tests { namespace site_programs {

using namespace Botanik;
using namespace Botanik::Vars;

using TestScan::Live;

// Inline rather than an anonymous namespace: each of the three files uses a
// subset, and an unused static function warns where an unused inline one does
// not.

/** @brief Summer, as the modulation ladder reports it: full watering. A cycle
 *  case states the factor rather than driving the weather behind it. */
inline void summer(Live& f) {
    f.bank.var(f.ref("irr_mod")).set(1.0f);
}

/** @brief Take the soak out of the cycle: `irr_soak_sec` 0. A case about the
 *  fill or the drain states its own configuration. */
inline void no_soak(Live& f) {
    f.bank.var(f.ref("irr_soak_sec")).param_set("0");
}

/** @brief Force a row: an override, which shadows the live value.
 *
 *  A row another ST program writes cannot be set, since the next scan
 *  overwrites it. Force where the case is about how a law responds to an input;
 *  drive the chain where it is about how the input is produced.
 *  @{ */
inline void force(Live& f, const char* row, float v) {
    f.bank.var(f.ref(row)).override_set(from_f32(T_F32, v));
}
inline void force(Live& f, const char* row, bool v) {
    f.bank.var(f.ref(row)).override_set(from_bool(T_BOOL, v));
}
/** @brief ...and a whole-number row: a state code, a tray number. */
inline void force(Live& f, const char* row, int64_t v) {
    const uint16_t r = f.ref(row);
    f.bank.var(r).override_set(from_i64(f.bank.descriptor(r).type, v));
}
/** @brief Make @p row read like a dead instrument, and the other way. A
 *  quality is a row, so this is an ordinary force. */
inline void distrust(Live& f, const char* row) {
    f.bank.var(f.ref(row)).quality_var().override_set(Scalar::U(Q_NONE));
}
inline void believe(Live& f, const char* row) {
    f.bank.var(f.ref(row)).quality_var().override_set(Scalar::U(Q_GOOD));
}
/** @} */

/** @brief Put the day where a case needs it, through the sun.
 *
 *  cli_phase is day_phase_controller's output, so setting it directly is
 *  overwritten on the next scan. The boundaries are minutes either side of
 *  sunrise and sunset, and a phase is reached by placing the clock inside its
 *  window, the way the plant reaches it.
 *  @note The two bank phases also need their arm, which day_phase derives from
 *  the published extremes. */
inline void put_phase(Live& f, Site::DayPhase p) {
    f.bank.var(f.ref("sol_ready")).set(true);
    int rise = 0, set_ = 0;
    switch (p) {
        case Site::PHASE_NIGHT:       rise = -60;  set_ =  60;  break;
        case Site::PHASE_COLD_BANK:   rise =   0;  set_ =  60;  break;
        case Site::PHASE_DAWN_BREEZE: rise =  45;  set_ =  60;  break;
        case Site::PHASE_DAY:         rise = 120;  set_ = -240; break;
        case Site::PHASE_HEAT_BANK:   rise = 120;  set_ = -60;  break;
        case Site::PHASE_DUSK_BREEZE: rise = 120;  set_ =   0;  break;
        default: break;
    }
    f.bank.var(f.ref("sol_fromRise_min")).set_i(rise);
    f.bank.var(f.ref("sol_fromSet_min")).set_i(set_);
    // day_phase derives the arm from the extremes it accumulates, not from the
    // row, so the house has to have been hot or cold. Forced, because
    // temp_rh_zone owns cli_temp_in and would put the probes back over it.
    if (p == Site::PHASE_COLD_BANK)
        force(f, "cli_temp_in",
              f.bank.var(f.ref("cli_coldBankTrg_degC")).as_f32() + 5.0f);
    else if (p == Site::PHASE_HEAT_BANK)
        force(f, "cli_temp_in",
              f.bank.var(f.ref("cli_heatBankTrg_degC")).as_f32() - 5.0f);

    // And the row itself: a rig running day_phase derives cli_phase from the
    // two above and overwrites anything set here, and a rig without it has
    // nothing else that puts the plant in a phase.
    f.bank.var(f.ref("cli_phase")).set_i((int64_t)p);
    f.bank.var(f.ref("cli_phase")).set_quality(Q_GOOD);
}

/** @brief Put the plant in the end-of-night watering slot. A cycle may only
 *  start in one of the two slots, so every irrigation case says which. */
inline void slot(Live& f) {
    put_phase(f, Site::PHASE_DAWN_BREEZE);
    // ...and past the dawn gate, which prevents watering cold roots.
    // temp_rh_sensors owns cli_temp_ext and publishes the row's band floor for
    // a zone nothing has measured, so a rig seeding no exterior probe is always
    // too cold to water.
    force(f, "cli_temp_ext",
          f.bank.var(f.ref("irr_dawnGate_degC")).as_f32() + 5.0f);
}

/** @brief Where the roof parks with a house nobody can measure: the declared
 *  value of cli_windowDem, which is what cooling_controller publishes there.
 *  @note Never cli_windowN_tgt, whose declared value is where the roof sits
 *  with nothing driving at all: shut. */
inline float safe_open(Live& f) {
    const Vars::Descriptor& d = f.bank.descriptor(f.ref("cli_windowDem"));
    return Vars::as_f32(d.type, d.def);
}

/** @brief Publish one field-read row as if the bus had just answered it.
 *  publish() judges the band and writes the row's quality, so one call says the
 *  instrument answered. distrust() rehearses one that has gone. */
inline void field(Live& f, const char* row, float v, uint64_t at = 0) {
    f.bank.var(f.ref(row)).publish(from_f32(T_F32, v), at);
}

/** @brief The house at two levels, and the row derived from them.
 *
 *  The cooling ladder controls on `cli_temp_in` and reports its verdict on the
 *  levels behind it, so stating one without the other states half a house.
 *  Worst case, as temp_rh_zone makes it, and forced, because temp_rh_zone
 *  would overwrite a published value. */
inline void interior(Live& f, float hi, float lo) {
    field(f, "cli_temp_hi", hi);
    field(f, "cli_temp_lo", lo);
    force(f, "cli_temp_in", hi > lo ? hi : lo);
}

/** @brief A weather station reporting fine weather, so a case that is not
 *  about the interlocks is not shut in by one. */
inline void fine_weather(Live& f) {
    force(f, "cli_rain_mmph", 0.0f);
    force(f, "cli_wind_mps", 1.0f);
    force(f, "cli_windGust_mps", 2.0f);
}

/** @brief Clear the interlocks that default to their forbidding state.
 *
 *  A roof must not open on a supply nobody has measured, and the skirt must not
 *  humidify into air that has only just dried. dc_watch and humidity clear
 *  them; a rig without those on the beat never does. */
inline void interlocks_clear(Live& f) {
    // Forced, not set: dc_watch and humidity own these two and republish them
    // every scan. pwr_upsLowBat declares true (an unreadable terminal is not a
    // healthy supply), so a rig with no local terminals reaches a real lockout
    // within its debounce.
    force(f, "sys_supply_lockout", false);
    force(f, "cli_rotLockout", false);
}
/** @brief The ST plant on its own, which is what a law's own case needs.
 *
 *  There is no program object to hold a rig open on, so the fixture drives the
 *  resource. PHASE_INPUT runs too: a local terminal reaches a law as a row, and
 *  a meter reading an edge count off one gets zero for the life of a rig that
 *  never publishes it. */
struct StRig {
    Live                live;
    /** PHASE_INPUT's job, which the beat does before PHASE_CONTROL: the local
     *  terminals' own rows, an edge count among them. */
    Io::LocalInputs     terminals{ Botanik::SetupContext(live.reg), live.bank };
    Plc::Runtime::Plant     st{ Botanik::SetupContext(live.reg), live.bank };

    StRig() {
        d.terminals = &terminals;
        st.wire();
        d.st = &st;
        // Seeding a derived row achieves nothing: temp_rh_sensors overwrites
        // the levels from the probes, strat_sensor the gap from the levels,
        // destrat the demand from the gap. Seed the probes or seed nothing.
        //
        // Both levels alike, so the gap is zero and destrat asks for nothing.
        // Its demand is one of the four floors under the fan ladder, and a case
        // about any other floor needs it quiet.
        for (uint8_t i = 1; i <= 3; i++) {
            char n[24];
            snprintf(n, sizeof n, "cli_temp_hi%u", (unsigned)i);
            live.bank.var(live.ref(n)).publish(from_f32(T_F32, 20.0f), 0);
            snprintf(n, sizeof n, "cli_temp_lo%u", (unsigned)i);
            live.bank.var(live.ref(n)).publish(from_f32(T_F32, 20.0f), 0);
            snprintf(n, sizeof n, "cli_rh_hi%u", (unsigned)i);
            live.bank.var(live.ref(n)).publish(from_f32(T_F32, 50.0f), 0);
            snprintf(n, sizeof n, "cli_rh_lo%u", (unsigned)i);
            live.bank.var(live.ref(n)).publish(from_f32(T_F32, 50.0f), 0);
        }
    }

    struct Driver {
        Io::LocalInputs* terminals = nullptr;
        Plc::Runtime::Plant*     st = nullptr;
        void step(uint64_t now_ms) {
            terminals->step(now_ms, Tasks::PHASE_INPUT);
            st->scan(now_ms);
        }
    } d;
};

}}  // namespace tests::site_programs

#endif // TESTS_PROGRAM_RIG_H
