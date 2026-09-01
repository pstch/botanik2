/**
 * @file plant_fixture.h
 * @brief The whole plant on one bank: every program registered, driven beat by
 *        beat against the weather and the pipes a case describes.
 *
 * Shared by the two scenario files. INLINE rather than an anonymous namespace:
 * each file uses about half of this, and an unused static function warns where
 * an unused inline one does not.
 */

#ifndef TESTS_PLANT_FIXTURE_H
#define TESTS_PLANT_FIXTURE_H

#include <cstdint>
#include <cmath>
#include <cstdio>
#include "doctest.h"
#include "libbotanik.h"
#include "libbotanik_site.h"
#include BOTANIK_SITE_HEADER
#include "scan_fixture.h"
#include "scenario_trace.h"

namespace site_scenarios {

using namespace Botanik;
using namespace Botanik::Vars;

using TestScan::Cycle;
using TestScan::Live;

namespace fixture {

/** @brief Every program the site runs, in registration order.
 *
 *  The order IS the data flow: instruments, then the ladders that read their
 *  rows, then the actuators that read the demands. A case that registered them
 *  in another order would be testing a plant this one does not build.
 *
 *  Two are deliberately absent. the SUN TRACKER needs a fix and a wall clock, and
 *  day_phase would then follow the hour the suite happens to run at - so the
 *  phase is an INPUT here, stated by the scenario, and the program that
 *  would otherwise own it has cases of its own.
 */
struct Plant;
inline void hold_weather(Plant& p);

struct Plant {
    Live f;

    /** @brief PHASE_INPUT's job: the local terminals' own rows - an edge count
     *  among them - then a field row's quality reaching ST as a companion.
     *  Before the plant, as the beat has it. */
    Io::LocalInputs            terminals;
    Vars::QualityMirror        mirror;
    /** @brief The ST plant: every one of the site's programs. */
    Plc::Runtime::Plant            st;

    Plc::Program* all[1];    ///< nothing left to hold: every program is ST
    Cycle*        cycle = nullptr;
    uint64_t      t = 0;
    /** @brief What this plant's environment is, held every beat. The weather
     *  alone by default; a case about water sets the one that drives the
     *  meters too, since a field row nobody refreshes goes stale. */
    void (*hold)(Plant&) = hold_weather;

    Plant()
        : terminals(ctx(), io()), mirror(ctx(), io()), st(ctx(), io()) {
        // No C++ programs left: every one of them is ST, and the RESOURCE
        // below runs the lot. The Cycle stays so a case can still step "the
        // programs" without knowing there are none.
        cycle = new Cycle(all, 0, &f.bank, f.reg);
        st.wire();
        mirror.wire();
    }
    ~Plant() { delete cycle; }

    SetupContext ctx() { return Botanik::SetupContext(f.reg); }
    Vars::Bank&  io()  { return f.bank; }

    /** @brief ONE beat, in the order the firmware has: the field's quality
     *  reaches ST as companions, the ST plant runs, then the C++ programs. A
     *  case that steps the cycle alone runs neither of the first two. */
    void beat() {
        t += 100;
        terminals.step(t, Tasks::PHASE_INPUT);
        mirror.scan();
        st.scan(t);
        cycle->step(t);
    }

    /** @brief Beats, at the site's own period. */
    void run(uint32_t beats) {
        for (uint32_t i = 0; i < beats; i++) beat();
    }
    /** @brief ...and the same, holding the environment where the case put it:
     *  a field row goes stale without a driver refreshing it. */
    void weather_for(uint32_t beats) {
        for (uint32_t i = 0; i < beats; i++) {
            hold(*this); beat();
        }
    }

    void probe(const char* fmt, uint8_t n, float v) {
        char name[28];
        for (uint8_t i = 1; i <= n; i++) {
            snprintf(name, sizeof name, fmt, (unsigned)i);
            f.bank.var(f.ref(name)).publish(from_f32(T_F32, v), t);
        }
    }
    void put(const char* row, float v)  { f.bank.var(f.ref(row)).publish(from_f32(T_F32, v), t); }
    void set(const char* row, float v)  { f.bank.var(f.ref(row)).set(v); }
    void phase(Site::DayPhase p)        { /* day_phase owns it; see hold_day() */ (void)p; }

    float num(const char* row)  { return f.bank.var(f.ref(row)).as_f32(); }
    bool  on(const char* row)   { return f.bank.var(f.ref(row)).as_bool(); }
    bool  valid(const char* row){ return f.bank.var(f.ref(row)).valid(); }
    Vars::VarRef ref(const char* n) { return f.bank.find(n); }
};

/** @brief The interior, the space under it, and the sky - the four things a
 *  scenario actually sets. Held every beat, because a field row that stops
 *  being refreshed goes stale and every law then reports itself degraded. */
struct Weather {
    float ceiling;
    float floor_;
    float crawlspace;
    float exterior;
    float rh;          ///< percent, as the instruments report it
    float rh_ext;      ///< ...and outside, which is a different air mass
    float rain;        ///< mm/h
    float wind;        ///< m/s
};

inline Weather& g_wx() { static Weather v = {}; return v; }

/** @brief The phase a scenario is set in. HELD every beat: day_phase owns the
 *  row and republishes it, aninline Site::DayPhase& g_phase() { static Site::DayPhase v = Site::PHASE_DAY; return v; }. */
inline Site::DayPhase& g_phase() { static Site::DayPhase v = Site::PHASE_DAY; return v; }

/** @brief Minutes since sunrise, for the laws that measure the day from it. */
inline int& g_from_rise() { static int v = 0; return v; }

inline void hold_weather(Plant& p) {
    p.probe("cli_temp_hi%u", 3, g_wx().ceiling);
    p.probe("cli_temp_lo%u", 3, g_wx().floor_);
    p.probe("cli_temp_cs%u", 3, g_wx().crawlspace);
    p.probe("cli_temp_ext%u", 3, g_wx().exterior);
    p.probe("cli_rh_hi%u", 3, g_wx().rh);
    p.probe("cli_rh_lo%u", 3, g_wx().rh);
    p.probe("cli_rh_cs%u", 3, g_wx().rh);
    p.probe("cli_rh_ext%u", 3, g_wx().rh_ext);
    p.probe("cli_rain%u_mmph", 2, g_wx().rain);
    p.probe("cli_wind%u_mps", 2, g_wx().wind);
    p.probe("cli_windGust%u_mps", 2, g_wx().wind * 1.5f);
    p.probe("cli_irrad%u_Wpm2", 2, 0.0f);
    // The supplies, so nothing is held off for want of power.
    p.put("grid_LN_V", 230.0f);
    p.put("grid_Hz", 50.0f);
    p.put("ats_V", 230.0f);
    p.put("ats_Hz", 50.0f);
    p.put("pwr_dc1_V", 24.0f);
    p.put("pwr_dc2_V", 24.0f);
    p.put("pwr_upsBat_V", 26.0f);
    p.put("pwr_genBat_V", 26.0f);
    p.f.bank.var(p.ref("pwr_upsLowBat")).publish(from_bool(T_BOOL, false), p.t);
    // FORCED, because day_phase is in this plant now and rewrites the row on
    // every scan from a sun the scenario is not simulating. An override
    // shadows the live value, which is what a forced phase means anyway.
    p.f.bank.override_set(p.f.bank.var(p.ref("cli_phase")),
                          Vars::from_i64(Vars::T_U8, (int64_t)g_phase()));
    p.f.bank.override_set(p.f.bank.var(p.ref("cli_phase_qlt")),
                          Vars::from_bool(Vars::T_BOOL, true));
    // sun_watch is not in this plant, and the morning watering slot is measured
    // from sunrise: the case states where in the day it is, the same way it
    // states the phase.
    p.f.bank.var(p.ref("sol_fromRise_min")).set_i((int64_t)g_from_rise());
}

/**
 * @brief The hydraulics a scenario puts behind the valves.
 *
 * The weather for the water: what the inlet delivers while it is open, how much
 * of it the tray gives back down the drain, and what passes with everything
 * shut. Nothing here decides anything - it reads the valves the cycle has
 * driven and answers at the two meters, exactly as the pipework would.
 */
struct Plumbing {
    float fill_Lpm;     ///< what arrives at the inlet meter while filling
    float drain_frac;   ///< of the dose, how much comes back out
    float leak_Lpm;     ///< ...and what passes with every valve shut
};

/** @brief What a tray gives back while it is draining, in Lpm. The tray's
 *  business, not the main's, so it is not part of a Plumbing. */
const float DRAIN_Lpm = 30.0f;

const Plumbing SOUND_CIRCUIT    = { 40.0f, 0.80f, 0.0f };
const Plumbing CLOGGED_TRAY     = { 40.0f, 0.10f, 0.0f };
const Plumbing LEAKING_MANIFOLD = { 40.0f, 0.80f, 1.0f };
const Plumbing DEAD_MAIN        = {  0.0f, 0.00f, 0.0f };

inline Plumbing& g_pipe() { static Plumbing v = SOUND_CIRCUIT; return v; }
inline float& g_owed() { static float v = 0.0f; return v; }    ///< litres delivered but not yet a whole pulse

inline Site::IrrState irr_state(Plant& p) {
    return (Site::IrrState)p.f.bank.var(p.ref("irr_state")).as_i64();
}

inline void hold_water(Plant& p) {
    hold_weather(p);
    const Site::IrrState st = irr_state(p);
    const bool inlet_open = (st == Site::IRR_FILLING || st == Site::IRR_FLUSHING);

    // The inlet meter is a PULSE TRAIN, a whole litre a pulse: carry the
    // fraction rather than rounding it away, or anything slower than ten
    // litres a second meters as nothing at all.
    const float per_pulse = p.num("irr_input_qtyPerImp");
    g_owed() += (inlet_open ? g_pipe().fill_Lpm : g_pipe().leak_Lpm) / 600.0f;
    while (per_pulse > 0.0f && g_owed() >= per_pulse) {
        HAL::Pulse::inject(TestScan::pulse_line(p.f, "irr_inCount"), 1);
        g_owed() -= per_pulse;
    }

    // The outlet is a 4-20 mA loop, so what the pipework answers with is a
    // RATE. Draining, the tray gives back its share of the dose and then stops
    // - and the inlet is shut, so nothing is arriving meanwhile. Flushing, what
    // goes in comes straight back out: same water, same rate, no tray between
    // the two ends.
    float out = 0.0f;
    if (st == Site::IRR_DRAINING &&
        p.num("irr_drained_L") < g_pipe().drain_frac * p.num("irr_volume_L"))
        out = DRAIN_Lpm;
    else if (st == Site::IRR_FLUSHING)
        out = g_pipe().fill_Lpm;
    p.put("irr_out_Lpm", out);
}

/** @brief Beats, then a snapshot. Every water snapshot goes through this.
 *
 *  A meter answers over a window - the inlet differentiates over five seconds,
 *  the outlet averages and then deadbands - so a reading taken on the beat a
 *  valve moved still describes the state BEFORE it. Ten seconds is longer than
 *  either window; a flush lasts ten and gets eight.
 */
inline void settle_meters(Plant& p, uint32_t beats = 100) { p.weather_for(beats); }

/** @brief Put @p pipe behind the valves for the rest of the case, and make the
 *  plant hold both the weather and the water every beat. */
inline void plumbing(Plant& p, const Plumbing& pipe) {
    // Bare, not REQUIREd: a channel another case already attached answers false.
    g_pipe() = pipe;
    g_owed() = 0.0f;
    p.hold = hold_water;
}

/** @brief A settled plant in the weather the case describes: long enough for
 *  the probe filters to seed and the PID to have an opinion. */
inline void settle(Plant& p, const Weather& wx, uint32_t beats = 600) {
    g_wx() = wx;
    p.weather_for(beats);
}

/** @brief Where in the day it is, in minutes from sunrise. */
inline void force_rise(Plant& p, int16_t minutes) {
    g_from_rise() = minutes;
    p.f.bank.var(p.ref("sol_fromRise_min")).set_i((int64_t)minutes);
}

/** @brief The day phase, which the sun tracker cannot produce without a fix.
 *  Forced here, because a scenario is about the weather and not about NTP. */
inline void force_phase(Plant& p, Site::DayPhase phase) {
    g_phase() = phase;
    p.f.bank.override_set(p.f.bank.var(p.ref("cli_phase")),
                          Vars::from_i64(Vars::T_U8, (int64_t)phase));
    p.f.bank.override_set(p.f.bank.var(p.ref("cli_phase_qlt")),
                          Vars::from_bool(Vars::T_BOOL, true));
}

/** @brief The plant, as the report shows it: the rows an operator would look at
 *  to answer "what is it doing, and why". Written only when a trace is asked
 *  for, and only from a case that has just asserted on the same values. */
inline void snapshot(Plant& p, const char* moment) {
    static const char* const ROWS[] = {
        "cli_temp_in", "cli_temp_ext", "cli_temp_cs", "cli_rh_hi",
        "cli_dpMargin_degC", "cli_targetTemp_degC", "cli_phase",
        "cli_coolDem", "cli_stratDem", "cli_humidDem", "cli_rotVentDem",
        "cli_windowDem", "cli_window1_pos", "cli_screen", "cli_screenMoving",
        "cli_intr1", "cli_fanSpeed", "cli_skirtDuty", "cli_skirtValve",
        "cli_intakeCoolOk", "cli_groundHeatOk", "cli_intakeDryOk",
        "cli_rainLockout", "cli_windLockout", "cli_windowCap",
        "sys_supply_lockout", "alarm_weather_storm",
        "alarm_rot", "alarm_too_hot", "alarm_too_cold", "alarm_sns_ceiling",
    };
    ScenarioTrace::moment(moment);
    for (const char* r : ROWS) ScenarioTrace::row(p.f.bank, r);
}

/** @brief ...and the same for a scenario about water: the circuit, the valves
 *  it has driven, and what the two meters counted. */
inline void snapshot_water(Plant& p, const char* moment) {
    static const char* const ROWS[] = {
        "cli_temp_ext", "cli_phase", "sol_fromRise_min",
        "irr_vpdAvg_kPa", "irr_mod", "irr_period_days",
        "irr_state", "irr_activeTray", "irr_servedToday",
        "irr_volume_L", "irr_delivered_L", "irr_drained_L",
        "irr_inFlow_Lpm", "irr_inTotal_L", "irr_outFlow_Lpm", "irr_outTotal_L",
        "irr_input_ro1", "irr_output_ro1", "irr_tray1", "irr_tray2",
        "irr_today", "irr_forceNow", "irr_forceOn_tray5", "irr_forceOff_tray1",
        "irr_last_tray1",
        "alarm_irr_fill", "alarm_irr_drain", "alarm_irr_drain_tray1",
        "alarm_water_leak", "alarm_water_pressure", "sys_supply_lockout",
    };
    ScenarioTrace::moment(moment);
    for (const char* r : ROWS) ScenarioTrace::row(p.f.bank, r);
}

/** @brief The weather a case is holding, as the report shows it. */
inline void trace_weather(const Weather& wx, Site::DayPhase phase) {
    char buf[48];
    const char* const PHASE[] = { "NIGHT", "COLD_BANK", "DAWN_BREEZE",
                                  "DAY", "HEAT_BANK", "DUSK_BREEZE" };
    snprintf(buf, sizeof buf, "%.1f degC", (double)wx.ceiling);
    ScenarioTrace::weather("ceiling", buf);
    snprintf(buf, sizeof buf, "%.1f degC", (double)wx.floor_);
    ScenarioTrace::weather("floor", buf);
    snprintf(buf, sizeof buf, "%.1f degC", (double)wx.crawlspace);
    ScenarioTrace::weather("crawlspace", buf);
    snprintf(buf, sizeof buf, "%.1f degC", (double)wx.exterior);
    ScenarioTrace::weather("outside", buf);
    snprintf(buf, sizeof buf, "%.0f %% / %.0f %% outside", (double)wx.rh,
             (double)wx.rh_ext);
    ScenarioTrace::weather("humidity", buf);
    snprintf(buf, sizeof buf, "%.1f mm/h, wind %.1f m/s", (double)wx.rain,
             (double)wx.wind);
    ScenarioTrace::weather("sky", buf);
    ScenarioTrace::weather("phase", PHASE[(int)phase]);
}

/** @brief Put the plant in one weather and one phase, and say so in the
 *  report: what every scenario does before it asserts anything. */
inline void hold(Plant& p, const Weather& wx, Site::DayPhase phase) {
    settle(p, wx);
    force_phase(p, phase);
    trace_weather(wx, phase);
}

const Weather HOT_AFTERNOON = { 32.0f, 29.0f, 18.0f, 30.0f, 45.0f, 40.0f, 0.0f, 1.0f };
const Weather COLD_NIGHT    = {  9.0f,  8.0f, 14.0f,  4.0f, 70.0f, 80.0f, 0.0f, 1.0f };
const Weather DAMP_NIGHT    = { 14.0f, 13.5f, 13.0f,  8.0f, 96.0f, 75.0f, 0.0f, 1.0f };
const Weather DRY_SPELL     = { 28.0f, 26.0f, 20.0f, 30.0f, 15.0f, 20.0f, 0.0f, 1.0f };

// The end of a mild night, which is when this greenhouse waters: warm enough
// that the dawn gate is open, and damp enough that the modulation is well
// under its summer figure.
const Weather MILD_DAWN = { 16.0f, 15.0f, 14.0f, 13.0f, 70.0f, 80.0f, 0.0f, 1.0f };
const Weather COLD_DAWN = { 11.0f, 10.0f, 12.0f,  8.0f, 70.0f, 80.0f, 0.0f, 1.0f };
// The same hour of the same day, one damp and one parched: what the irrigation
// ladder is for, and the only difference between the two runs below.
const Weather DAMP_MORNING = { 18.0f, 17.0f, 15.0f, 14.0f, 92.0f, 90.0f, 0.0f, 1.0f };
const Weather DRY_MORNING  = { 26.0f, 25.0f, 18.0f, 24.0f, 25.0f, 25.0f, 0.0f, 1.0f };

inline bool cycle_filling(Plant& p)  { return irr_state(p) == Site::IRR_FILLING; }
inline bool cycle_soaking(Plant& p)  { return irr_state(p) == Site::IRR_SOAKING; }
inline bool cycle_draining(Plant& p) { return irr_state(p) == Site::IRR_DRAINING; }
inline bool cycle_flushing(Plant& p) { return irr_state(p) == Site::IRR_FLUSHING; }
inline bool cycle_idle(Plant& p)     { return irr_state(p) == Site::IRR_IDLE; }
inline bool second_tray(Plant& p) {
    return irr_state(p) == Site::IRR_FILLING &&
           p.num("irr_activeTray") >= 2.0f;
}
inline bool day_spent(Plant& p) {
    return p.num("irr_servedToday") >= 2.0f && irr_state(p) == Site::IRR_IDLE;
}
inline bool filling_five(Plant& p) {
    return irr_state(p) == Site::IRR_FILLING && p.num("irr_activeTray") >= 5.0f;
}
inline bool filling_one(Plant& p) {
    return irr_state(p) == Site::IRR_FILLING &&
           p.num("irr_activeTray") == doctest::Approx(1.0f);
}
inline bool fill_blamed(Plant& p)  { return p.on("alarm_irr_fill"); }
inline bool drain_blamed(Plant& p) { return p.on("alarm_irr_drain"); }
inline bool leaking(Plant& p)      { return p.on("alarm_water_leak"); }

/** @brief Beats until @p done, holding the weather, or 0 if it never happens.
 *  A budget rather than a fixed run: what a scenario wants to say is "within
 *  ten minutes", not "on beat 4137". */
inline uint32_t run_until(Plant& p, bool (*done)(Plant&), uint32_t budget) {
    for (uint32_t i = 1; i <= budget; i++) {
        p.hold(p);
        // The WHOLE beat. It used to drive the cycle alone, back when the
        // irrigation laws were all C++; the meters and the modulation are on
        // the resource now, so a run that skips it meters nothing.
        p.beat();
        if (done(p)) return i;
    }
    return 0;
}

/** @brief An evening: the house losing heat at a couple of degrees a minute,
 *  which is about what this greenhouse does once the sun is off it. */
inline void cooling_evening(Plant& p) {
    if (g_wx().ceiling > 12.0f) {
        g_wx().ceiling  -= 0.004f;
        g_wx().floor_   -= 0.004f;
        g_wx().exterior -= 0.004f;
    }
    hold_weather(p);
}

inline bool roof_shut(Plant& p)  { return p.num("cli_window1_pos") < 0.1f; }
inline bool fabric_out(Plant& p) { return p.on("cli_screen"); }

}  // namespace fixture

/** @brief Narrate a step: into the scenario report, and into doctest's context
 *  so a failure says which step it was. */
#define SAY(text) ScenarioTrace::note(text); INFO(text)

}  // namespace site_scenarios

#endif // TESTS_PLANT_FIXTURE_H
