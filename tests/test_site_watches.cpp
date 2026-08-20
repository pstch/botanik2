/**
 * @file test_site_watches.cpp
 * @brief The gh_mlb41 WATCH programs: the three supply watches, and the two
 *        that turn a reading crossing a line into an alarm.
 *
 * All of them are the same shape - empty run(), everything in observe() - so
 * they read together and apart from the control laws in
 * test_site_programs.cpp.
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

namespace site_watches {

using namespace Botanik;
using namespace Botanik::Vars;

using TestScan::Cycle;
using TestScan::Live;

namespace {
    /** @brief Publish one field-read row as if the bus had just answered it. */
    void field(Live& f, const char* row, float v, uint64_t at = 0) {
        f.bank.var(f.ref(row)).publish(from_f32(T_F32, v), at);
    }

    /** @brief The UPS low-battery contact, as the local input scan publishes
     *  it. Every dc_watch case has to say: the row FAILS ASSERTED, so leaving
     *  it alone holds the plant. */
    void contact(Live& f, bool low, uint64_t at = 0) {
        f.bank.var(f.ref("pwr_upsLowBat")).publish(from_bool(T_BOOL, low), at);
    }

    /** @brief A healthy utility feed, so a case that is not about the grid is
     *  not derailed by one. */
    void good_grid(Live& f) {
        field(f, "grid_LN_V", 230.0f);
        field(f, "grid_total_A", 12.0f);
        field(f, "grid_total_W", 2760.0f);
        field(f, "grid_Hz", 50.0f);
    }
}

TEST_CASE("ac grid: the meter is mirrored onto rows an external master can read") {
    Live f;
    Site::AcGridWatch w(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &w };
    Cycle d(progs, 1, &f.bank, f.reg);

    good_grid(f);
    d.step(100);

    INFO("a field-read row carries no TCP register; the status mirror is why "
         "the supply is visible off the box at all");
    CHECK(f.bank.get(f.bank.resolve_in<float>("sys_grid_V")) == doctest::Approx(230.0f));
    CHECK(f.bank.get(f.bank.resolve_in<float>("sys_grid_A")) == doctest::Approx(12.0f));
    CHECK(f.bank.get(f.bank.resolve_in<float>("sys_grid_W")) == doctest::Approx(2760.0f));
    CHECK(f.bank.get(f.bank.resolve_in<float>("sys_grid_Hz")) == doctest::Approx(50.0f));
    CHECK(f.bank.get(f.bank.resolve_in<bool>("sys_grid_ok")));
    CHECK_FALSE(f.bank.var(f.ref("alarm_ac_grid")).b());
}

TEST_CASE("ac grid: a reading that has not moved does not rewrite its row") {
    Live f;
    Site::AcGridWatch w(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &w };
    Cycle d(progs, 1, &f.bank, f.reg);

    good_grid(f);
    d.step(100);

    INFO("the meter's last digit never settles, and every write is a telemetry "
         "publish and a register update");
    field(f, "grid_LN_V", 230.2f, 100);
    d.step(200);
    CHECK(f.bank.get(f.bank.resolve_in<float>("sys_grid_V")) == doctest::Approx(230.0f));

    field(f, "grid_LN_V", 231.0f, 200);
    d.step(300);
    CHECK(f.bank.get(f.bank.resolve_in<float>("sys_grid_V")) == doctest::Approx(231.0f));
}

TEST_CASE("ac grid: out of band alarms, and a meter that never answered does not") {
    Live f;
    Site::AcGridWatch w(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &w };
    Cycle d(progs, 1, &f.bank, f.reg);

    INFO("bring-up: the bus has not got round to the meter, which is not a fault");
    f.bank.var(f.ref("grid_LN_V")).invalidate(Q_NO_DATA);
    f.bank.var(f.ref("grid_Hz")).invalidate(Q_NO_DATA);
    d.step(100);
    CHECK_FALSE(f.bank.var(f.ref("alarm_ac_grid")).b());
    CHECK_FALSE(f.bank.get(f.bank.resolve_in<bool>("sys_grid_ok")));
    CHECK(w.health() == Plc::Health::DEGRADED);

    good_grid(f);
    d.step(200);
    REQUIRE_FALSE(f.bank.var(f.ref("alarm_ac_grid")).b());

    INFO("...but once it HAS answered, silence is exactly the fault to raise");
    f.bank.var(f.ref("grid_LN_V")).invalidate(Q_NO_DATA);
    d.step(300);
    CHECK(f.bank.var(f.ref("alarm_ac_grid")).b());

    INFO("and so is a voltage outside the band");
    good_grid(f);
    field(f, "grid_LN_V", 180.0f, 300);
    d.step(400);
    CHECK(f.bank.var(f.ref("alarm_ac_grid")).b());
    CHECK_FALSE(f.bank.get(f.bank.resolve_in<bool>("sys_grid_ok")));
}

TEST_CASE("ac local: the generator side is judged on its own wider bands") {
    Live f;
    Site::AcLocalWatch w(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &w };
    Cycle d(progs, 1, &f.bank, f.reg);

    field(f, "ats_V", 230.0f);
    field(f, "ats_A", 2.0f);
    field(f, "ats_W", 400.0f);
    field(f, "ats_Hz", 46.0f);
    d.step(100);

    const float grid_lo = f.bank.var(f.ref("pwr_gridLo_Hz")).as_f32();
    const float local_lo = f.bank.var(f.ref("pwr_localLo_Hz")).as_f32();
    REQUIRE(local_lo < grid_lo);        // else the case proves nothing

    INFO("46 Hz would fail the grid's band and passes the generator's: one wanders");
    CHECK(f.bank.get(f.bank.resolve_in<bool>("sys_local_ok")));
    CHECK(f.bank.get(f.bank.resolve_in<float>("sys_local_V")) == doctest::Approx(230.0f));
}

TEST_CASE("dc: the lockout follows the CONTACT, not the battery voltage") {
    Live f;
    Site::DcWatch w(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &w };
    Cycle d(progs, 1, &f.bank, f.reg);

    Vars::RefIn<bool> lock = f.bank.resolve_in<bool>("sys_supply_lockout");
    field(f, "pwr_dc1_V", 24.0f);
    field(f, "pwr_dc2_V", 24.0f);
    field(f, "pwr_upsBat_V", 26.0f);
    field(f, "pwr_genBat_V", 26.0f);
    contact(f, false);

    uint64_t t = 0;
    for (uint32_t i = 0; i < 40; i++) d.step(t += 100);
    CHECK_FALSE(f.bank.get(lock));
    CHECK(f.bank.get(f.bank.resolve_in<bool>("sys_dc_ok")));

    INFO("a battery under its floor WARNS; it does not hold the plant");
    field(f, "pwr_upsBat_V", 21.0f, t);
    for (uint32_t i = 0; i < 40; i++) d.step(t += 100);
    CHECK_FALSE(f.bank.get(lock));
    CHECK_FALSE(f.bank.get(f.bank.resolve_in<bool>("sys_dc_ok")));

    INFO("the UPS knows what its own battery has left, and the contact is it");
    contact(f, true, t);
    for (uint32_t i = 0; i < 40; i++) d.step(t += 100);
    CHECK(f.bank.get(lock));
    CHECK(f.bank.var(f.ref("alarm_dc_ups")).b());
}

TEST_CASE("dc: a bouncing contact does not flick the whole plant off") {
    Live f;
    Site::DcWatch w(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &w };
    Cycle d(progs, 1, &f.bank, f.reg);

    Vars::RefIn<bool> lock = f.bank.resolve_in<bool>("sys_supply_lockout");

    uint64_t t = 0;
    for (uint8_t i = 0; i < 8; i++) {
        contact(f, (i % 2) == 0, t);
        d.step(t += 100);
        CHECK_FALSE(f.bank.get(lock));
    }
    contact(f, true, t);
    for (uint32_t i = 0; i < 30; i++) d.step(t += 100);
    CHECK(f.bank.get(lock));
}

TEST_CASE("dc: a terminal nothing has read holds the plant, by declaration") {
    Live f;
    Site::DcWatch w(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &w };
    Cycle d(progs, 1, &f.bank, f.reg);

    // Nothing publishes the contact here, so the row sits at its declared
    // fallback. That fallback is TRUE on purpose: a terminal the scan has not
    // read is not evidence of a healthy supply.
    uint64_t t = 0;
    for (uint32_t i = 0; i < 40; i++) d.step(t += 100);
    CHECK(f.bank.get(f.bank.resolve_in<bool>("sys_supply_lockout")));
}

TEST_CASE("dc: a rail alarms on its own floor, and each rail on its own row") {
    Live f;
    Site::DcWatch w(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &w };
    Cycle d(progs, 1, &f.bank, f.reg);

    field(f, "pwr_dc1_V", 24.0f);
    field(f, "pwr_dc2_V", 24.0f);
    field(f, "pwr_upsBat_V", 26.0f);
    field(f, "pwr_genBat_V", 26.0f);
    contact(f, false);
    d.step(100);
    REQUIRE_FALSE(f.bank.var(f.ref("alarm_dc_ps1")).b());

    // Held, not poked once: the rail is judged on its rolling average, so a
    // sag has to LAST pwr_dc1_avg_ms to be a fault. That is the whole point of
    // the average - one noisy sample from a divider is not a supply failure.
    uint64_t t = 100;
    for (uint32_t i = 0; i < 2; i++) {
        field(f, "pwr_dc1_V", 18.0f, t);
        d.step(t += 100);
    }
    INFO("two samples into a 1 s average, the rail has not moved far enough to "
         "be under its floor");
    CHECK_FALSE(f.bank.var(f.ref("alarm_dc_ps1")).b());

    for (uint32_t i = 0; i < 60; i++) {
        field(f, "pwr_dc1_V", 18.0f, t);
        d.step(t += 100);
    }
    CHECK(f.bank.var(f.ref("alarm_dc_ps1")).b());
    CHECK_FALSE(f.bank.var(f.ref("alarm_dc_ps2")).b());
    INFO("the mirror lands within the publish deadband of the average, not on "
         "top of it: a write costs telemetry and the last 0.2 V is noise");
    const float mirror = f.bank.get(f.bank.resolve_in<float>("sys_dc1_V"));
    CHECK(mirror >= 18.0f);
    CHECK(mirror <= 18.5f);

    INFO("the generator start battery has an alarm of its own: too low to crank");
    for (uint32_t i = 0; i < 60; i++) {
        field(f, "pwr_genBat_V", 19.0f, t);
        d.step(t += 100);
    }
    CHECK(f.bank.var(f.ref("alarm_generator_battery")).b());
}

TEST_CASE("climate watch: a reading crossing a line, and nothing on one nobody has") {
    Live f;
    Site::ClimateWatch w(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &w };
    Cycle d(progs, 1, &f.bank, f.reg);

    const float hot  = f.bank.var(f.ref("cli_tooHot_degC")).as_f32();
    const float cold = f.bank.var(f.ref("cli_tooCold_degC")).as_f32();
    const float cap  = f.bank.var(f.ref("cli_rotRhCap")).as_f32();

    field(f, "cli_temp_hi", 0.5f * (hot + cold));
    field(f, "cli_rh_hi", 0.5f * cap);
    field(f, "cli_dTStrat_degC", 0.0f);
    d.step(100);
    CHECK_FALSE(f.bank.var(f.ref("alarm_too_hot")).b());
    CHECK_FALSE(f.bank.var(f.ref("alarm_too_cold")).b());
    CHECK_FALSE(f.bank.var(f.ref("alarm_too_humid")).b());

    field(f, "cli_temp_hi", hot + 1.0f, 100);
    field(f, "cli_rh_hi", cap + 0.1f, 100);
    d.step(200);
    CHECK(f.bank.var(f.ref("alarm_too_hot")).b());
    CHECK(f.bank.var(f.ref("alarm_too_humid")).b());
    CHECK_FALSE(f.bank.var(f.ref("alarm_too_cold")).b());

    INFO("an unmeasured reading raises NOTHING: the quorum alarm is what says "
         "the plant cannot be seen, and alarming twice would blunt both");
    f.bank.var(f.ref("cli_temp_hi")).invalidate(Q_NO_DATA);
    d.step(300);
    CHECK_FALSE(f.bank.var(f.ref("alarm_too_hot")).b());
    CHECK(w.health() == Plc::Health::DEGRADED);
}

TEST_CASE("climate watch: the four quorum alarms roll up into one") {
    Live f;
    Site::ClimateWatch w(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &w };
    Cycle d(progs, 1, &f.bank, f.reg);

    field(f, "cli_temp_hi", 20.0f);
    field(f, "cli_rh_hi", 0.5f);
    d.step(100);
    CHECK_FALSE(f.bank.var(f.ref("alarm_sensor_fault")).b());

    INFO("a supervisor wants \"a sensor is faulty\" without knowing there are four");
    f.bank.var(f.ref("alarm_sns_crawlspace")).set(true);
    d.step(200);
    CHECK(f.bank.var(f.ref("alarm_sensor_fault")).b());
}

TEST_CASE("climate watch: the gap alarms past the band the fans are flat out on") {
    Live f;
    Site::ClimateWatch w(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &w };
    Cycle d(progs, 1, &f.bank, f.reg);

    const float top = f.bank.var(f.ref("cli_dTStratHigh_degC")).as_f32();
    field(f, "cli_temp_hi", 20.0f);
    field(f, "cli_rh_hi", 0.5f);

    field(f, "cli_dTStrat_degC", top - 0.5f);
    d.step(100);
    CHECK_FALSE(f.bank.var(f.ref("alarm_strat")).b());

    field(f, "cli_dTStrat_degC", top + 0.5f, 100);
    d.step(200);
    CHECK(f.bank.var(f.ref("alarm_strat")).b());
}

TEST_CASE("weather watch: a storm is what shuts the roof, and silence is a fault") {
    Live f;
    Site::WeatherWatch w(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &w };
    Cycle d(progs, 1, &f.bank, f.reg);

    INFO("bring-up: the bus has not reached the station, which is not a fault");
    f.bank.var(f.ref("cli_wind_mps")).invalidate(Q_NO_DATA);
    f.bank.var(f.ref("cli_rain_mmph")).invalidate(Q_NO_DATA);
    d.step(100);
    CHECK_FALSE(f.bank.var(f.ref("alarm_wx_station")).b());

    field(f, "cli_wind_mps", 2.0f, 100);
    field(f, "cli_rain_mmph", 0.0f, 100);
    d.step(200);
    CHECK_FALSE(f.bank.var(f.ref("alarm_wx_station")).b());
    CHECK_FALSE(f.bank.var(f.ref("alarm_weather_storm")).b());

    INFO("...but once it HAS answered, silence is the fault the roof is shut for");
    f.bank.var(f.ref("cli_wind_mps")).invalidate(Q_NO_DATA);
    d.step(300);
    CHECK(f.bank.var(f.ref("alarm_wx_station")).b());

    INFO("and a storm is the windows' own verdict, not a second threshold");
    f.bank.var(f.ref("cli_windLockout")).set(true);
    d.step(400);
    CHECK(f.bank.var(f.ref("alarm_weather_storm")).b());
}

}  // namespace site_watches
