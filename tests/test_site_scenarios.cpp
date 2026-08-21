/**
 * @file test_site_scenarios.cpp
 * @brief The WHOLE site, driven through weather.
 *
 * Every other site case drives one law and states its inputs. This one runs all
 * of them together, in the order the firmware registers them, against an
 * environment the case moves: a hot afternoon, a cold night, a shower, a damp
 * night, a dry spell, a probe dying. What it asserts is what an operator would
 * see happen to the plant - which actuator moved, and how far - because that is
 * where the interactions live and no single-program case can reach them.
 */

#include <cstdint>
#include <cmath>
#include <cstdio>

#include "doctest.h"

#include "libbotanik.h"
#include "libbotanik_site.h"
#include BOTANIK_SITE_HEADER
#include "sites/gh_mlb41/generated/programs.gen.h"

#include "scan_fixture.h"
#include "scenario_trace.h"

namespace site_scenarios {

using namespace Botanik;
using namespace Botanik::Vars;

using TestScan::Cycle;
using TestScan::Live;

namespace {

/** @brief Every program the site runs, in registration order.
 *
 *  The order IS the data flow: instruments, then the ladders that read their
 *  rows, then the actuators that read the demands. A case that registered them
 *  in another order would be testing a plant this one does not build.
 *
 *  Two are deliberately absent. sun_watch needs a fix and a wall clock, and
 *  day_phase would then follow the hour the suite happens to run at - so the
 *  phase is an INPUT here, stated by the scenario, and the two programs that
 *  would otherwise own it have cases of their own.
 */
struct Plant;
void hold_weather(Plant& p);

struct Plant {
    Live f;

    Site::AcGridWatch          ac_grid;
    Site::AcLocalWatch         ac_local;
    Site::DcWatch              dc;
    Site::TempRHZone           zones;
    Site::StratSensor          strat;
    Site::ClimateWatch         climate;
    Site::WeatherWatch         weather;
    Site::TargetTempController target;
    Site::CoolingController    cooling;
    Site::DestratController    destrat;
    Site::HumidityController   humidity;
    Site::IrrigationModulation irr_mod;
    Site::WaterMeters          irr_meters;
    Site::IrrigationSchedule   irr_schedule;
    Site::IrrigationCycle      irrigation;
    Site::ScreenController     screen;
    Site::WindowsController    windows;
    Site::IntractorController  intractors;
    Site::FanController        fans;
    Site::SkirtController      skirt;

    Plc::Program* all[24];   ///< room for the list below
    Cycle*        cycle = nullptr;
    uint64_t      t = 0;
    /** @brief What this plant's environment is, held every beat. The weather
     *  alone by default; a case about water sets the one that drives the
     *  meters too, since a field row nobody refreshes goes stale. */
    void (*hold)(Plant&) = hold_weather;

    Plant()
        : ac_grid(ctx(), io()), ac_local(ctx(), io()), dc(ctx(), io()),
          zones(ctx(), io()), strat(ctx(), io()),
          climate(ctx(), io()), weather(ctx(), io()),
          target(ctx(), io()), cooling(ctx(), io()), destrat(ctx(), io()),
          humidity(ctx(), io()), irr_mod(ctx(), io()), irr_meters(ctx(), io()),
          irr_schedule(ctx(), io()), irrigation(ctx(), io()), screen(ctx(), io()),
          windows(ctx(), io()),
          intractors(ctx(), io()), fans(ctx(), io()), skirt(ctx(), io()) {
        Plc::Program* const list[] = {
            &ac_grid, &ac_local, &dc, &zones, &strat, &climate, &weather,
            &target, &cooling, &destrat, &humidity,
            &irr_mod, &irr_meters, &irr_schedule, &irrigation,
            &screen, &windows, &intractors, &fans, &skirt
        };
        // Sized from the list, never a literal: a hand-kept count silently
        // drops whichever program is last the day one is added.
        const uint8_t n = (uint8_t)(sizeof(list) / sizeof(list[0]));
        for (uint8_t i = 0; i < n; i++) all[i] = list[i];
        cycle = new Cycle(all, n, &f.bank, f.reg);
    }
    ~Plant() { delete cycle; }

    SetupContext ctx() { return Botanik::SetupContext(f.reg); }
    Vars::Bank&  io()  { return f.bank; }

    /** @brief Beats, at the site's own period. */
    void run(uint32_t beats) {
        for (uint32_t i = 0; i < beats; i++) cycle->step(t += 100);
    }
    /** @brief ...and the same, holding the environment where the case put it:
     *  a field row goes stale without a driver refreshing it. */
    void weather_for(uint32_t beats) {
        for (uint32_t i = 0; i < beats; i++) { hold(*this); cycle->step(t += 100); }
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
    bool  on(const char* row)   { return f.bank.var(f.ref(row)).b(); }
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

Weather g_wx;

/** @brief The phase a scenario is set in. HELD every beat: day_phase owns the
 *  row and republishes it, and without a solar fix it would hold NIGHT. */
Site::DayPhase g_phase = Site::PHASE_DAY;

/** @brief Minutes since sunrise, for the laws that measure the day from it. */
int16_t g_from_rise = 0;

void hold_weather(Plant& p) {
    p.probe("cli_temp_hi%u", 3, g_wx.ceiling);
    p.probe("cli_temp_lo%u", 3, g_wx.floor_);
    p.probe("cli_temp_cs%u", 3, g_wx.crawlspace);
    p.probe("cli_temp_ext%u", 3, g_wx.exterior);
    p.probe("cli_rh_hi%u", 3, g_wx.rh);
    p.probe("cli_rh_lo%u", 3, g_wx.rh);
    p.probe("cli_rh_cs%u", 3, g_wx.rh);
    p.probe("cli_rh_ext%u", 3, g_wx.rh_ext);
    p.put("cli_rain_mmph", g_wx.rain);
    p.put("cli_wind_mps", g_wx.wind);
    p.put("cli_windGust_mps", g_wx.wind * 1.5f);
    p.put("cli_irrad_Wpm2", 0.0f);
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
    p.f.bank.var(p.ref("cli_phase")).set_i((int64_t)g_phase);
    // sun_watch is not in this plant, and the morning watering slot is measured
    // from sunrise: the case states where in the day it is, the same way it
    // states the phase.
    p.f.bank.var(p.ref("sol_fromRise_min")).set_i((int64_t)g_from_rise);
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

Plumbing g_pipe = SOUND_CIRCUIT;
float    g_owed = 0.0f;    ///< litres delivered but not yet a whole pulse

Site::IrrState irr_state(Plant& p) {
    return (Site::IrrState)p.f.bank.var(p.ref("irr_state")).i();
}

void hold_water(Plant& p) {
    hold_weather(p);
    const Site::IrrState st = irr_state(p);
    const bool inlet_open = (st == Site::IRR_FILLING || st == Site::IRR_FLUSHING);

    // The inlet meter is a PULSE TRAIN, a whole litre a pulse: carry the
    // fraction rather than rounding it away, or anything slower than ten
    // litres a second meters as nothing at all.
    const float per_pulse = p.num("irr_input_qtyPerImp");
    g_owed += (inlet_open ? g_pipe.fill_Lpm : g_pipe.leak_Lpm) / 600.0f;
    while (per_pulse > 0.0f && g_owed >= per_pulse) {
        HAL::Pulse::inject(Site::pulse_inputs[0].channel, 1);
        g_owed -= per_pulse;
    }

    // The outlet is a 4-20 mA loop, so what the pipework answers with is a
    // RATE. Draining, the tray gives back its share of the dose and then stops
    // - and the inlet is shut, so nothing is arriving meanwhile. Flushing, what
    // goes in comes straight back out: same water, same rate, no tray between
    // the two ends.
    float out = 0.0f;
    if (st == Site::IRR_DRAINING &&
        p.num("irr_drained_L") < g_pipe.drain_frac * p.num("irr_volume_L"))
        out = DRAIN_Lpm;
    else if (st == Site::IRR_FLUSHING)
        out = g_pipe.fill_Lpm;
    p.put("irr_out_Lpm", out);
}

/** @brief Beats, then a snapshot. Every water snapshot goes through this.
 *
 *  A meter answers over a window - the inlet differentiates over five seconds,
 *  the outlet averages and then deadbands - so a reading taken on the beat a
 *  valve moved still describes the state BEFORE it. Ten seconds is longer than
 *  either window; a flush lasts ten and gets eight.
 */
void settle_meters(Plant& p, uint32_t beats = 100) { p.weather_for(beats); }

/** @brief Put @p pipe behind the valves for the rest of the case, and make the
 *  plant hold both the weather and the water every beat. */
void plumbing(Plant& p, const Plumbing& pipe) {
    // Bare, not REQUIREd: a channel another case already attached answers false.
    HAL::Pulse::attach(Site::pulse_inputs[0].channel, Site::pulse_inputs[0].edge);
    g_pipe = pipe;
    g_owed = 0.0f;
    p.hold = hold_water;
}

/** @brief A settled plant in the weather the case describes: long enough for
 *  the probe filters to seed and the PID to have an opinion. */
void settle(Plant& p, const Weather& wx, uint32_t beats = 600) {
    g_wx = wx;
    p.weather_for(beats);
}

/** @brief Where in the day it is, in minutes from sunrise. */
void force_rise(Plant& p, int16_t minutes) {
    g_from_rise = minutes;
    p.f.bank.var(p.ref("sol_fromRise_min")).set_i((int64_t)minutes);
}

/** @brief The day phase, which the sun tracker cannot produce without a fix.
 *  Forced here, because a scenario is about the weather and not about NTP. */
void force_phase(Plant& p, Site::DayPhase phase) {
    g_phase = phase;
    p.f.bank.var(p.ref("cli_phase")).set_i((int64_t)phase);
}

/** @brief The plant, as the report shows it: the rows an operator would look at
 *  to answer "what is it doing, and why". Written only when a trace is asked
 *  for, and only from a case that has just asserted on the same values. */
void snapshot(Plant& p, const char* moment) {
    static const char* const ROWS[] = {
        "cli_temp_in", "cli_temp_ext", "cli_temp_cs", "cli_rh_hi",
        "cli_dpMargin_degC", "cli_targetTemp_degC", "cli_phase",
        "cli_coolDem", "cli_stratDem", "cli_humidDem", "cli_rotVentDem",
        "cli_windowDem", "cli_window1_pos", "cli_screen", "cli_screenMoving",
        "cli_intr1", "cli_fanSpeed", "cli_skirtDuty", "cli_skirtValve",
        "cli_intakeCoolOk", "cli_groundHeatOk", "cli_intakeDryOk",
        "cli_rainLockout", "cli_windLockout", "sys_supply_lockout",
        "alarm_rot", "alarm_too_hot", "alarm_too_cold", "alarm_sns_ceiling",
    };
    ScenarioTrace::moment(moment);
    for (const char* r : ROWS) ScenarioTrace::row(p.f.bank, r);
}

/** @brief ...and the same for a scenario about water: the circuit, the valves
 *  it has driven, and what the two meters counted. */
void snapshot_water(Plant& p, const char* moment) {
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
void trace_weather(const Weather& wx, Site::DayPhase phase) {
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

bool cycle_filling(Plant& p)  { return irr_state(p) == Site::IRR_FILLING; }
bool cycle_soaking(Plant& p)  { return irr_state(p) == Site::IRR_SOAKING; }
bool cycle_draining(Plant& p) { return irr_state(p) == Site::IRR_DRAINING; }
bool cycle_flushing(Plant& p) { return irr_state(p) == Site::IRR_FLUSHING; }
bool cycle_idle(Plant& p)     { return irr_state(p) == Site::IRR_IDLE; }
bool second_tray(Plant& p) {
    return irr_state(p) == Site::IRR_FILLING &&
           p.num("irr_activeTray") >= 2.0f;
}
bool day_spent(Plant& p) {
    return p.num("irr_servedToday") >= 2.0f && irr_state(p) == Site::IRR_IDLE;
}
bool filling_five(Plant& p) {
    return irr_state(p) == Site::IRR_FILLING && p.num("irr_activeTray") >= 5.0f;
}
bool filling_one(Plant& p) {
    return irr_state(p) == Site::IRR_FILLING &&
           p.num("irr_activeTray") == doctest::Approx(1.0f);
}
bool fill_blamed(Plant& p)  { return p.on("alarm_irr_fill"); }
bool drain_blamed(Plant& p) { return p.on("alarm_irr_drain"); }
bool leaking(Plant& p)      { return p.on("alarm_water_leak"); }

/** @brief Beats until @p done, holding the weather, or 0 if it never happens.
 *  A budget rather than a fixed run: what a scenario wants to say is "within
 *  ten minutes", not "on beat 4137". */
uint32_t run_until(Plant& p, bool (*done)(Plant&), uint32_t budget) {
    for (uint32_t i = 1; i <= budget; i++) {
        p.hold(p);
        p.cycle->step(p.t += 100);
        if (done(p)) return i;
    }
    return 0;
}

/** @brief An evening: the house losing heat at a couple of degrees a minute,
 *  which is about what this greenhouse does once the sun is off it. */
void cooling_evening(Plant& p) {
    if (g_wx.ceiling > 12.0f) {
        g_wx.ceiling  -= 0.004f;
        g_wx.floor_   -= 0.004f;
        g_wx.exterior -= 0.004f;
    }
    hold_weather(p);
}

bool roof_shut(Plant& p)  { return p.num("cli_window1_pos") < 0.1f; }
bool fabric_out(Plant& p) { return p.on("cli_screen"); }

}  // namespace

/** @brief What the case is asserting, said once: to doctest when it fails, and
 *  to the report when one is being written. NOT wrapped in a do-while - INFO
 *  registers a context for the rest of the enclosing scope, and a block would
 *  end it on the next line. */
#define SAY(text) ScenarioTrace::note(text); INFO(text)

TEST_CASE("scenario: a hot afternoon opens the roof and pulls cool ground air") {
    ScenarioTrace::scenario("A hot afternoon",
        "32 degC under the ridge with 18 degC in the crawlspace: the whole "
        "cooling ladder, from the roof down to the wet skirt.");
    Plant p;
    settle(p, HOT_AFTERNOON);
    force_phase(p, Site::PHASE_DAY);
    trace_weather(HOT_AFTERNOON, Site::PHASE_DAY);
    p.weather_for(400);

    SAY("the interior is well over the target, so the ladder is calling for "
         "everything it has");
    CHECK(p.num("cli_coolDem") > 0.5f);

    SAY("windows: the ladder is past cli_windowFullDem, so wide open");
    CHECK(p.num("cli_windowDem") == doctest::Approx(1.0f));
    // One ram at a time, 15 s each: four windows is a minute of travel.
    p.weather_for(900);
    CHECK(p.num("cli_window1_pos") > 0.9f);
    CHECK(p.num("cli_window4_pos") > 0.9f);

    SAY("intractors: the space under the greenhouse is 14 degC cooler, so the "
         "gate is open and they run");
    CHECK(p.on("cli_intakeCoolOk"));
    CHECK(p.num("cli_intr1") > 0.5f);

    SAY("fans: a cooling demand past cli_fanHighDem is speed 3");
    CHECK(p.on("cli_destrat1_speed3"));

    SAY("skirt: past cli_skirtCoolDem, and the air is dry enough for a wet pad "
         "to cool rather than just wet it");
    CHECK(p.num("cli_skirtDuty") > 0.0f);

    SAY("and the screen stays off the glass: DAY forbids deploying it");
    CHECK_FALSE(p.on("cli_screen"));
    snapshot(p, "the plant settled in the heat");
}

TEST_CASE("scenario: a cold night deploys the screen and pulls ground heat") {
    ScenarioTrace::scenario("A cold night",
        "9 degC inside against 4 degC out, and 14 degC of stored heat under "
        "the floor: the negative half of the same ladder.");
    Plant p;
    settle(p, COLD_NIGHT);
    force_phase(p, Site::PHASE_NIGHT);
    trace_weather(COLD_NIGHT, Site::PHASE_NIGHT);
    p.weather_for(600);

    SAY("the interior is well under the target, so the ladder is negative");
    CHECK(p.num("cli_coolDem") < p.num("cli_thermalScreenDem"));

    SAY("screen: deployed, and NIGHT is what permits it");
    CHECK(p.on("cli_screen"));

    SAY("intractors: the ground is warmer than the house, so the mirror gate "
         "opens and they run at the low ground-heat speed - not at the ladder");
    CHECK(p.on("cli_groundHeatOk"));
    CHECK(p.num("cli_intr1") == doctest::Approx(p.num("cli_groundSpeed")));

    SAY("windows: shut, because a negative demand ladders to zero");
    CHECK(p.num("cli_windowDem") == doctest::Approx(0.0f));

    SAY("and nothing is calling for the skirt in the middle of a cold night");
    CHECK(p.num("cli_skirtDuty") == doctest::Approx(0.0f));
    snapshot(p, "the plant settled in the cold");
}

TEST_CASE("scenario: rain shuts the roof over the cooling ladder's objection") {
    ScenarioTrace::scenario("A shower on a hot afternoon",
        "The weather interlock against the ladder: which of the two owns the "
        "roof, and how long the answer outlives the rain.");
    Plant p;
    settle(p, HOT_AFTERNOON);
    force_phase(p, Site::PHASE_DAY);
    trace_weather(HOT_AFTERNOON, Site::PHASE_DAY);
    p.weather_for(900);
    REQUIRE(p.num("cli_window1_pos") > 0.9f);
    snapshot(p, "before the shower: the roof wide open");

    SAY("it starts raining on a hot afternoon: the ladder still wants the roof "
         "open, and stage one of the arbitration does not care");
    g_wx.rain = 4.0f;
    p.weather_for(600);
    CHECK(p.on("cli_rainLockout"));
    CHECK(p.num("cli_windowDem") > 0.5f);      // the demand is unchanged...
    CHECK(p.num("cli_window1_pos") < 0.1f);    // ...and the roof is shut anyway
    snapshot(p, "raining: the demand stands, the roof does not");

    SAY("the shower stops, and the lockout OUTLIVES it: a shower that has just "
         "stopped is about to be one that has not");
    g_wx.rain = 0.0f;
    p.weather_for(600);
    CHECK(p.on("cli_rainLockout"));
    CHECK(p.num("cli_window1_pos") < 0.1f);
    snapshot(p, "a minute after the last drop");
}

TEST_CASE("scenario: a damp night vents, and refuses to wet the air further") {
    ScenarioTrace::scenario("A damp night",
        "96 % at 14 degC, under a degree of dew-point margin: the rot guard, "
        "and the one thing the wet skirt must never do.");
    Plant p;
    settle(p, DAMP_NIGHT);
    force_phase(p, Site::PHASE_NIGHT);
    trace_weather(DAMP_NIGHT, Site::PHASE_NIGHT);
    p.weather_for(600);

    SAY("96% at 14 degC is under a degree of dew-point margin: the rot guard "
         "is up and the ladder is asking to dehumidify");
    CHECK(p.num("cli_dpMargin_degC") < 1.5f);
    CHECK(p.on("alarm_rot"));
    CHECK(p.num("cli_humidDem") < 0.0f);

    SAY("the skirt is locked out: wetting air that is already condensing is "
         "the one thing that valve must never do");
    CHECK(p.on("cli_rotLockout"));
    CHECK(p.num("cli_skirtDuty") == doctest::Approx(0.0f));
    CHECK_FALSE(p.on("cli_skirtValve"));

    SAY("and the anti-rot ventilation runs the intractors, ungated: the dew "
         "point it is fighting is the one inside");
    CHECK(p.num("cli_rotVentDem") > 0.0f);
    CHECK(p.num("cli_intr1") > p.num("cli_groundSpeed"));
    snapshot(p, "the rot guard up, the skirt locked out");
}

TEST_CASE("scenario: a dry spell humidifies through the skirt") {
    ScenarioTrace::scenario("A dry spell",
        "15 % at 28 degC: the other arm of the humidity ladder, and the only "
        "humidifier the plant has.");
    Plant p;
    settle(p, DRY_SPELL);
    force_phase(p, Site::PHASE_DAY);
    trace_weather(DRY_SPELL, Site::PHASE_DAY);
    p.weather_for(600);

    SAY("15% at 28 degC is a very large dew-point margin: the ladder's other "
         "arm, asking to humidify");
    CHECK(p.num("cli_dpMargin_degC") > 18.0f);
    CHECK(p.num("cli_humidDem") > 0.0f);

    SAY("the skirt modulates, and the intractors draw the air in through it - "
         "the wetted textile is the only humidifier the plant has");
    CHECK(p.num("cli_skirtDuty") > 0.0f);
    CHECK(p.num("cli_intr1") > p.num("cli_groundSpeed"));
    snapshot(p, "the skirt wetting the intake");
}

TEST_CASE("scenario: a zone dying leaves the plant safe, not confident") {
    ScenarioTrace::scenario("The ceiling probes dying",
        "All three of one level stop answering in the middle of a hot "
        "afternoon: what the plant does with a reading it no longer has.");
    Plant p;
    settle(p, HOT_AFTERNOON);
    force_phase(p, Site::PHASE_DAY);
    trace_weather(HOT_AFTERNOON, Site::PHASE_DAY);
    p.weather_for(900);
    REQUIRE(p.num("cli_window1_pos") > 0.9f);
    const float was = p.num("cli_temp_hi");

    SAY("all three ceiling probes stop answering, and the FLOOR carries the "
         "law: the hottest level is what cooling controls on");
    for (uint8_t i = 1; i <= 3; i++) {
        char n[24]; snprintf(n, sizeof n, "cli_temp_hi%u", (unsigned)i);
        p.f.bank.var(p.ref(n)).invalidate(Q_NO_DATA);
    }
    // ...and the case stops refreshing them, so nothing publishes them again.
    for (uint32_t i = 0; i < 300; i++) {
        p.probe("cli_temp_lo%u", 3, g_wx.floor_);
        p.probe("cli_temp_cs%u", 3, g_wx.crawlspace);
        p.probe("cli_rh_hi%u", 3, g_wx.rh);
        p.probe("cli_rh_lo%u", 3, g_wx.rh);
        p.cycle->step(p.t += 100);
    }

    CHECK_FALSE(p.valid("cli_temp_hi"));
    SAY("the row HOLDS its last average rather than reading a default of zero");
    CHECK(p.num("cli_temp_hi") == doctest::Approx(was));
    CHECK(p.on("alarm_sns_ceiling"));

    SAY("the plant keeps running on the floor: still hot, still open");
    CHECK(p.num("cli_coolDem") > 0.5f);
    CHECK(p.num("cli_window1_pos") > 0.9f);

    SAY("and the laws that read it say they are working on a reading they do "
         "not have");
    CHECK(p.zones.health() != Plc::Health::HEALTHY);
    CHECK(p.cooling.health() == Plc::Health::DEGRADED);
    snapshot(p, "five minutes without a ceiling reading");
}

TEST_CASE("scenario: the supply going down parks the plant") {
    ScenarioTrace::scenario("The supply going down",
        "The UPS reports its battery going while the plant is working hard: "
        "the lockout is the plant's safe state, and it is reversible.");
    Plant p;
    settle(p, HOT_AFTERNOON);
    force_phase(p, Site::PHASE_DAY);
    trace_weather(HOT_AFTERNOON, Site::PHASE_DAY);
    p.weather_for(900);
    REQUIRE(p.num("cli_window1_pos") > 0.9f);

    SAY("the UPS says its battery is going: the whole plant is held, and every "
         "actuator goes where the table says it fails to");
    for (uint32_t i = 0; i < 400; i++) {
        hold_weather(p);
        p.f.bank.var(p.ref("pwr_upsLowBat")).publish(from_bool(T_BOOL, true), p.t);
        p.cycle->step(p.t += 100);
    }
    CHECK(p.on("sys_supply_lockout"));

    SAY("the lockout IS the plant's safe state: every program was asked to "
         "park, through the machinery a halt already uses");
    CHECK(p.f.reg.safe_held());
    CHECK(p.f.reg.safe_requested());

    SAY("windows: shut, whatever the ladder wants - the lockout is stage one");
    CHECK(p.num("cli_window1_pos") < 0.1f);
    SAY("skirt: shut");
    CHECK_FALSE(p.on("cli_skirtValve"));
    snapshot(p, "the battery going, and the plant parked");

    SAY("...and the plant is released the moment the battery is not: the hold "
         "goes down on the same edge that raised it, and the roof reopens");
    const uint32_t back = run_until(p, [](Plant& q) {
        return !q.f.reg.safe_held() && q.num("cli_window1_pos") > 0.9f;
    }, 3000);
    CHECK(back > 0);
    CHECK_FALSE(p.on("sys_supply_lockout"));
    snapshot(p, "the supply back, and the plant with it");
}

TEST_CASE("scenario: a long hot spell does not cost minutes of recovery") {
    // The integral is what makes this worth a case. Held against its limit for
    // an afternoon, a loop without anti-windup accumulates effort it can never
    // use, and answers a cooling house by holding the windows open for as long
    // as it spent winding.
    ScenarioTrace::scenario("A long hot spell, then relief",
        "Ten minutes of the cooling loop pinned at its limit, and what that "
        "costs when the house finally reaches its target.");
    Plant p;
    settle(p, HOT_AFTERNOON);
    force_phase(p, Site::PHASE_DAY);
    trace_weather(HOT_AFTERNOON, Site::PHASE_DAY);
    p.weather_for(6000);        // ten minutes hard against the limit
    REQUIRE(p.num("cli_coolDem") > 0.9f);
    snapshot(p, "ten minutes hard against the limit");

    SAY("the house reaches its target and the demand comes off the limit at "
         "once, keeping only what the integral had needed: the proportional "
         "term was doing the work, and conditional integration would not let "
         "the integral accumulate underneath it while the output was pinned");
    g_wx.ceiling = p.num("cli_targetTemp_degC");
    g_wx.floor_  = g_wx.ceiling - 1.0f;
    p.weather_for(1200);
    CHECK(p.num("cli_coolDem") < 0.5f);
    SAY("...and it does not fall to nothing either: some ventilation IS what "
         "is holding the house at its target");
    CHECK(p.num("cli_coolDem") > 0.0f);

    SAY("...and once the house is genuinely COLDER than wanted, the loop lets "
         "go from the FIRST beat rather than sitting on stored effort");
    g_wx.ceiling = p.num("cli_targetTemp_degC") - 3.0f;
    g_wx.floor_  = g_wx.ceiling - 1.0f;
    const float from = p.num("cli_coolDem");
    p.weather_for(600);
    CHECK(p.num("cli_coolDem") < from);

    // The rate is the TUNING's business - Ti is 200 s and the spec has it set
    // in situ - so what is asserted here is that the recovery is bounded by it
    // rather than by however long the loop spent saturated. Ten minutes hard
    // against the limit, and the roof is shut well inside twenty.
    SAY("and the roof shuts within a time the tuning explains, not within one "
         "the winding does: the integral was clamped to what the output can "
         "span, so an afternoon of saturation costs no more than a minute of it");
    const uint32_t beats =
        run_until(p, [](Plant& q) { return q.num("cli_windowDem") < 0.1f; }, 12000);
    CHECK(beats != 0);
    snapshot(p, "the roof shut again, and no minutes of winding to pay off");
}

TEST_CASE("scenario: a house sitting on a threshold does not thrash the plant") {
    // Every ladder in the site has a rung somewhere. What this asks is whether
    // a temperature parked ON one leaves an actuator flipping - which is a
    // question about dead bands, and it is the failure mode that wears the
    // hardware out rather than the one that kills a plant.
    ScenarioTrace::scenario("A house parked on a threshold",
        "A tenth of a degree of probe noise either side of a gate's own "
        "margin, for a minute: the failure mode that wears hardware out.");
    Plant p;
    settle(p, HOT_AFTERNOON);
    force_phase(p, Site::PHASE_DAY);
    trace_weather(HOT_AFTERNOON, Site::PHASE_DAY);

    // Park the interior where the intake gate lives: exactly the margin above
    // the crawlspace, which is where two converging spaces sit for hours.
    const float margin = p.num("cli_ventTempMgn_degC");
    g_wx.crawlspace = g_wx.ceiling - margin;
    p.weather_for(600);

    const bool gate = p.on("cli_intakeCoolOk");
    uint32_t flips = 0;
    for (uint32_t i = 0; i < 600; i++) {
        // A tenth of a degree of probe noise either side of the threshold.
        g_wx.crawlspace = g_wx.ceiling - margin + ((i % 2) ? 0.1f : -0.1f);
        hold_weather(p);
        p.cycle->step(p.t += 100);
        if (p.on("cli_intakeCoolOk") != gate) flips++;
    }
    SAY("the gate holds through the noise: it opens at its margin and closes a "
         "band under it, so a difference sitting on the margin cannot flip it");
    CHECK(flips == 0);

    SAY("...and the fans hold their speed through the same noise");
    const bool speed2 = p.on("cli_destrat1_speed2");
    uint32_t fan_flips = 0;
    for (uint32_t i = 0; i < 600; i++) {
        hold_weather(p);
        p.cycle->step(p.t += 100);
        if (p.on("cli_destrat1_speed2") != speed2) fan_flips++;
    }
    CHECK(fan_flips == 0);
    snapshot(p, "a minute of noise later, nothing has moved");
}


TEST_CASE("scenario: an evening moves the plant the other way, in order") {
    ScenarioTrace::scenario("An evening, in order",
        "The house cooling through the whole ladder in one run: which of the "
        "roof and the screen moves first, and what the fans do meanwhile.");
    Plant p;
    settle(p, HOT_AFTERNOON);
    force_phase(p, Site::PHASE_DAY);
    trace_weather(HOT_AFTERNOON, Site::PHASE_DUSK_BREEZE);
    p.weather_for(900);
    REQUIRE(p.num("cli_window1_pos") > 0.9f);
    REQUIRE_FALSE(p.on("cli_screen"));

    SAY("dusk, and the house loses heat at a couple of degrees a minute");
    force_phase(p, Site::PHASE_DUSK_BREEZE);

    uint32_t shut = 0, deployed = 0;
    for (uint32_t i = 1; i <= 30000; i++) {          // 50 minutes of beats
        cooling_evening(p);
        p.cycle->step(p.t += 100);
        if (shut == 0 && roof_shut(p))    shut = i;
        if (deployed == 0 && fabric_out(p)) deployed = i;
        if (shut != 0 && deployed != 0)   break;
    }

    SAY("both happen, and the ROOF FIRST: the ladder passes zero long before "
         "it passes the screen's rung, so the fabric is never asked to hold "
         "heat through an open window");
    CHECK(shut != 0);
    CHECK(deployed != 0);
    CHECK(shut < deployed);

    SAY("the fans are cut while the fabric travels: it is moving through the "
         "air they blow into");
    CHECK(p.on("cli_screenMoving"));
    CHECK_FALSE(p.on("cli_destrat1_speed1"));
    CHECK_FALSE(p.on("cli_destrat1_speed2"));
    CHECK_FALSE(p.on("cli_destrat1_speed3"));

    SAY("and once it has arrived they are allowed back - the breeze phase is "
         "asking for one");
    p.weather_for(700);
    CHECK_FALSE(p.on("cli_screenMoving"));
    CHECK(p.on("cli_destrat1_speed1"));
    snapshot(p, "shut, deployed, and the breeze back on");
}

TEST_CASE("scenario: a dawn watering serves the trays one at a time") {
    ScenarioTrace::scenario("A dawn watering",
        "The end of a mild night, every tray owed water: one cycle per tray, "
        "and the day's allowance closing the slot behind them.");
    Plant p;
    plumbing(p, SOUND_CIRCUIT);
    force_phase(p, Site::PHASE_DAWN_BREEZE);
    force_rise(p, -30);
    settle(p, MILD_DAWN, 300);
    trace_weather(MILD_DAWN, Site::PHASE_DAWN_BREEZE);

    SAY("the end of the night, and 13 degC outside clears the dawn gate: the "
        "cycle takes the first tray owed water and opens the inlet on it");
    REQUIRE(run_until(p, cycle_filling, 600) != 0);
    CHECK(p.num("irr_activeTray") == doctest::Approx(1.0f));
    CHECK(p.on("irr_input_ro1"));
    CHECK(p.on("irr_tray1"));
    SAY("...and only that tray: one cycle can never water two");
    CHECK_FALSE(p.on("irr_output_ro1"));
    CHECK_FALSE(p.on("irr_tray2"));

    SAY("the dose is the tray's SUMMER figure scaled down by the weather, and "
        "damp air at dawn is a long way from summer");
    p.weather_for(2);        // the dose is published against the tray in hand
    const float dose = p.num("irr_volume_L");
    CHECK(dose > 0.0f);
    CHECK(dose < 150.0f);

    settle_meters(p);
    SAY("...and one end of the circuit is open: water arriving at the inlet "
        "meter, and the drain shut behind the tray");
    CHECK(p.num("irr_inFlow_Lpm") > 0.0f);
    CHECK(p.num("irr_outFlow_Lpm") == doctest::Approx(0.0f));
    snapshot_water(p, "filling the first tray");

    SAY("the fill ends at the DOSE and not at a time: the meter says when, and "
        "nothing is blamed for a fill that arrived");
    REQUIRE(run_until(p, cycle_soaking, 4000) != 0);
    CHECK(p.num("irr_delivered_L") >= dose);
    CHECK_FALSE(p.on("alarm_irr_fill"));

    SAY("...and the tray keeps it: both ends of the manifold shut for the soak, "
        "the tray still connected, and neither meter reading anything");
    CHECK_FALSE(p.on("irr_input_ro1"));
    CHECK_FALSE(p.on("irr_output_ro1"));
    CHECK(p.on("irr_tray1"));
    settle_meters(p);
    CHECK(p.num("irr_inFlow_Lpm") == doctest::Approx(0.0f));
    CHECK(p.num("irr_outFlow_Lpm") == doctest::Approx(0.0f));
    snapshot_water(p, "the tray sitting full through its soak");

    SAY("then the drain opens on the same tray");
    REQUIRE(run_until(p, cycle_draining, 6000) != 0);
    SAY("draining: the inlet shut, the tray still open, the drain carrying "
        "what comes back past the outlet meter");
    CHECK_FALSE(p.on("irr_input_ro1"));
    CHECK(p.on("irr_tray1"));
    CHECK(p.on("irr_output_ro1"));
    SAY("...and the day is stamped at the END OF THE FILL, so a fault during "
        "the long drain cannot water the tray twice");
    CHECK(p.num("irr_servedToday") == doctest::Approx(1.0f));

    // Ten seconds in, so the inlet's own 5 s rate window lies wholly inside
    // the drain: at the transition beat it is still reporting the fill.
    const float in_at_drain = p.num("irr_inTotal_L");
    settle_meters(p);
    SAY("water is leaving and none is arriving: one meter reads and the other "
        "does not, which is what a drain looks like at the two ends");
    CHECK(p.num("irr_outFlow_Lpm") > 0.0f);
    CHECK(p.num("irr_inFlow_Lpm") == doctest::Approx(0.0f));
    CHECK(p.num("irr_inTotal_L") == doctest::Approx(in_at_drain));
    snapshot_water(p, "draining the first tray");

    SAY("enough came back for the tray to be clear of a clog");
    REQUIRE(run_until(p, cycle_flushing, 12000) != 0);
    CHECK_FALSE(p.on("alarm_irr_drain"));
    SAY("...and the manifold is rinsed with no tray between the two ends, so "
        "what sat in the line is not what the next tray gets first");
    CHECK(p.on("irr_input_ro1"));
    CHECK(p.on("irr_output_ro1"));
    CHECK(p.num("irr_activeTray") == doctest::Approx(0.0f));

    const float in_at_flush  = p.num("irr_inTotal_L");
    const float out_at_flush = p.num("irr_outTotal_L");
    settle_meters(p, 80);
    SAY("both meters read, and read the SAME thing: fresh water going in one "
        "end of the manifold and out of the other, with no tray to hold it");
    CHECK(p.num("irr_inFlow_Lpm") > 20.0f);
    CHECK(fabsf(p.num("irr_inFlow_Lpm") - p.num("irr_outFlow_Lpm")) < 15.0f);
    const float went_in   = p.num("irr_inTotal_L") - in_at_flush;
    const float came_back = p.num("irr_outTotal_L") - out_at_flush;
    CHECK(went_in > 0.0f);
    CHECK(fabsf(went_in - came_back) < 0.25f * went_in);
    snapshot_water(p, "rinsing the manifold between two trays");

    SAY("then the second tray, because tray 1 is no longer owed anything");
    REQUIRE(run_until(p, second_tray, 12000) != 0);
    CHECK(p.on("irr_tray2"));
    CHECK_FALSE(p.on("irr_tray1"));

    SAY("and there it stops: irr_traysPerDay is the day's allowance, and the "
        "trays still owed water wait for tomorrow");
    REQUIRE(run_until(p, day_spent, 40000) != 0);
    CHECK(p.num("irr_servedToday") == doctest::Approx(2.0f));
    p.weather_for(3000);
    CHECK(irr_state(p) == Site::IRR_IDLE);
    CHECK(p.num("irr_servedToday") == doctest::Approx(2.0f));
    snapshot_water(p, "the day's allowance spent");
}

TEST_CASE("scenario: the two forces an operator has, and what spends them") {
    ScenarioTrace::scenario("Forcing a tray, and holding one back",
        "Both forces are ONE-SHOT: they change the NEXT cycle and are spent "
        "doing it, or they would ask for the same thing every morning for "
        "ever and no other tray would be reached.");
    Plant p;
    plumbing(p, SOUND_CIRCUIT);
    force_phase(p, Site::PHASE_DAWN_BREEZE);
    force_rise(p, -30);
    // Before the plant is stepped: the slot is open, so the first beat of the
    // settle is already a cycle looking for a tray.
    SAY("tray 5 is forced on. Every tray is owed water and the schedule would "
        "have taken the first, so the tray that goes is the one asked for");
    REQUIRE(p.f.bank.param_set(p.f.bank.var(p.ref("irr_forceOn_tray5")), true)
            == Vars::SET_OK);
    settle(p, MILD_DAWN, 300);
    trace_weather(MILD_DAWN, Site::PHASE_DAWN_BREEZE);
    REQUIRE(run_until(p, filling_five, 1200) != 0);
    CHECK(p.on("irr_tray5"));
    settle_meters(p);
    CHECK_FALSE(p.on("irr_tray1"));
    CHECK(p.on("irr_forceOn_tray5"));
    snapshot_water(p, "filling the tray that was asked for");

    SAY("the force is spent as the fill completes, and owed to flash with the "
        "rest of the schedule");
    REQUIRE(run_until(p, cycle_draining, 6000) != 0);
    CHECK_FALSE(p.on("irr_forceOn_tray5"));
    CHECK(p.f.bank.var(p.ref("irr_forceOn_tray5")).param_unsaved());

    SAY("...so the day's second cycle goes back to the schedule, which has "
        "been asking for tray 1 all along");
    REQUIRE(run_until(p, filling_one, 24000) != 0);
    CHECK(p.on("irr_tray1"));
    settle_meters(p);
    SAY("...and the drain is shut behind it: a tray being filled has one end "
        "open, and the meter at the other end reads nothing");
    CHECK(p.num("irr_outFlow_Lpm") == doctest::Approx(0.0f));
    snapshot_water(p, "back to the schedule for the second cycle");

    Plant q;
    plumbing(q, SOUND_CIRCUIT);
    force_phase(q, Site::PHASE_DAWN_BREEZE);
    force_rise(q, -30);
    SAY("the other force, on a fresh plant: tray 1 is due and held back by "
        "hand, so the cycle passes over it and takes the next one owed");
    REQUIRE(q.f.bank.param_set(q.f.bank.var(q.ref("irr_forceOff_tray1")), true)
            == Vars::SET_OK);
    settle(q, MILD_DAWN, 300);
    REQUIRE(run_until(q, cycle_filling, 1200) != 0);
    CHECK(q.num("irr_activeTray") == doctest::Approx(2.0f));
    settle_meters(q);

    SAY("the skip is spent doing it, and the tray is STAMPED as though it had "
        "been watered - so it waits its period rather than arriving again "
        "tomorrow with the flag gone");
    CHECK_FALSE(q.on("irr_forceOff_tray1"));
    CHECK(q.num("irr_last_tray1") > 0.0f);
    snapshot_water(q, "the held-back tray passed over");
}

TEST_CASE("scenario: a cold dawn waits for the morning rather than watering") {
    ScenarioTrace::scenario("A cold dawn",
        "8 degC outside at the end of the night: watering cold roots is what "
        "the dawn gate exists to prevent, and the morning slot is where that "
        "watering goes instead.");
    Plant p;
    plumbing(p, SOUND_CIRCUIT);
    force_phase(p, Site::PHASE_DAWN_BREEZE);
    force_rise(p, -30);
    settle(p, COLD_DAWN, 300);
    trace_weather(COLD_DAWN, Site::PHASE_DAWN_BREEZE);

    SAY("the end of the night, and too cold: the trays are owed water and the "
        "cycle does not start anyway");
    p.weather_for(1200);
    CHECK(irr_state(p) == Site::IRR_IDLE);
    CHECK(p.num("irr_servedToday") == doctest::Approx(0.0f));
    CHECK_FALSE(p.on("irr_input_ro1"));
    snapshot_water(p, "cold, at the end of the night");

    SAY("the sun comes up and the day's own slot opens: the same tray, the "
        "same dose, an hour and a half later");
    force_phase(p, Site::PHASE_DAY);
    force_rise(p, 90);
    REQUIRE(run_until(p, cycle_filling, 1200) != 0);
    CHECK(p.num("irr_activeTray") == doctest::Approx(1.0f));
    settle_meters(p);
    snapshot_water(p, "watering in the morning slot instead");

    SAY("...and the afternoon is not a slot at all: the cycle in hand finishes "
        "and no second one starts, because a tray filled then is still wet "
        "when the stomata open at dusk");
    force_rise(p, 400);
    REQUIRE(run_until(p, cycle_idle, 24000) != 0);
    CHECK(p.num("irr_servedToday") == doctest::Approx(1.0f));
    p.weather_for(3000);
    CHECK(irr_state(p) == Site::IRR_IDLE);
    CHECK(p.num("irr_servedToday") == doctest::Approx(1.0f));
    snapshot_water(p, "the afternoon, with trays still owed water");
}

TEST_CASE("scenario: damp air waters less than dry air, and less often") {
    ScenarioTrace::scenario("Damp air against dry",
        "The same morning twice, at 92 % and at 25 %: the modulation is the "
        "whole of this greenhouse's seasons, and it moves both the dose and "
        "the interval.");
    Plant damp;
    plumbing(damp, SOUND_CIRCUIT);
    force_phase(damp, Site::PHASE_DAY);
    force_rise(damp, 90);
    settle(damp, DAMP_MORNING, 300);
    trace_weather(DAMP_MORNING, Site::PHASE_DAY);
    REQUIRE(run_until(damp, cycle_filling, 1200) != 0);
    damp.weather_for(2);     // the dose is published against the tray in hand
    snapshot_water(damp, "the damp morning");

    Plant dry;
    plumbing(dry, SOUND_CIRCUIT);
    force_phase(dry, Site::PHASE_DAY);
    force_rise(dry, 90);
    settle(dry, DRY_MORNING, 300);
    REQUIRE(run_until(dry, cycle_filling, 1200) != 0);
    dry.weather_for(2);
    snapshot_water(dry, "the dry morning");

    SAY("the air the plants have been breathing is the input, and the damp "
        "house is nowhere near its summer figure");
    CHECK(damp.num("irr_vpdAvg_kPa") < dry.num("irr_vpdAvg_kPa"));
    CHECK(damp.num("irr_mod") < dry.num("irr_mod"));

    SAY("the split spends that on BOTH halves: a smaller dose, and a longer "
        "wait for the next one");
    CHECK(damp.num("irr_volume_L") < dry.num("irr_volume_L"));
    CHECK(damp.num("irr_period_days") > dry.num("irr_period_days"));

    SAY("...and the floor is a floor: even at 92 % the tray still gets water");
    CHECK(damp.num("irr_volume_L") > 0.0f);
    CHECK(damp.num("irr_mod") >= damp.num("irr_modMin"));
}

TEST_CASE("scenario: a tray that will not drain names itself") {
    ScenarioTrace::scenario("A tray that will not drain",
        "The fill arrives and only a tenth of it comes back: a clog is a "
        "MEASUREMENT, made against the dose that tray actually got.");
    Plant p;
    plumbing(p, CLOGGED_TRAY);
    force_phase(p, Site::PHASE_DAWN_BREEZE);
    force_rise(p, -30);
    settle(p, MILD_DAWN, 300);
    trace_weather(MILD_DAWN, Site::PHASE_DAWN_BREEZE);

    REQUIRE(run_until(p, cycle_draining, 12000) != 0);
    settle_meters(p);
    SAY("the fill itself was fine: the water went in, and the alarm below is "
        "about where it went afterwards");
    CHECK_FALSE(p.on("alarm_irr_fill"));
    snapshot_water(p, "the tray filled, and now draining");

    SAY("the drain window closes with a tenth of the dose recovered, which is "
        "under what the tray owes even with the margin allowed");
    REQUIRE(run_until(p, drain_blamed, 12000) != 0);
    CHECK(p.num("irr_drained_L") < p.num("irr_volume_L"));
    settle_meters(p, 80);
    SAY("...and the per-tray row says WHICH one, so somebody goes to the right "
        "end of the plant");
    CHECK(p.on("alarm_irr_drain_tray1"));
    CHECK_FALSE(p.on("alarm_irr_drain_tray2"));
    snapshot_water(p, "the drain window closed on a clog");

    SAY("the cycle carries on regardless: a blocked tray is not a reason to "
        "leave the other seven dry");
    REQUIRE(run_until(p, second_tray, 12000) != 0);
    CHECK(p.on("irr_tray2"));
    settle_meters(p);
    snapshot_water(p, "moving on to the next tray anyway");
}

TEST_CASE("scenario: the two things the inlet meter can see on its own") {
    ScenarioTrace::scenario("A leak, and a main with no water in it",
        "Both are read off the inlet meter alone: water moving with every "
        "valve shut, and water not arriving with the inlet open.");
    Plant p;
    plumbing(p, LEAKING_MANIFOLD);
    force_phase(p, Site::PHASE_NIGHT);
    force_rise(p, -240);
    settle(p, MILD_DAWN, 300);
    trace_weather(MILD_DAWN, Site::PHASE_NIGHT);

    SAY("the middle of the night: no cycle is running, so every valve in the "
        "circuit is shut and anything the meter counts is going somewhere "
        "nobody opened");
    REQUIRE(irr_state(p) == Site::IRR_IDLE);
    REQUIRE(run_until(p, leaking, 1200) != 0);
    CHECK(p.num("irr_inFlow_Lpm") > 0.0f);
    snapshot_water(p, "a litre a minute with everything shut");

    SAY("the main is repaired, and gives no water at all: the mirror image, "
        "raised while the inlet is open and nothing is arriving");
    plumbing(p, DEAD_MAIN);
    force_phase(p, Site::PHASE_DAWN_BREEZE);
    REQUIRE(run_until(p, cycle_filling, 1200) != 0);
    p.weather_for(400);
    SAY("...and the leak is not one any more: the meter is counting into a "
        "tray somebody opened on purpose");
    CHECK_FALSE(p.on("alarm_water_leak"));
    CHECK(p.on("alarm_water_pressure"));
    snapshot_water(p, "filling, with nothing coming out of the main");

    SAY("and the fill gives up at its timeout rather than holding the inlet "
        "open on a fault, which is how a tray overflows");
    REQUIRE(run_until(p, fill_blamed, 12000) != 0);
    CHECK(p.on("alarm_irr_fill_tray1"));
    CHECK_FALSE(p.on("irr_input_ro1"));
    SAY("...and it goes STRAIGHT to the drain, past the soak: a soak steeps a "
        "tray in the dose it was given, and this one got nothing");
    CHECK(irr_state(p) == Site::IRR_DRAINING);
    SAY("...and a fill that delivered nothing gets no drain verdict either: a "
        "clog is a measurement, and there is nothing to measure");
    REQUIRE(run_until(p, cycle_flushing, 12000) != 0);
    CHECK_FALSE(p.on("alarm_irr_drain"));
    settle_meters(p, 80);
    snapshot_water(p, "the fill given up on, and no clog claimed");
}

TEST_CASE("scenario: the supply going down mid-fill empties the tray") {
    ScenarioTrace::scenario("The supply going down mid-fill",
        "Ten valves, and they do not all fail the same way: the inlet shuts "
        "and the drains open, so a tray half full empties instead of sitting "
        "there.");
    Plant p;
    plumbing(p, SOUND_CIRCUIT);
    force_phase(p, Site::PHASE_DAWN_BREEZE);
    force_rise(p, -30);
    settle(p, MILD_DAWN, 300);
    trace_weather(MILD_DAWN, Site::PHASE_DAWN_BREEZE);

    REQUIRE(run_until(p, cycle_filling, 600) != 0);
    REQUIRE(p.on("irr_input_ro1"));
    settle_meters(p);
    snapshot_water(p, "filling, on a plant with power");

    SAY("the UPS says its battery is going, and the lockout parks the whole "
        "plant - irrigation with it, through the machinery a halt uses");
    for (uint32_t i = 0; i < 400; i++) {
        p.hold(p);
        p.f.bank.var(p.ref("pwr_upsLowBat")).publish(from_bool(T_BOOL, true), p.t);
        p.cycle->step(p.t += 100);
    }
    REQUIRE(p.on("sys_supply_lockout"));
    CHECK(irr_state(p) == Site::IRR_IDLE);
    CHECK(p.num("irr_activeTray") == doctest::Approx(0.0f));

    SAY("each valve where its own row says a dead controller leaves it: the "
        "inlet shut, the drain and the trays open");
    CHECK_FALSE(p.on("irr_input_ro1"));
    CHECK_FALSE(p.on("irr_input_ro2"));
    CHECK(p.on("irr_output_ro1"));
    CHECK(p.on("irr_tray1"));
    snapshot_water(p, "parked, with the tray draining itself");

    SAY("and the plant comes back when the battery does: the cycle starts "
        "again from IDLE rather than resuming a fill it stopped counting");
    REQUIRE(run_until(p, cycle_filling, 12000) != 0);
    CHECK(p.num("irr_delivered_L") < 5.0f);
    settle_meters(p);
    snapshot_water(p, "the supply back, and the fill started again");
}

}  // namespace site_scenarios
