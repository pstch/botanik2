/**
 * @file test_site_programs.cpp
 * @brief The gh_mlb41 control programs: what each law does, driven through the
 *        real scan cycle against the site's real tables.
 *
 * The cycle itself is test_scan.cpp; this file is the site's behaviour. Cases
 * are in signal order, conditioning first and the actuator controllers last.
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

namespace site_programs {

using namespace Botanik;
using namespace Botanik::Vars;

using TestScan::Cycle;
using TestScan::Live;

namespace {
    /** @brief Publish one field-read row as if the bus had just answered it. */
    void field(Live& f, const char* row, float v, uint64_t at = 0) {
        f.bank.var(f.ref(row)).publish(from_f32(T_F32, v), at);
    }

    /** @brief A weather station reporting fine weather, so a case that is not
     *  about the interlocks is not shut in by one. */
    void fine_weather(Live& f) {
        field(f, "cli_rain_mmph", 0.0f);
        field(f, "cli_wind_mps", 1.0f);
        field(f, "cli_windGust_mps", 2.0f);
    }
}

TEST_CASE("zone: the selector drops the outlier and averages the pair that agree") {
    Live f;
    Vars::RefIn<float> hi = f.bank.resolve_in<float>("cli_temp_hi");
    const VarRef p[3] = { f.ref("cli_temp_hi1"), f.ref("cli_temp_hi2"),
                          f.ref("cli_temp_hi3") };

    Site::TempRHZone z(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &z };
    Cycle d(progs, 1, &f.bank, f.reg);

    for (uint8_t i = 0; i < 3; i++) {
        f.bank.var(p[i]).publish(from_f32(T_F32, (i == 2) ? 40.0f : 20.0f + 0.2f * i), 0);
        f.bank.var(f.ref(i == 0 ? "cli_rh_hi1" : i == 1 ? "cli_rh_hi2" : "cli_rh_hi3"))
              .publish(from_f32(T_F32, 0.5f), 0);
    }
    for (uint64_t t = 0; t <= 60000; t += 100) d.step(t);

    INFO("40 C is the outlier; 20.0 and 20.2 are the pair");
    CHECK(f.bank.get(hi) == doctest::Approx(20.1f).epsilon(0.02));
}

TEST_CASE("zone: an invalid probe is excluded, and one good probe still reads") {
    Live f;
    Vars::RefIn<float> hi = f.bank.resolve_in<float>("cli_temp_hi");
    const VarRef p[3] = { f.ref("cli_temp_hi1"), f.ref("cli_temp_hi2"),
                          f.ref("cli_temp_hi3") };

    Site::TempRHZone z(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &z };
    Cycle d(progs, 1, &f.bank, f.reg);

    // One probe answering 25, the other two dead. The dead pair must not vote:
    // they read their declared default, which would drag any average.
    f.bank.var(p[0]).publish(from_f32(T_F32, 25.0f), 0);
    f.bank.var(p[1]).invalidate(Q_NO_DATA);
    f.bank.var(p[2]).invalidate(Q_NO_DATA);
    f.bank.var(f.ref("cli_rh_hi1")).publish(from_f32(T_F32, 0.5f), 0);
    for (uint64_t t = 0; t <= 60000; t += 100) d.step(t);

    CHECK(f.bank.var(hi.r).valid());
    CHECK(f.bank.get(hi) == doctest::Approx(25.0f).epsilon(0.02));
    INFO("and the quorum alarm is up, because two probes are gone");
    CHECK(f.bank.var(f.ref("alarm_sns_ceiling")).b());
}

TEST_CASE("zone: temperature survives losing every humidity probe") {
    // The two quantities carry their own quorum. Coupling them would let one
    // dead RH probe invalidate a temperature three good probes agree on.
    Live f;
    Vars::RefIn<float> t_out = f.bank.resolve_in<float>("cli_temp_hi");
    Vars::RefIn<float> h_out = f.bank.resolve_in<float>("cli_rh_hi");

    Site::TempRHZone z(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &z };
    Cycle d(progs, 1, &f.bank, f.reg);

    const char* temps[3] = { "cli_temp_hi1", "cli_temp_hi2", "cli_temp_hi3" };
    const char* rhs[3]   = { "cli_rh_hi1", "cli_rh_hi2", "cli_rh_hi3" };
    for (uint8_t i = 0; i < 3; i++) {
        f.bank.var(f.ref(temps[i])).publish(from_f32(T_F32, 22.0f), 0);
        f.bank.var(f.ref(rhs[i])).invalidate(Q_NO_DATA);
    }
    for (uint64_t t = 0; t <= 60000; t += 100) d.step(t);

    CHECK(f.bank.var(t_out.r).valid());
    CHECK(f.bank.get(t_out) == doctest::Approx(22.0f).epsilon(0.02));
    CHECK_FALSE(f.bank.var(h_out.r).valid());
}

TEST_CASE("zone: with every probe gone the reading goes invalid, not stale-good") {
    Live f;
    Vars::RefIn<float> hi = f.bank.resolve_in<float>("cli_temp_hi");
    const VarRef p[3] = { f.ref("cli_temp_hi1"), f.ref("cli_temp_hi2"),
                          f.ref("cli_temp_hi3") };

    Site::TempRHZone z(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &z };
    Cycle d(progs, 1, &f.bank, f.reg);

    for (uint8_t i = 0; i < 3; i++)
        f.bank.var(p[i]).publish(from_f32(T_F32, 21.0f), 0);
    for (uint64_t t = 0; t <= 20000; t += 100) d.step(t);
    REQUIRE(f.bank.var(hi.r).valid());

    for (uint8_t i = 0; i < 3; i++) f.bank.var(p[i]).invalidate(Q_NO_DATA);
    for (uint64_t t = 20000; t <= 40000; t += 100) d.step(t);

    INFO("a law gating on quality must see that nothing is measuring this zone");
    CHECK_FALSE(f.bank.var(hi.r).valid());

    // And it reads its DECLARED DEFAULT, not the last filtered number: any
    // quality but GOOD or STALE falls back, so a law that does not test
    // validity still gets the value the row was declared to fail to.
    INFO("and it reads the declared default rather than a stale filtered value");
    CHECK(f.bank.get(hi) == doctest::Approx(
              as_f32(T_F32, f.bank.descriptor(hi.r.index).def)));
    CHECK(f.bank.get(hi) != doctest::Approx(21.0f));
}

TEST_CASE("strat: the gap is unsigned, so an inverted profile still calls the fans") {
    Live f;
    Site::StratSensor st(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &st };
    Cycle d(progs, 1, &f.bank, f.reg);

    Vars::RefIn<float> gap = f.bank.resolve_in<float>("cli_dTStrat_degC");
    const VarRef hi = f.ref("cli_temp_hi"), lo = f.ref("cli_temp_lo");

    f.bank.var(hi).publish(from_f32(T_F32, 24.0f), 0);
    f.bank.var(lo).publish(from_f32(T_F32, 20.0f), 0);
    d.step(100);
    CHECK(f.bank.get(gap) == doctest::Approx(4.0f));

    INFO("a cold roof over a warm floor is as much a case for mixing");
    f.bank.var(hi).publish(from_f32(T_F32, 18.0f), 200);
    f.bank.var(lo).publish(from_f32(T_F32, 22.0f), 200);
    d.step(200);
    CHECK(f.bank.get(gap) == doctest::Approx(4.0f));
}

TEST_CASE("strat: a level nothing measures makes the gap unmeasured, not zero") {
    Live f;
    Site::StratSensor st(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &st };
    Cycle d(progs, 1, &f.bank, f.reg);

    f.bank.var(f.ref("cli_temp_hi")).publish(from_f32(T_F32, 24.0f), 0);
    f.bank.var(f.ref("cli_temp_lo")).invalidate(Q_NO_DATA);
    d.step(100);
    CHECK_FALSE(f.bank.var(f.ref("cli_dTStrat_degC")).valid());
}

TEST_CASE("target: a bank phase only shifts the target when yesterday armed it") {
    Live f;
    Site::TargetTempController tc(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &tc };
    Cycle d(progs, 1, &f.bank, f.reg);

    Vars::RefIn<float> target = f.bank.resolve_in<float>("cli_targetTemp_degC");
    const VarRef phase = f.ref("cli_phase");
    const VarRef cold  = f.ref("cli_coldBankArm");
    const VarRef heat  = f.ref("cli_heatBankArm");
    const float base = f.bank.var(f.ref("cli_coolSet_degC")).as_f32();

    f.bank.var(phase).set_i((int64_t)Site::PHASE_DAY);
    d.step(100);
    CHECK(f.bank.get(target) == doctest::Approx(base));

    INFO("COLD_BANK in force but not armed runs at the ordinary target");
    f.bank.var(phase).set_i((int64_t)Site::PHASE_COLD_BANK);
    f.bank.var(cold).set(false);
    d.step(200);
    CHECK(f.bank.get(target) == doctest::Approx(base));

    INFO("armed, it pulls the target down by the cold offset");
    f.bank.var(cold).set(true);
    d.step(300);
    const float ofs = f.bank.var(f.ref("cli_coldBankOfs_degC")).as_f32();
    CHECK(f.bank.get(target) == doctest::Approx(base - ofs));

    INFO("and HEAT_BANK uses the HEAT offset, not the cold one");
    f.bank.var(phase).set_i((int64_t)Site::PHASE_HEAT_BANK);
    f.bank.var(heat).set(true);
    d.step(400);
    const float hofs = f.bank.var(f.ref("cli_heatBankOfs_degC")).as_f32();
    REQUIRE(hofs != ofs);          // else the case proves nothing
    CHECK(f.bank.get(target) == doctest::Approx(base + hofs));
}

TEST_CASE("target: an aggressive offset is clamped to the safe band") {
    Live f;
    Site::TargetTempController tc(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &tc };
    Cycle d(progs, 1, &f.bank, f.reg);

    f.bank.param_set(f.bank.var(f.ref("cli_coldBankOfs_degC")), 15.0f);
    f.bank.var(f.ref("cli_phase")).set_i((int64_t)Site::PHASE_COLD_BANK);
    f.bank.var(f.ref("cli_coldBankArm")).set(true);
    d.step(100);

    Vars::RefIn<float> target = f.bank.resolve_in<float>("cli_targetTemp_degC");
    CHECK(f.bank.get(target) == doctest::Approx(Site::TargetTempController::TARGET_MIN));
}

TEST_CASE("cooling: the floor carries it when the ceiling goes, and both gone is unknown") {
    Live f;
    Site::CoolingController cc(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &cc };
    Cycle d(progs, 1, &f.bank, f.reg);

    const VarRef dem = f.ref("cli_coolDem");
    field(f, "cli_targetTemp_degC", 20.0f);
    field(f, "cli_temp_hi", 30.0f);
    field(f, "cli_temp_lo", 30.0f);
    // The intake gate reads it; without it the law is DEGRADED for a reason
    // that has nothing to do with this case.
    field(f, "cli_temp_cs", 18.0f);
    d.step(100);
    REQUIRE(f.bank.var(dem).valid());
    REQUIRE(f.bank.var(dem).as_f32() > 0.0f);          // hot: cooling
    CHECK(cc.health() == Plc::Health::HEALTHY);

    INFO("the ceiling is the better place to control from; the floor is a "
         "worse one and a better one than nowhere");
    f.bank.var(f.ref("cli_temp_hi")).invalidate(Q_NO_DATA);
    d.step(200);
    CHECK(f.bank.var(dem).valid());
    CHECK(f.bank.var(dem).as_f32() > 0.0f);
    CHECK(cc.health() == Plc::Health::DEGRADED);

    INFO("with both gone a PID on the defaulted reading would see 0 degC "
         "against the target and call for full HEAT");
    f.bank.var(f.ref("cli_temp_lo")).invalidate(Q_NO_DATA);
    d.step(300);
    CHECK_FALSE(f.bank.var(dem).valid());
    CHECK(cc.health() == Plc::Health::FAULTED);
}

TEST_CASE("cooling: an unknown demand puts every actuator at its own safe state") {
    Live f;
    fine_weather(f);
    Site::WindowsController    win(Botanik::SetupContext(f.reg), f.bank);
    Site::IntractorController  intr(Botanik::SetupContext(f.reg), f.bank);
    Site::ScreenController     scr(Botanik::SetupContext(f.reg), f.bank);
    Site::SkirtController      sk(Botanik::SetupContext(f.reg), f.bank);
    Site::FanController        fan(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &win, &intr, &scr, &sk, &fan };
    Cycle d(progs, 5, &f.bank, f.reg);

    // Everything else says GO: only the cooling demand is missing.
    f.bank.var(f.ref("cli_coolDem")).invalidate(Q_NO_DATA);
    f.bank.var(f.ref("cli_stratDem")).set(0.0f);
    f.bank.var(f.ref("cli_humidDem")).set(0.0f);
    f.bank.var(f.ref("cli_rotVentDem")).set(0.0f);
    field(f, "cli_dpMargin_degC", 20.0f);
    field(f, "cli_temp_hi", 25.0f);
    f.bank.var(f.ref("cli_phase")).set_i((int64_t)Site::PHASE_DAY);
    // The screen traverses once at bring-up whatever it is asked for, and while
    // it does it CUTS the fans - which is right, and would hide what this case
    // is about. Let it finish.
    REQUIRE(f.bank.param_set(f.bank.var(f.ref("cli_screenTravel_sec")), 1.0f)
            == SET_OK);

    uint64_t t = 0;
    for (uint32_t i = 0; i < 40; i++) d.step(t += 100);
    REQUIRE_FALSE(f.bank.var(f.ref("cli_screenMoving")).b());

    INFO("each one substitutes ITS OWN safe state for the input it cannot read, "
         "and not one of the five is simply OFF");
    CHECK(f.bank.get(f.bank.resolve_in<float>("cli_window1_tgt")) ==
          doctest::Approx(Site::WindowsController::SAFE_OPEN));   // part open
    CHECK(f.bank.get(f.bank.resolve_in<float>("cli_intr1")) ==
          doctest::Approx(0.15f));                                // some air
    CHECK(f.bank.get(f.bank.resolve_in<bool>("cli_screen")));     // deployed
    CHECK_FALSE(f.bank.get(f.bank.resolve_in<bool>("cli_skirtValve")));  // shut
    CHECK(f.bank.get(f.bank.resolve_in<bool>("cli_destrat1_speed1")));   // speed 1

    for (Plc::Program* p : progs)
        CHECK(p->health() == Plc::Health::DEGRADED);
}

TEST_CASE("destrat: an unmeasured gap makes the demand unknown, not zero") {
    Live f;
    Site::DestratController dc(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &dc };
    Cycle d(progs, 1, &f.bank, f.reg);

    const VarRef dem = f.ref("cli_stratDem");
    field(f, "cli_dTStrat_degC", 8.0f);
    d.step(100);
    REQUIRE(f.bank.var(dem).valid());
    REQUIRE(f.bank.var(dem).as_f32() == doctest::Approx(1.0f));

    INFO("this row's default is 0, which reads as 'no stratification' and would "
         "stop the fans on a dead probe");
    f.bank.var(f.ref("cli_dTStrat_degC")).invalidate(Q_NO_DATA);
    d.step(200);
    CHECK_FALSE(f.bank.var(dem).valid());
    CHECK(dc.health() == Plc::Health::FAULTED);
}

TEST_CASE("fans: an unknown gap floors them at their safe state, not at rest") {
    Live f;
    Site::FanController fc(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &fc };
    Cycle d(progs, 1, &f.bank, f.reg);

    Vars::RefIn<bool> s1 = f.bank.resolve_in<bool>("cli_destrat1_speed1");
    Vars::RefIn<bool> s3 = f.bank.resolve_in<bool>("cli_destrat1_speed3");

    f.bank.var(f.ref("cli_stratDem")).invalidate(Q_NO_DATA);
    f.bank.var(f.ref("cli_coolDem")).set(0.0f);
    d.step(100);

    INFO("still air is itself a rot risk, so an unknown gap is speed 1");
    CHECK(f.bank.get(s1));
    CHECK(fc.health() == Plc::Health::DEGRADED);

    INFO("...and it is a FLOOR: a cooling demand above it still takes them up");
    f.bank.var(f.ref("cli_coolDem")).set(1.0f);
    d.step(200);
    CHECK_FALSE(f.bank.get(s1));
    CHECK(f.bank.get(s3));

    INFO("the screen still outranks it: a mechanical interlock is resolved "
         "before any demand, and before the safe state the floor comes from");
    f.bank.var(f.ref("cli_coolDem")).set(0.0f);
    f.bank.var(f.ref("cli_screenMoving")).set(true);
    d.step(300);
    CHECK_FALSE(f.bank.get(s1));
    CHECK_FALSE(f.bank.get(s3));
}

TEST_CASE("humidity, rot guard: the ventilation demand rises as the margin falls") {
    Live f;
    Site::HumidityController rg(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &rg };
    Cycle d(progs, 1, &f.bank, f.reg);

    Vars::RefIn<float> dem = f.bank.resolve_in<float>("cli_rotVentDem");
    const VarRef margin = f.ref("cli_dpMargin_degC");
    const float limit = f.bank.var(f.ref("cli_tooHumid_degC")).as_f32();

    f.bank.var(margin).publish(from_f32(T_F32, limit + 2.0f), 0);
    d.step(100);
    CHECK(f.bank.get(dem) == doctest::Approx(0.0f));

    INFO("at the limit itself the ramp has not started");
    f.bank.var(margin).publish(from_f32(T_F32, limit), 100);
    d.step(200);
    CHECK(f.bank.get(dem) == doctest::Approx(0.0f));

    INFO("and it is fully on at the dew point itself, not before");
    f.bank.var(margin).publish(from_f32(T_F32, 0.5f * limit), 200);
    d.step(300);
    const float half = f.bank.get(dem);
    CHECK(half > 0.0f);
    CHECK(half < 1.0f);
    f.bank.var(margin).publish(from_f32(T_F32, 0.0f), 300);
    d.step(400);
    CHECK(f.bank.get(dem) == doctest::Approx(1.0f));
}

TEST_CASE("humidity, rot guard: the lockout outlives the condition, and boots clear") {
    Live f;
    Site::HumidityController rg(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &rg };
    Cycle d(progs, 1, &f.bank, f.reg);

    Vars::RefIn<bool> lock = f.bank.resolve_in<bool>("cli_rotLockout");
    const VarRef rot = f.ref("alarm_rot");
    f.bank.var(f.ref("cli_dpMargin_degC")).publish(from_f32(T_F32, 20.0f), 0);

    INFO("dry air at boot is not locked out: the guard has nothing against it");
    d.step(100);
    CHECK_FALSE(f.bank.get(lock));

    f.bank.var(rot).set(true);
    d.step(200);
    CHECK(f.bank.get(lock));

    INFO("the condition clearing does not release it");
    f.bank.var(rot).set(false);
    d.step(300);
    CHECK(f.bank.get(lock));

    uint64_t t = 300;
    for (uint32_t i = 0; i < 3000; i++) d.step(t += 100);
    CHECK_FALSE(f.bank.get(lock));
}

TEST_CASE("humidity, rot guard: an unmeasured margin ventilates rather than assuming dry air") {
    Live f;
    Site::HumidityController rg(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &rg };
    Cycle d(progs, 1, &f.bank, f.reg);

    f.bank.var(f.ref("cli_dpMargin_degC")).invalidate(Q_NO_DATA);
    d.step(100);
    CHECK(f.bank.get(f.bank.resolve_in<float>("cli_rotVentDem")) == doctest::Approx(1.0f));
    CHECK(f.bank.get(f.bank.resolve_in<bool>("cli_rotLockout")));
    CHECK(rg.health() == Plc::Health::DEGRADED);
}

TEST_CASE("screen: a cold night deploys it, and the traverse cuts the fans") {
    Live f;
    Site::ScreenController sc(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &sc };
    Cycle d(progs, 1, &f.bank, f.reg);

    Vars::RefIn<bool> screen = f.bank.resolve_in<bool>("cli_screen");
    Vars::RefIn<bool> moving = f.bank.resolve_in<bool>("cli_screenMoving");

    f.bank.var(f.ref("cli_phase")).set_i((int64_t)Site::PHASE_DAY);
    f.bank.var(f.ref("cli_temp_hi")).publish(from_f32(T_F32, 25.0f), 0);
    f.bank.var(f.ref("cli_coolDem")).set(0.5f);
    d.step(100);
    CHECK_FALSE(f.bank.get(screen));

    INFO("a cold night deploys it, and the fabric is in the fans' way while it goes");
    f.bank.var(f.ref("cli_phase")).set_i((int64_t)Site::PHASE_NIGHT);
    f.bank.var(f.ref("cli_temp_hi")).publish(from_f32(T_F32, 10.0f), 100);
    d.step(200);
    CHECK(f.bank.get(screen));
    CHECK(f.bank.get(moving));

    INFO("...and stops being in their way once the traverse has run");
    const uint32_t travel = f.bank.var(f.ref("cli_screenTravel_sec")).value().as_u32();
    uint64_t t = 200;
    for (uint32_t i = 0; i < 10 * travel + 5; i++) d.step(t += 100);
    CHECK(f.bank.get(screen));
    CHECK_FALSE(f.bank.get(moving));
}

TEST_CASE("screen: a safe request DEPLOYS it, and it parks only once it is there") {
    Live f;
    Site::ScreenController sc(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &sc };
    Cycle d(progs, 1, &f.bank, f.reg);

    // A warm DAY, so the law itself wants it removed and the safe state is the
    // only thing that could deploy it.
    f.bank.var(f.ref("cli_phase")).set_i((int64_t)Site::PHASE_DAY);
    f.bank.var(f.ref("cli_temp_hi")).publish(from_f32(T_F32, 25.0f), 0);
    f.bank.var(f.ref("cli_coolDem")).set(0.5f);
    uint64_t t = 0;
    for (uint32_t i = 0; i < 700; i++) d.step(t += 100);
    REQUIRE_FALSE(f.bank.get(f.bank.resolve_in<bool>("cli_screen")));

    sc.request_safe(true);
    d.step(t += 100);
    CHECK(f.bank.get(f.bank.resolve_in<bool>("cli_screen")));
    INFO("deployed is COMMANDED here, not reached: the fabric is still travelling");
    CHECK_FALSE(sc.is_parked());

    for (uint32_t i = 0; i < 700; i++) d.step(t += 100);
    CHECK(sc.is_parked());
}

TEST_CASE("windows: exactly one ram is energised at a time") {
    Live f;
    fine_weather(f);
    Site::WindowsController wc(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &wc };
    Cycle d(progs, 1, &f.bank, f.reg);

    f.bank.var(f.ref("cli_coolDem")).set(1.0f);

    uint64_t t = 0;
    for (uint32_t i = 0; i < 800; i++) {
        d.step(t += 100);
        uint8_t energised = 0;
        for (uint8_t w = 1; w <= 4; w++) {
            char opn[24], clo[24];
            snprintf(opn, sizeof(opn), "cli_window%u_opn", (unsigned)w);
            snprintf(clo, sizeof(clo), "cli_window%u_clo", (unsigned)w);
            const bool o = f.bank.var(f.ref(opn)).b();
            const bool c = f.bank.var(f.ref(clo)).b();
            const bool both = o && c;
            REQUIRE_FALSE(both);            // never both coils of one ram
            if (o || c) energised++;
        }
        REQUIRE(energised <= 1);
    }

    INFO("and all four get there in the end");
    for (uint8_t w = 1; w <= 4; w++) {
        char pos[24];
        snprintf(pos, sizeof(pos), "cli_window%u_pos", (unsigned)w);
        CHECK(f.bank.var(f.ref(pos)).as_f32() == doctest::Approx(1.0f).epsilon(0.05));
    }
}

TEST_CASE("windows: rain shuts them, and the lockout outlives the shower") {
    Live f;
    fine_weather(f);
    Site::WindowsController wc(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &wc };
    Cycle d(progs, 1, &f.bank, f.reg);

    Vars::RefIn<float> tgt = f.bank.resolve_in<float>("cli_window1_tgt");
    Vars::RefIn<bool>  rain_lock = f.bank.resolve_in<bool>("cli_rainLockout");
    f.bank.var(f.ref("cli_coolDem")).set(1.0f);

    uint64_t t = 0;
    for (uint32_t i = 0; i < 800; i++) d.step(t += 100);
    REQUIRE(f.bank.get(tgt) == doctest::Approx(1.0f));

    f.bank.var(f.ref("cli_rain_mmph")).publish(from_f32(T_F32, 0.4f), t);
    d.step(t += 100);
    CHECK(f.bank.get(rain_lock));
    CHECK(f.bank.get(tgt) == doctest::Approx(0.0f));

    INFO("the shower stopping does not re-open them");
    f.bank.var(f.ref("cli_rain_mmph")).publish(from_f32(T_F32, 0.0f), t);
    d.step(t += 100);
    CHECK(f.bank.get(rain_lock));

    const uint32_t hold = f.bank.var(f.ref("cli_rainLockout_sec")).value().as_u32();
    for (uint32_t i = 0; i < 10 * hold + 20; i++) d.step(t += 100);
    CHECK_FALSE(f.bank.get(rain_lock));
    CHECK(f.bank.get(tgt) == doctest::Approx(1.0f));
}

TEST_CASE("windows: a gust shuts them, and the mean alone does not") {
    Live f;
    fine_weather(f);
    Site::WindowsController wc(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &wc };
    Cycle d(progs, 1, &f.bank, f.reg);

    Vars::RefIn<bool> wind_lock = f.bank.resolve_in<bool>("cli_windLockout");
    const float shut = f.bank.var(f.ref("cli_windShut_mps")).as_f32();
    f.bank.var(f.ref("cli_coolDem")).set(1.0f);

    f.bank.var(f.ref("cli_wind_mps")).publish(from_f32(T_F32, shut - 1.0f), 0);
    d.step(100);
    CHECK_FALSE(f.bank.get(wind_lock));

    INFO("the PEAK is what tears a window off its ram, not the average");
    f.bank.var(f.ref("cli_windGust_mps")).publish(from_f32(T_F32, shut + 1.0f), 100);
    d.step(200);
    CHECK(f.bank.get(wind_lock));
    CHECK(f.bank.get(f.bank.resolve_in<float>("cli_window1_tgt")) ==
          doctest::Approx(0.0f));
}

TEST_CASE("windows: an unmeasured sky shuts the roof rather than reading fine") {
    Live f;
    Site::WindowsController wc(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &wc };
    Cycle d(progs, 1, &f.bank, f.reg);

    f.bank.var(f.ref("cli_rain_mmph")).invalidate(Q_NO_DATA);
    f.bank.var(f.ref("cli_wind_mps")).invalidate(Q_NO_DATA);
    f.bank.var(f.ref("cli_coolDem")).set(1.0f);
    d.step(100);

    CHECK(f.bank.get(f.bank.resolve_in<bool>("cli_rainLockout")));
    CHECK(f.bank.get(f.bank.resolve_in<bool>("cli_windLockout")));
    CHECK(wc.health() == Plc::Health::DEGRADED);
}

TEST_CASE("windows: parking waits for the LAST ram to seat, not the first") {
    Live f;
    fine_weather(f);
    Site::WindowsController wc(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &wc };
    Cycle d(progs, 1, &f.bank, f.reg);

    f.bank.var(f.ref("cli_coolDem")).set(1.0f);
    uint64_t t = 0;
    for (uint32_t i = 0; i < 800; i++) d.step(t += 100);
    REQUIRE(f.bank.get(f.bank.resolve_in<float>("cli_window4_pos")) > 0.9f);

    wc.request_safe(true);
    // One at a time, so the first ram reaches the safe opening long before the
    // fourth - which is PART OPEN, not shut.
    for (uint32_t i = 0; i < 200; i++) d.step(t += 100);
    REQUIRE(f.bank.get(f.bank.resolve_in<float>("cli_window1_pos")) ==
            doctest::Approx(Site::WindowsController::SAFE_OPEN).epsilon(0.15));
    CHECK_FALSE(wc.is_parked());

    for (uint32_t i = 0; i < 800; i++) d.step(t += 100);
    CHECK(wc.is_parked());
}

TEST_CASE("intractors: the fusion is a maximum of the demands whose gate is open") {
    Live f;
    Site::IntractorController ic(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &ic };
    Cycle d(progs, 1, &f.bank, f.reg);

    Vars::RefIn<float> intr = f.bank.resolve_in<float>("cli_intr1");
    const float start = f.bank.var(f.ref("cli_intakeStartDem")).as_f32();

    INFO("under the staging threshold they are at their FLOOR, not stopped: "
         "the row declares the safe state and some air is always wanted");
    f.bank.var(f.ref("cli_intakeCoolOk")).set(true);
    f.bank.var(f.ref("cli_coolDem")).set(start - 0.1f);
    d.step(100);
    CHECK(f.bank.get(intr) == doctest::Approx(0.15f));

    INFO("the rot guard alone gets them going: each input is a floor");
    f.bank.var(f.ref("cli_rotVentDem")).set(1.0f);
    d.step(200);
    CHECK(f.bank.get(intr) == doctest::Approx(1.0f));

    INFO("asking to HUMIDIFY is not this device's business, and does not lower it");
    f.bank.var(f.ref("cli_humidDem")).set(1.0f);
    d.step(300);
    CHECK(f.bank.get(intr) == doctest::Approx(1.0f));

    INFO("...but asking to DEHUMIDIFY is, once the air outside is drier: the "
         "gate is the humidity law's verdict, not this one's");
    f.bank.var(f.ref("cli_rotVentDem")).set(0.0f);
    f.bank.var(f.ref("cli_humidDem")).set(-1.0f);
    d.step(400);
    CHECK(f.bank.get(intr) == doctest::Approx(0.15f));
    f.bank.var(f.ref("cli_intakeDryOk")).set(true);
    d.step(500);
    CHECK(f.bank.get(intr) == doctest::Approx(1.0f));
    CHECK(f.bank.get(f.bank.resolve_in<float>("cli_intr2")) == doctest::Approx(1.0f));
}

TEST_CASE("intractors: cooling only draws air that is worth drawing") {
    Live f;
    Site::IntractorController ic(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &ic };
    Cycle d(progs, 1, &f.bank, f.reg);

    Vars::RefIn<float> intr = f.bank.resolve_in<float>("cli_intr1");

    INFO("a full cooling demand with the intake no cooler than the house does "
         "nothing: the fans would only bring the outside in");
    f.bank.var(f.ref("cli_coolDem")).set(1.0f);
    d.step(100);
    CHECK(f.bank.get(intr) == doctest::Approx(0.15f));

    f.bank.var(f.ref("cli_intakeCoolOk")).set(true);
    d.step(200);
    CHECK(f.bank.get(intr) == doctest::Approx(1.0f));

    INFO("and the NEGATIVE half is a demand of its own: cold inside, warm "
         "ground, and the same fans pull the stored heat up");
    f.bank.var(f.ref("cli_intakeCoolOk")).set(false);
    f.bank.var(f.ref("cli_coolDem")).set(-1.0f);
    d.step(300);
    CHECK(f.bank.get(intr) == doctest::Approx(0.15f));
    f.bank.var(f.ref("cli_groundHeatOk")).set(true);
    d.step(400);
    CHECK(f.bank.get(intr) == doctest::Approx(1.0f));
}

TEST_CASE("fans: one speed relay at a time, and a band change costs hysteresis") {
    Live f;
    Site::FanController fc(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &fc };
    Cycle d(progs, 1, &f.bank, f.reg);

    Vars::RefIn<bool> s1 = f.bank.resolve_in<bool>("cli_destrat1_speed1");
    Vars::RefIn<bool> s2 = f.bank.resolve_in<bool>("cli_destrat1_speed2");
    Vars::RefIn<bool> s3 = f.bank.resolve_in<bool>("cli_destrat1_speed3");
    const float med = f.bank.var(f.ref("cli_fanMedDem")).as_f32();

    f.bank.var(f.ref("cli_stratDem")).set(med + 0.01f);
    d.step(100);
    CHECK_FALSE(f.bank.get(s1));
    CHECK(f.bank.get(s2));
    CHECK_FALSE(f.bank.get(s3));

    INFO("just under the threshold again is not a band change");
    f.bank.var(f.ref("cli_stratDem")).set(med - 0.01f);
    d.step(200);
    CHECK(f.bank.get(s2));

    INFO("...it takes the whole hysteresis to give the band back");
    f.bank.var(f.ref("cli_stratDem")).set(med - 0.2f);
    d.step(300);
    CHECK(f.bank.get(s1));
    CHECK_FALSE(f.bank.get(s2));
}

TEST_CASE("fans: a travelling screen cuts them, and a safe request leaves speed 1") {
    Live f;
    Site::FanController fc(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &fc };
    Cycle d(progs, 1, &f.bank, f.reg);

    Vars::RefIn<bool> s1 = f.bank.resolve_in<bool>("cli_destrat1_speed1");
    Vars::RefIn<bool> s3 = f.bank.resolve_in<bool>("cli_destrat2_speed3");

    f.bank.var(f.ref("cli_stratDem")).set(1.0f);
    d.step(100);
    REQUIRE(f.bank.get(s3));

    f.bank.var(f.ref("cli_screenMoving")).set(true);
    d.step(200);
    CHECK_FALSE(f.bank.get(s1));
    CHECK_FALSE(f.bank.get(s3));

    INFO("the interlock outranks the safe state too: the fabric is still moving");
    fc.request_safe(true);
    d.step(300);
    CHECK_FALSE(f.bank.get(s1));

    INFO("and once it has stopped, safe is RUNNING at speed 1");
    f.bank.var(f.ref("cli_screenMoving")).set(false);
    d.step(400);
    CHECK(f.bank.get(s1));
    CHECK_FALSE(f.bank.get(s3));
    CHECK(fc.is_parked());
}

TEST_CASE("fans: a watering cycle is a floor under them") {
    Live f;
    Site::FanController fc(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &fc };
    Cycle d(progs, 1, &f.bank, f.reg);

    // Both demands present and asking for nothing: an ABSENT one is a floor of
    // its own, which is the case above this one.
    f.bank.var(f.ref("cli_stratDem")).set(0.0f);
    f.bank.var(f.ref("cli_coolDem")).set(0.0f);
    d.step(100);
    CHECK_FALSE(f.bank.get(f.bank.resolve_in<bool>("cli_destrat1_speed1")));

    f.bank.var(f.ref("irr_state")).set_i((int64_t)Site::IRR_FILLING);
    d.step(200);
    CHECK(f.bank.get(f.bank.resolve_in<bool>("cli_destrat1_speed1")));
}

TEST_CASE("skirt: the humidify arm is proportional, the cooling arm is staged") {
    Live f;
    Site::SkirtController sk(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &sk };
    Cycle d(progs, 1, &f.bank, f.reg);

    Vars::RefIn<float> duty = f.bank.resolve_in<float>("cli_skirtDuty");
    const uint32_t period = f.bank.var(f.ref("cli_skirtPeriod_sec")).value().as_u32();
    const uint32_t min_on = f.bank.var(f.ref("cli_skirtMinOn_sec")).value().as_u32();

    // Dry enough for a wet pad to cool, so the cooling arm is not gated out.
    f.bank.var(f.ref("cli_dpMargin_degC")).publish(from_f32(T_F32, 20.0f), 0);

    f.bank.var(f.ref("cli_humidDem")).set(0.5f);
    d.step(100);
    CHECK(f.bank.get(duty) == doctest::Approx(0.5f).epsilon(0.02));

    INFO("a demand whose pulse would be under the minimum does not open at all");
    f.bank.var(f.ref("cli_humidDem")).set(
        0.5f * (float)min_on / (float)period);
    d.step(200);
    CHECK(f.bank.get(duty) == doctest::Approx(0.0f));

    INFO("asking to DEHUMIDIFY never opens it: that arm belongs to the intractors");
    f.bank.var(f.ref("cli_humidDem")).set(-1.0f);
    d.step(300);
    CHECK(f.bank.get(duty) == doctest::Approx(0.0f));

    INFO("and the cooling arm starts only above its own staging threshold");
    const float start = f.bank.var(f.ref("cli_skirtCoolDem")).as_f32();
    f.bank.var(f.ref("cli_coolDem")).set(start - 0.05f);
    d.step(400);
    CHECK(f.bank.get(duty) == doctest::Approx(0.0f));
    f.bank.var(f.ref("cli_coolDem")).set(1.0f);
    d.step(500);
    CHECK(f.bank.get(duty) == doctest::Approx(1.0f));
}

TEST_CASE("skirt: damp air gates the cooling arm, and the rot lockout cuts both") {
    Live f;
    Site::SkirtController sk(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &sk };
    Cycle d(progs, 1, &f.bank, f.reg);

    Vars::RefIn<bool> valve = f.bank.resolve_in<bool>("cli_skirtValve");
    const float dry_min = f.bank.var(f.ref("cli_skirtDryMin_degC")).as_f32();

    INFO("a wet pad in damp air adds water without cooling anything");
    f.bank.var(f.ref("cli_dpMargin_degC")).publish(from_f32(T_F32, dry_min - 1.0f), 0);
    f.bank.var(f.ref("cli_coolDem")).set(1.0f);
    d.step(100);
    CHECK_FALSE(f.bank.get(valve));

    f.bank.var(f.ref("cli_dpMargin_degC")).publish(from_f32(T_F32, dry_min + 1.0f), 100);
    d.step(200);
    CHECK(f.bank.get(valve));

    INFO("and the rot lockout cuts it whatever either arm is asking for");
    f.bank.var(f.ref("cli_rotLockout")).set(true);
    d.step(300);
    CHECK_FALSE(f.bank.get(valve));
    CHECK(f.bank.get(f.bank.resolve_in<float>("cli_skirtDuty")) == doctest::Approx(0.0f));
}

TEST_CASE("valves: each parks where its own row says it fails, not all one way") {
    Live f;
    Site::IrrigationController irr(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &irr };
    Cycle d(progs, 1, &f.bank, f.reg);

    irr.request_safe(true);
    d.step(100);

    INFO("the inlet fails SHUT and the drains fail OPEN, so a filling tray empties");
    CHECK_FALSE(f.bank.get(f.bank.resolve_in<bool>("irr_input_ro1")));
    CHECK(f.bank.get(f.bank.resolve_in<bool>("irr_output_ro1")));
    CHECK(f.bank.get(f.bank.resolve_in<bool>("irr_tray1")));
    CHECK(irr.is_parked());
}

TEST_CASE("irrigation: a fill fault names the tray, not just the cycle") {
    Live f;
    Site::IrrigationController irr(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &irr };
    Cycle d(progs, 1, &f.bank, f.reg);

    // Tray 2 alone is due, and nothing will ever arrive to fill it.
    for (uint8_t i = 1; i <= 8; i++) {
        char name[28];
        snprintf(name, sizeof name, "irr_forceOff_tray%u", (unsigned)i);
        REQUIRE(f.bank.param_set(f.bank.var(f.ref(name)), i != 2) == SET_OK);
    }
    REQUIRE(f.bank.param_set(f.bank.var(f.ref("irr_forceOn_tray2")), true) == SET_OK);
    REQUIRE(f.bank.param_set(f.bank.var(f.ref("irr_maxFill_sec")), 100.0f) == SET_OK);
    field(f, "cli_temp_ext", 30.0f);

    uint64_t t = 0;
    d.step(t += 1000);
    REQUIRE(f.bank.var(f.ref("irr_state")).i() == Site::IRR_FILLING);
    REQUIRE(f.bank.var(f.ref("irr_activeTray")).i() == 2);

    for (uint32_t i = 0; i < 102; i++) d.step(t += 1000);

    INFO("the any-form is what an operator sees; the per-tray one is what still "
         "says which tray once the cycle has moved on");
    CHECK(f.bank.var(f.ref("alarm_irr_fill")).b());
    CHECK(f.bank.var(f.ref("alarm_irr_fill_tray2")).b());
    CHECK_FALSE(f.bank.var(f.ref("alarm_irr_fill_tray1")).b());
    CHECK_FALSE(f.bank.var(f.ref("alarm_irr_fill_tray3")).b());
}

TEST_CASE("irrigation: total water tracks the modulation whatever the split") {
    // The spec's load-bearing property: volume x mod^split over period /
    // mod^(1-split) is proportional to mod for ANY split. The split changes how
    // the water arrives, never how much.
    const float summer_vol = 10.0f, summer_days = 4.0f;
    for (float mod = 0.25f; mod <= 1.0f; mod += 0.25f) {
        float first = 0.0f;
        for (float split = 0.0f; split <= 1.0f; split += 0.25f) {
            const float vol    = summer_vol * std::pow(mod, split);
            const float period = summer_days / std::pow(mod, 1.0f - split);
            const float per_day = vol / period;
            if (split == 0.0f) first = per_day;
            INFO("mod " << mod << " split " << split);
            CHECK(per_day == doctest::Approx(first).epsilon(0.001));
            CHECK(per_day == doctest::Approx(mod * summer_vol / summer_days)
                                 .epsilon(0.001));
        }
    }
}

TEST_CASE("irrigation: the cycle fills to its dose, then drains, then idles") {
    Live f;
    Site::IrrigationController irr(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &irr };
    Cycle d(progs, 1, &f.bank, f.reg);

    REQUIRE(HAL::Pulse::attach(Site::pulse_inputs[0].channel,
                               Site::pulse_inputs[0].edge));

    // Warm enough that the dawn gate is open, and tray 1 forced so the cycle
    // starts without the calendar the due-check still wants.
    f.bank.var(f.ref("cli_temp_ext")).publish(from_f32(T_F32, 20.0f), 0);
    f.bank.var(f.ref("cli_vpd_kPa")).publish(from_f32(T_F32, 1.5f), 0);
    f.bank.param_set(f.bank.var(f.ref("irr_forceOn_tray1")), true);
    f.bank.param_set(f.bank.var(f.ref("irr_liters_tray1")), 5.0f);
    f.bank.param_set(f.bank.var(f.ref("irr_input_qtyPerImp")), 1.0f);

    uint64_t t = 0;
    d.step(t += 1000);
    CHECK(f.bank.var(f.ref("irr_state")).i() == Site::IRR_FILLING);
    CHECK(f.bank.var(f.ref("irr_activeTray")).i() == 1);
    INFO("filling: inlet and this tray open, outlet shut, no other tray");
    CHECK(f.bank.var(f.ref("irr_input_ro1")).b());
    CHECK(f.bank.var(f.ref("irr_tray1")).b());
    CHECK_FALSE(f.bank.var(f.ref("irr_output_ro1")).b());
    CHECK_FALSE(f.bank.var(f.ref("irr_tray2")).b());

    // The meter delivers the dose. It is modulated, so ask for plenty.
    HAL::Pulse::inject(Site::pulse_inputs[0].channel, 20);
    d.step(t += 1000);
    CHECK(f.bank.var(f.ref("irr_state")).i() == Site::IRR_DRAINING);
    INFO("draining: inlet shut, tray still open, outlet open");
    CHECK_FALSE(f.bank.var(f.ref("irr_input_ro1")).b());
    CHECK(f.bank.var(f.ref("irr_tray1")).b());
    CHECK(f.bank.var(f.ref("irr_output_ro1")).b());
    CHECK_FALSE(f.bank.var(f.ref("alarm_irr_fill")).b());

    INFO("the day is stamped at the END OF THE FILL, so a fault during the long "
         "drain cannot cause a second watering");
    CHECK(f.bank.var(f.ref("irr_servedToday")).i() == 1);
    const uint32_t stamped = (uint32_t)f.bank.var(f.ref("irr_last_tray1")).i();
    CHECK(stamped > 0);
    // The bank under test has no store bound, so the write stays marked and a
    // later save retries it. That mark is the whole point: drive() would have
    // written the value and left NOTHING owed to flash, so the schedule would
    // restart from nothing at the next boot.
    INFO("it went through the parameter path, so flash is owed the new value");
    CHECK(f.bank.var(f.ref("irr_last_tray1")).param_unsaved());

    const uint32_t window = (uint32_t)f.bank.var(f.ref("irr_drainWindow_sec")).i();
    for (uint32_t i = 0; i <= window + 2; i++) d.step(t += 1000);
    CHECK(f.bank.var(f.ref("irr_activeTray")).i() == 0);
    CHECK(f.bank.var(f.ref("irr_last_tray1")).i() == stamped);
}

namespace {

/** @brief Fill tray 1, then hold @p lpm on the outlet meter for the whole drain
 *  window. @return the litres the meter says came back. */
float drain_at(Live& f, Cycle& d, Site::IrrigationController& irr, float lpm,
               bool meter_live, Plc::Health* worst = nullptr) {
    f.bank.var(f.ref("cli_temp_ext")).publish(from_f32(T_F32, 20.0f), 0);
    f.bank.param_set(f.bank.var(f.ref("irr_forceOn_tray1")), true);
    f.bank.param_set(f.bank.var(f.ref("irr_liters_tray1")), 5.0f);
    f.bank.param_set(f.bank.var(f.ref("irr_input_qtyPerImp")), 1.0f);

    uint64_t t = 0;
    d.step(t += 1000);
    HAL::Pulse::inject(Site::pulse_inputs[0].channel, 20);
    d.step(t += 1000);
    REQUIRE(f.bank.var(f.ref("irr_state")).i() == Site::IRR_DRAINING);

    const uint32_t window = (uint32_t)f.bank.var(f.ref("irr_drainWindow_sec")).i();
    for (uint32_t i = 0; i <= window + 2; i++) {
        // Every second: the row's timeout is 5 s, and a meter that went stale
        // half way through would be testing the invalid path instead.
        if (meter_live)
            f.bank.var(f.ref("irr_out_Lpm")).publish(from_f32(T_F32, lpm), t);
        d.step(t += 1000);
        // Health is per ACTIVATION, so it has to be read inside the window: by
        // the time the loop ends the cycle is flushing and healthy again.
        if (worst != nullptr && irr.health() > *worst) *worst = irr.health();
    }
    return f.bank.var(f.ref("irr_drained_L")).as_f32();
}

}  // namespace

TEST_CASE("irrigation: the two meters read as a counter and a rate") {
    Live f;
    Site::IrrigationController irr(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &irr };
    Cycle d(progs, 1, &f.bank, f.reg);
    HAL::Pulse::attach(Site::pulse_inputs[0].channel, Site::pulse_inputs[0].edge);
    f.bank.param_set(f.bank.var(f.ref("irr_input_qtyPerImp")), 1.0f);

    uint64_t t = 0;
    d.step(t += 1000);
    // 20 pulses a second at a litre each, for ten seconds.
    for (uint32_t i = 0; i < 10; i++) {
        HAL::Pulse::inject(Site::pulse_inputs[0].channel, 20);
        f.bank.var(f.ref("irr_out_Lpm")).publish(from_f32(T_F32, 30.0f), t);
        d.step(t += 1000);
    }
    INFO("the inlet counter is the measurement, and its rate is derived");
    CHECK(f.bank.var(f.ref("irr_inTotal_L")).as_f32() == doctest::Approx(200.0f));
    CHECK(f.bank.var(f.ref("irr_inFlow_Lpm")).as_f32() == doctest::Approx(1200.0f).epsilon(0.2));

    INFO("the outlet rate is the measurement, and its counter is derived");
    CHECK(f.bank.var(f.ref("irr_outFlow_Lpm")).as_f32() == doctest::Approx(30.0f).epsilon(0.1));
    const float out = f.bank.var(f.ref("irr_outTotal_L")).as_f32();
    CHECK(out > 3.0f);
    CHECK(out < 6.0f);
}

TEST_CASE("irrigation: what comes back out is judged against what went in") {
    Live f;
    Site::IrrigationController irr(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &irr };
    Cycle d(progs, 1, &f.bank, f.reg);
    // Bare, not REQUIREd: a channel another case already attached answers
    // false, and this one only needs it counting.
    HAL::Pulse::attach(Site::pulse_inputs[0].channel, Site::pulse_inputs[0].edge);

    // 20 L went in; 5 L/min over the 300 s window is far more than the 30% the
    // margin allows to be missing.
    const float back = drain_at(f, d, irr, 5.0f, true);
    CHECK(back > 20.0f);
    CHECK_FALSE(f.bank.var(f.ref("alarm_water_clog")).b());
    CHECK_FALSE(f.bank.var(f.ref("alarm_irr_drain_tray1")).b());
}

TEST_CASE("irrigation: a tray that does not drain names itself") {
    Live f;
    Site::IrrigationController irr(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &irr };
    Cycle d(progs, 1, &f.bank, f.reg);
    HAL::Pulse::attach(Site::pulse_inputs[0].channel, Site::pulse_inputs[0].edge);

    // A trickle: half a litre back out of twenty.
    const float back = drain_at(f, d, irr, 0.1f, true);
    CHECK(back < 1.0f);
    CHECK(f.bank.var(f.ref("alarm_water_clog")).b());
    CHECK(f.bank.var(f.ref("alarm_irr_drain_tray1")).b());
    INFO("the OTHER trays are not accused");
    CHECK_FALSE(f.bank.var(f.ref("alarm_irr_drain_tray2")).b());
}

TEST_CASE("irrigation: a silent outlet meter gives no verdict, not a clog") {
    Live f;
    Site::IrrigationController irr(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &irr };
    Cycle d(progs, 1, &f.bank, f.reg);
    HAL::Pulse::attach(Site::pulse_inputs[0].channel, Site::pulse_inputs[0].edge);

    Plc::Health worst = Plc::Health::HEALTHY;
    const float back = drain_at(f, d, irr, 0.0f, false, &worst);
    CHECK(back == 0.0f);
    INFO("nothing was measured, so nothing is blamed - the meter has an alarm "
         "of its own");
    CHECK_FALSE(f.bank.var(f.ref("alarm_water_clog")).b());
    CHECK_FALSE(f.bank.var(f.ref("alarm_irr_drain_tray1")).b());
    INFO("but it does not pass in silence");
    CHECK(worst == Plc::Health::DEGRADED);
}

TEST_CASE("irrigation: a fill that never arrives raises, and still opens the drain") {
    Live f;
    Site::IrrigationController irr(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &irr };
    Cycle d(progs, 1, &f.bank, f.reg);
    HAL::Pulse::attach(Site::pulse_inputs[0].channel, Site::pulse_inputs[0].edge);

    f.bank.var(f.ref("cli_temp_ext")).publish(from_f32(T_F32, 20.0f), 0);
    f.bank.param_set(f.bank.var(f.ref("irr_forceOn_tray1")), true);
    f.bank.param_set(f.bank.var(f.ref("irr_liters_tray1")), 5.0f);
    // The shortest the band allows; a rejected edit would leave the default and
    // the case would just be waiting out five minutes it never reaches.
    REQUIRE(f.bank.param_set(f.bank.var(f.ref("irr_maxFill_sec")), 100.0f) == SET_OK);

    uint64_t t = 0;
    d.step(t += 1000);
    REQUIRE(f.bank.var(f.ref("irr_state")).i() == Site::IRR_FILLING);

    for (uint32_t i = 0; i < 102; i++) d.step(t += 1000);   // no pulses at all

    INFO("leaving the inlet open on a fault is how a tray overflows");
    CHECK(f.bank.var(f.ref("alarm_irr_fill")).b());
    CHECK(f.bank.var(f.ref("irr_state")).i() == Site::IRR_DRAINING);
    CHECK_FALSE(f.bank.var(f.ref("irr_input_ro1")).b());
}

}  // namespace site_programs
