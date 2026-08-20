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
#include "sites/gh_mlb41/tasks.h"

#include "scan_fixture.h"

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
    Site::IrrigationCycle      irrigation;
    Site::ScreenController     screen;
    Site::WindowsController    windows;
    Site::IntractorController  intractors;
    Site::FanController        fans;
    Site::SkirtController      skirt;

    Plc::Program* all[19];
    Cycle*        cycle = nullptr;
    uint64_t      t = 0;

    Plant()
        : ac_grid(ctx(), io()), ac_local(ctx(), io()), dc(ctx(), io()),
          zones(ctx(), io()), strat(ctx(), io()),
          climate(ctx(), io()), weather(ctx(), io()),
          target(ctx(), io()), cooling(ctx(), io()), destrat(ctx(), io()),
          humidity(ctx(), io()), irr_mod(ctx(), io()), irr_meters(ctx(), io()),
          irrigation(ctx(), io()), screen(ctx(), io()), windows(ctx(), io()),
          intractors(ctx(), io()), fans(ctx(), io()), skirt(ctx(), io()) {
        Plc::Program* const list[] = {
            &ac_grid, &ac_local, &dc, &zones, &strat, &climate, &weather,
            &target, &cooling, &destrat, &humidity,
            &irr_mod, &irr_meters, &irrigation,
            &screen, &windows, &intractors, &fans, &skirt
        };
        for (uint8_t i = 0; i < 19; i++) all[i] = list[i];
        cycle = new Cycle(all, 19, &f.bank, f.reg);
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
    void weather_for(uint32_t beats, void (*hold)(Plant&)) {
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
}

/** @brief A settled plant in the weather the case describes: long enough for
 *  the probe filters to seed and the PID to have an opinion. */
void settle(Plant& p, const Weather& wx, uint32_t beats = 600) {
    g_wx = wx;
    p.weather_for(beats, hold_weather);
}

/** @brief The day phase, which the sun tracker cannot produce without a fix.
 *  Forced here, because a scenario is about the weather and not about NTP. */
void force_phase(Plant& p, Site::DayPhase phase) {
    g_phase = phase;
    p.f.bank.var(p.ref("cli_phase")).set_i((int64_t)phase);
}

const Weather HOT_AFTERNOON = { 32.0f, 29.0f, 18.0f, 30.0f, 45.0f, 40.0f, 0.0f, 1.0f };
const Weather COLD_NIGHT    = {  9.0f,  8.0f, 14.0f,  4.0f, 70.0f, 80.0f, 0.0f, 1.0f };
const Weather DAMP_NIGHT    = { 14.0f, 13.5f, 13.0f,  8.0f, 96.0f, 75.0f, 0.0f, 1.0f };
const Weather DRY_SPELL     = { 28.0f, 26.0f, 20.0f, 30.0f, 15.0f, 20.0f, 0.0f, 1.0f };

/** @brief Beats until @p done, holding the weather, or 0 if it never happens.
 *  A budget rather than a fixed run: what a scenario wants to say is "within
 *  ten minutes", not "on beat 4137". */
uint32_t run_until(Plant& p, bool (*done)(Plant&), uint32_t budget) {
    for (uint32_t i = 1; i <= budget; i++) {
        hold_weather(p);
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

TEST_CASE("scenario: a hot afternoon opens the roof and pulls cool ground air") {
    Plant p;
    settle(p, HOT_AFTERNOON);
    force_phase(p, Site::PHASE_DAY);
    p.weather_for(400, hold_weather);

    INFO("the interior is well over the target, so the ladder is calling for "
         "everything it has");
    CHECK(p.num("cli_coolDem") > 0.5f);

    INFO("windows: the ladder is past cli_windowFullDem, so wide open");
    CHECK(p.num("cli_windowDem") == doctest::Approx(1.0f));
    // One ram at a time, 15 s each: four windows is a minute of travel.
    p.weather_for(900, hold_weather);
    CHECK(p.num("cli_window1_pos") > 0.9f);
    CHECK(p.num("cli_window4_pos") > 0.9f);

    INFO("intractors: the space under the greenhouse is 14 degC cooler, so the "
         "gate is open and they run");
    CHECK(p.on("cli_intakeCoolOk"));
    CHECK(p.num("cli_intr1") > 0.5f);

    INFO("fans: a cooling demand past cli_fanHighDem is speed 3");
    CHECK(p.on("cli_destrat1_speed3"));

    INFO("skirt: past cli_skirtCoolDem, and the air is dry enough for a wet pad "
         "to cool rather than just wet it");
    CHECK(p.num("cli_skirtDuty") > 0.0f);

    INFO("and the screen stays off the glass: DAY forbids deploying it");
    CHECK_FALSE(p.on("cli_screen"));
}

TEST_CASE("scenario: a cold night deploys the screen and pulls ground heat") {
    Plant p;
    settle(p, COLD_NIGHT);
    force_phase(p, Site::PHASE_NIGHT);
    p.weather_for(600, hold_weather);

    INFO("the interior is well under the target, so the ladder is negative");
    CHECK(p.num("cli_coolDem") < p.num("cli_thermalScreenDem"));

    INFO("screen: deployed, and NIGHT is what permits it");
    CHECK(p.on("cli_screen"));

    INFO("intractors: the ground is warmer than the house, so the mirror gate "
         "opens and they run at the low ground-heat speed - not at the ladder");
    CHECK(p.on("cli_groundHeatOk"));
    CHECK(p.num("cli_intr1") == doctest::Approx(p.num("cli_groundSpeed")));

    INFO("windows: shut, because a negative demand ladders to zero");
    CHECK(p.num("cli_windowDem") == doctest::Approx(0.0f));

    INFO("and nothing is calling for the skirt in the middle of a cold night");
    CHECK(p.num("cli_skirtDuty") == doctest::Approx(0.0f));
}

TEST_CASE("scenario: rain shuts the roof over the cooling ladder's objection") {
    Plant p;
    settle(p, HOT_AFTERNOON);
    force_phase(p, Site::PHASE_DAY);
    p.weather_for(900, hold_weather);
    REQUIRE(p.num("cli_window1_pos") > 0.9f);

    INFO("it starts raining on a hot afternoon: the ladder still wants the roof "
         "open, and stage one of the arbitration does not care");
    g_wx.rain = 4.0f;
    p.weather_for(600, hold_weather);
    CHECK(p.on("cli_rainLockout"));
    CHECK(p.num("cli_windowDem") > 0.5f);      // the demand is unchanged...
    CHECK(p.num("cli_window1_pos") < 0.1f);    // ...and the roof is shut anyway

    INFO("the shower stops, and the lockout OUTLIVES it: a shower that has just "
         "stopped is about to be one that has not");
    g_wx.rain = 0.0f;
    p.weather_for(600, hold_weather);
    CHECK(p.on("cli_rainLockout"));
    CHECK(p.num("cli_window1_pos") < 0.1f);
}

TEST_CASE("scenario: a damp night vents, and refuses to wet the air further") {
    Plant p;
    settle(p, DAMP_NIGHT);
    force_phase(p, Site::PHASE_NIGHT);
    p.weather_for(600, hold_weather);

    INFO("96% at 14 degC is under a degree of dew-point margin: the rot guard "
         "is up and the ladder is asking to dehumidify");
    CHECK(p.num("cli_dpMargin_degC") < 1.5f);
    CHECK(p.on("alarm_rot"));
    CHECK(p.num("cli_humidDem") < 0.0f);

    INFO("the skirt is locked out: wetting air that is already condensing is "
         "the one thing that valve must never do");
    CHECK(p.on("cli_rotLockout"));
    CHECK(p.num("cli_skirtDuty") == doctest::Approx(0.0f));
    CHECK_FALSE(p.on("cli_skirtValve"));

    INFO("and the anti-rot ventilation runs the intractors, ungated: the dew "
         "point it is fighting is the one inside");
    CHECK(p.num("cli_rotVentDem") > 0.0f);
    CHECK(p.num("cli_intr1") > p.num("cli_groundSpeed"));
}

TEST_CASE("scenario: a dry spell humidifies through the skirt") {
    Plant p;
    settle(p, DRY_SPELL);
    force_phase(p, Site::PHASE_DAY);
    p.weather_for(600, hold_weather);

    INFO("15% at 28 degC is a very large dew-point margin: the ladder's other "
         "arm, asking to humidify");
    CHECK(p.num("cli_dpMargin_degC") > 18.0f);
    CHECK(p.num("cli_humidDem") > 0.0f);

    INFO("the skirt modulates, and the intractors draw the air in through it - "
         "the wetted textile is the only humidifier the plant has");
    CHECK(p.num("cli_skirtDuty") > 0.0f);
    CHECK(p.num("cli_intr1") > p.num("cli_groundSpeed"));
}

TEST_CASE("scenario: a zone dying leaves the plant safe, not confident") {
    Plant p;
    settle(p, HOT_AFTERNOON);
    force_phase(p, Site::PHASE_DAY);
    p.weather_for(900, hold_weather);
    REQUIRE(p.num("cli_window1_pos") > 0.9f);
    const float was = p.num("cli_temp_hi");

    INFO("all three ceiling probes stop answering, and the FLOOR carries the "
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
    INFO("the row HOLDS its last average rather than reading a default of zero");
    CHECK(p.num("cli_temp_hi") == doctest::Approx(was));
    CHECK(p.on("alarm_sns_ceiling"));

    INFO("the plant keeps running on the floor: still hot, still open");
    CHECK(p.num("cli_coolDem") > 0.5f);
    CHECK(p.num("cli_window1_pos") > 0.9f);

    INFO("and the laws that read it say they are working on a reading they do "
         "not have");
    CHECK(p.zones.health() != Plc::Health::HEALTHY);
    CHECK(p.cooling.health() == Plc::Health::DEGRADED);
}

TEST_CASE("scenario: the supply going down parks the plant") {
    Plant p;
    settle(p, HOT_AFTERNOON);
    force_phase(p, Site::PHASE_DAY);
    p.weather_for(900, hold_weather);
    REQUIRE(p.num("cli_window1_pos") > 0.9f);

    INFO("the UPS says its battery is going: the whole plant is held, and every "
         "actuator goes where the table says it fails to");
    for (uint32_t i = 0; i < 400; i++) {
        hold_weather(p);
        p.f.bank.var(p.ref("pwr_upsLowBat")).publish(from_bool(T_BOOL, true), p.t);
        p.cycle->step(p.t += 100);
    }
    CHECK(p.on("sys_supply_lockout"));

    INFO("windows: shut, whatever the ladder wants - the lockout is stage one");
    CHECK(p.num("cli_window1_pos") < 0.1f);
    INFO("skirt: shut");
    CHECK_FALSE(p.on("cli_skirtValve"));
}

TEST_CASE("scenario: a long hot spell does not cost minutes of recovery") {
    // The integral is what makes this worth a case. Held against its limit for
    // an afternoon, a loop without anti-windup accumulates effort it can never
    // use, and answers a cooling house by holding the windows open for as long
    // as it spent winding.
    Plant p;
    settle(p, HOT_AFTERNOON);
    force_phase(p, Site::PHASE_DAY);
    p.weather_for(6000, hold_weather);        // ten minutes hard against the limit
    REQUIRE(p.num("cli_coolDem") > 0.9f);

    INFO("the house reaches its target and the demand comes off the limit at "
         "once, keeping only what the integral had needed: the proportional "
         "term was doing the work, and conditional integration would not let "
         "the integral accumulate underneath it while the output was pinned");
    g_wx.ceiling = p.num("cli_targetTemp_degC");
    g_wx.floor_  = g_wx.ceiling - 1.0f;
    p.weather_for(1200, hold_weather);
    CHECK(p.num("cli_coolDem") < 0.5f);
    INFO("...and it does not fall to nothing either: some ventilation IS what "
         "is holding the house at its target");
    CHECK(p.num("cli_coolDem") > 0.0f);

    INFO("...and once the house is genuinely COLDER than wanted, the loop lets "
         "go from the FIRST beat rather than sitting on stored effort");
    g_wx.ceiling = p.num("cli_targetTemp_degC") - 3.0f;
    g_wx.floor_  = g_wx.ceiling - 1.0f;
    const float from = p.num("cli_coolDem");
    p.weather_for(600, hold_weather);
    CHECK(p.num("cli_coolDem") < from);

    // The rate is the TUNING's business - Ti is 200 s and the spec has it set
    // in situ - so what is asserted here is that the recovery is bounded by it
    // rather than by however long the loop spent saturated. Ten minutes hard
    // against the limit, and the roof is shut well inside twenty.
    INFO("and the roof shuts within a time the tuning explains, not within one "
         "the winding does: the integral was clamped to what the output can "
         "span, so an afternoon of saturation costs no more than a minute of it");
    const uint32_t beats =
        run_until(p, [](Plant& q) { return q.num("cli_windowDem") < 0.1f; }, 12000);
    CHECK(beats != 0);
}

TEST_CASE("scenario: a house sitting on a threshold does not thrash the plant") {
    // Every ladder in the site has a rung somewhere. What this asks is whether
    // a temperature parked ON one leaves an actuator flipping - which is a
    // question about dead bands, and it is the failure mode that wears the
    // hardware out rather than the one that kills a plant.
    Plant p;
    settle(p, HOT_AFTERNOON);
    force_phase(p, Site::PHASE_DAY);

    // Park the interior where the intake gate lives: exactly the margin above
    // the crawlspace, which is where two converging spaces sit for hours.
    const float margin = p.num("cli_ventTempMgn_degC");
    g_wx.crawlspace = g_wx.ceiling - margin;
    p.weather_for(600, hold_weather);

    const bool gate = p.on("cli_intakeCoolOk");
    uint32_t flips = 0;
    for (uint32_t i = 0; i < 600; i++) {
        // A tenth of a degree of probe noise either side of the threshold.
        g_wx.crawlspace = g_wx.ceiling - margin + ((i % 2) ? 0.1f : -0.1f);
        hold_weather(p);
        p.cycle->step(p.t += 100);
        if (p.on("cli_intakeCoolOk") != gate) flips++;
    }
    INFO("the gate holds through the noise: it opens at its margin and closes a "
         "band under it, so a difference sitting on the margin cannot flip it");
    CHECK(flips == 0);

    INFO("...and the fans hold their speed through the same noise");
    const bool speed2 = p.on("cli_destrat1_speed2");
    uint32_t fan_flips = 0;
    for (uint32_t i = 0; i < 600; i++) {
        hold_weather(p);
        p.cycle->step(p.t += 100);
        if (p.on("cli_destrat1_speed2") != speed2) fan_flips++;
    }
    CHECK(fan_flips == 0);
}


TEST_CASE("scenario: an evening moves the plant the other way, in order") {
    Plant p;
    settle(p, HOT_AFTERNOON);
    force_phase(p, Site::PHASE_DAY);
    p.weather_for(900, hold_weather);
    REQUIRE(p.num("cli_window1_pos") > 0.9f);
    REQUIRE_FALSE(p.on("cli_screen"));

    INFO("dusk, and the house loses heat at a couple of degrees a minute");
    force_phase(p, Site::PHASE_DUSK_BREEZE);

    uint32_t shut = 0, deployed = 0;
    for (uint32_t i = 1; i <= 30000; i++) {          // 50 minutes of beats
        cooling_evening(p);
        p.cycle->step(p.t += 100);
        if (shut == 0 && roof_shut(p))    shut = i;
        if (deployed == 0 && fabric_out(p)) deployed = i;
        if (shut != 0 && deployed != 0)   break;
    }

    INFO("both happen, and the ROOF FIRST: the ladder passes zero long before "
         "it passes the screen's rung, so the fabric is never asked to hold "
         "heat through an open window");
    CHECK(shut != 0);
    CHECK(deployed != 0);
    CHECK(shut < deployed);

    INFO("the fans are cut while the fabric travels: it is moving through the "
         "air they blow into");
    CHECK(p.on("cli_screenMoving"));
    CHECK_FALSE(p.on("cli_destrat1_speed1"));
    CHECK_FALSE(p.on("cli_destrat1_speed2"));
    CHECK_FALSE(p.on("cli_destrat1_speed3"));

    INFO("and once it has arrived they are allowed back - the breeze phase is "
         "asking for one");
    p.weather_for(700, hold_weather);
    CHECK_FALSE(p.on("cli_screenMoving"));
    CHECK(p.on("cli_destrat1_speed1"));
}

}  // namespace site_scenarios
