/**
 * @file program_rig.h
 * @brief The fixtures the site's program tests are written against.
 *
 * One live bank, one registry, and the ST plant over the site's own programs.
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
#include "live_fixture.h"


namespace site_programs {

using namespace Botanik;
using namespace Botanik::Vars;

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

/** @brief Force a row, and mean it.
 *
 *  A row another ST program writes cannot be set: the next scan overwrites it.
 *  An override SHADOWS the live value, so it survives, and it is what an
 *  operator does when they want a law to see a number the plant is not
 *  producing. A case about how a law RESPONDS to an input forces the input;
 *  a case about how the input is PRODUCED drives the chain instead.
 *  @{ */
inline void force(Live& f, const char* row, float v) {
    f.bank.override_set(f.bank.var(f.ref(row)), from_f32(T_F32, v));
    // ...and its companion, where it has one. Forcing a reading without
    // saying it is believable leaves a law reading the number and refusing to
    // act on it, which is never what a case forcing a reading means.
    char qlt[48];
    const int n = snprintf(qlt, sizeof qlt, "%s_qlt", row);
    if (n <= 0 || (size_t)n >= sizeof qlt) return;
    const Vars::VarRef q = f.bank.find(qlt, (uint16_t)n);
    if (q.index != Vars::VAR_NONE)
        f.bank.override_set(f.bank.var(q), from_bool(T_BOOL, true));
}
inline void force(Live& f, const char* row, bool v) {
    f.bank.override_set(f.bank.var(f.ref(row)), from_bool(T_BOOL, v));
}
/** @brief ...and a whole-number row: a state code, a tray number. */
inline void force(Live& f, const char* row, int64_t v) {
    const Vars::Var h = f.bank.var(f.ref(row));
    f.bank.override_set(h, from_i64(h.meta()->type, v));
}
/** @} */

/** @brief Put the day where a case needs it, through the SUN.
 *
 *  cli_phase is day_phase_controller's output now, so setting it directly is
 *  overwritten on the next scan. The boundaries are minutes either side of
 *  sunrise and sunset, and a phase is reached by placing the clock inside its
 *  window - the same way the plant reaches it.
 *  @note The two BANK phases also need their arm, which day_phase derives from
 *  the published extremes. A case wanting one sets cli_prevDayMax_degC or
 *  cli_prevNightMin_degC past its trigger first. */
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
    // A BANK phase also needs its arm, and day_phase derives that from the
    // extremes it accumulates rather than from the row - so the house has to
    // have BEEN hot, or cold, the way the plant reaches one. FORCED, because
    // temp_rh_zone owns cli_temp_in and would put the probes back over it.
    if (p == Site::PHASE_COLD_BANK)
        force(f, "cli_temp_in",
              f.bank.var(f.ref("cli_coldBankTrg_degC")).as_f32() + 5.0f);
    else if (p == Site::PHASE_HEAT_BANK)
        force(f, "cli_temp_in",
              f.bank.var(f.ref("cli_heatBankTrg_degC")).as_f32() - 5.0f);

    // AND the row itself, because both kinds of rig exist: one running
    // day_phase, which derives cli_phase from the two above and would
    // overwrite anything set here, and one that does not, where this is the
    // only thing that puts the plant in a phase at all.
    f.bank.var(f.ref("cli_phase")).set_i((int64_t)p);
    f.bank.var(f.ref("cli_phase_qlt")).set(true);
}

/** @brief Put the plant in the end-of-night watering slot. A cycle may only
 *  START in one of the two the spec allows, so every irrigation case has to
 *  say which one it is in. */
inline void slot(Live& f) {
    put_phase(f, Site::PHASE_DAWN_BREEZE);
    // ...and past the DAWN GATE, which is the other half of the slot: watering
    // cold roots is what it exists to prevent. temp_rh_sensors owns
    // cli_temp_ext and publishes the row's band FLOOR for a zone nothing has
    // ever measured, so a rig that seeds no exterior probe is always too cold
    // to water. A case about the gate itself forces it back down.
    force(f, "cli_temp_ext",
          f.bank.var(f.ref("irr_dawnGate_degC")).as_f32() + 5.0f);
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
 *  stating half a house. WORST CASE, as temp_rh_zone makes it - FORCED,
 *  because temp_rh_zone is ST and would overwrite a published one. */
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

/** @brief Clear the interlocks that DEFAULT to their forbidding state.
 *
 *  A permissive boolean whose whole purpose is to forbid something defaults to
 *  the forbidding answer: a roof must not open on a supply nobody has measured,
 *  and the skirt must not humidify into air that has only just dried. dc_watch
 *  and humidity are what clear them, and a rig without those on the beat never
 *  does - so a case about anything else has to say so. */
inline void interlocks_clear(Live& f) {
    // FORCED, not set: dc_watch and humidity own these two and republish them
    // every scan. The supply one is the sharper case - pwr_upsLowBat declares
    // TRUE ("an unreadable terminal is not a healthy supply"), so a rig with
    // no local terminals reaches a real lockout within its debounce.
    force(f, "sys_supply_lockout", false);
    force(f, "cli_rotLockout", false);
}
/** @brief A live bank with ONE program on the beat, which is what a law's own
 *  case needs: `prog` is the program, `d` the beat over it.
 *
 *  PHASE_INPUT runs too, because the beat has one: a local terminal reaches a
 *  law as a ROW, and a meter reading an edge count off one gets zero for the
 *  life of a rig that never publishes it. */
/** @brief The ST plant on its own.
 *
 *  There is no program object to hold a rig open on - destrat_controller is
 *  generated ST now - so the fixture drives the RESOURCE. `d.step()` keeps the
 *  name every other rig uses so a case reads the same. */
struct StRig {
    Live                live;
    /** PHASE_INPUT's job, which the beat does before PHASE_CONTROL: the local
     *  terminals' own rows - an edge count among them - and a probe that
     *  stopped answering reaching ST as a companion, never as a quality. */
    Io::LocalInputs     terminals{ Botanik::SetupContext(live.reg), live.bank };
    Vars::QualityMirror mirror{ Botanik::SetupContext(live.reg), live.bank };
    Plc::Runtime::Plant     st{ Botanik::SetupContext(live.reg), live.bank };

    StRig() {
        d.terminals = &terminals;
        st.wire();
        mirror.wire();
        d.st = &st;
        d.mirror = &mirror;
        // The whole conditioning chain is ST now, so seeding a DERIVED row
        // achieves nothing: temp_rh_sensors overwrites the levels from the
        // probes, strat_sensor the gap from the levels, destrat the demand
        // from the gap. A case seeds the probes, or it seeds nothing.
        //
        // Both levels alike, so the gap is zero and destrat asks for nothing:
        // its demand is one of the four floors under the fan ladder, and a
        // case about any OTHER floor needs it quiet.
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
        Vars::QualityMirror* mirror = nullptr;
        void step(uint64_t now_ms) {
            terminals->step(now_ms, Tasks::PHASE_INPUT);
            mirror->scan();
            st->scan(now_ms);
        }
    } d;
};

}  // namespace site_programs

#endif // TESTS_PROGRAM_RIG_H
