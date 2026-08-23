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
#include "sites/gh_mlb41/generated/programs.gen.h"

#include "scan_fixture.h"

namespace site_programs {

using namespace Botanik;
using namespace Botanik::Vars;

using TestScan::Cycle;
using TestScan::Live;

namespace {

/** @brief Summer, as the modulation ladder would report it: full watering. The
 *  ladder is its own program now, so a cycle case states the factor rather than
 *  driving the weather that would produce it. */
void summer(Live& f) {
    f.bank.var(f.ref("irr_mod")).set(1.0f);
}

/** @brief Take the soak out of the cycle, which `irr_soak_sec` 0 is for.
 *  A case about the fill or the drain states its own configuration; the soak
 *  has a case of its own and the scenarios run at the site's real figure. */
void no_soak(Live& f) {
    f.bank.param_set(f.bank.var(f.ref("irr_soak_sec")), "0");
}

/** @brief Put the plant in the end-of-night watering slot. A cycle may only
 *  START in one of the two the spec allows, so every irrigation case has to
 *  say which one it is in. */
void slot(Live& f) {
    f.bank.var(f.ref("cli_phase")).set_i((int64_t)Site::PHASE_DAWN_BREEZE);
}

    /** @brief Where the roof parks, from the row that declares it. */
    float safe_open(Live& f) {
        const Vars::Descriptor& d = f.bank.descriptor(f.ref("cli_window1_tgt").index);
        return Vars::as_f32(d.type, d.def);
    }

    /** @brief Publish one field-read row as if the bus had just answered it. */
    void field(Live& f, const char* row, float v, uint64_t at = 0) {
        f.bank.var(f.ref(row)).publish(from_f32(T_F32, v), at);
    }

    /** @brief The house at two levels, and the row derived from them.
     *
     *  The cooling ladder controls on `cli_temp_in` and reports its verdict on
     *  the levels behind it, so a case stating one without the other is
     *  stating half a house. WORST CASE, as temp_rh_zone makes it. */
    void interior(Live& f, float hi, float lo) {
        field(f, "cli_temp_hi", hi);
        field(f, "cli_temp_lo", lo);
        field(f, "cli_temp_in", hi > lo ? hi : lo);
    }

    /** @brief A weather station reporting fine weather, so a case that is not
     *  about the interlocks is not shut in by one. */
    void fine_weather(Live& f) {
        field(f, "cli_rain_mmph", 0.0f);
        field(f, "cli_wind_mps", 1.0f);
        field(f, "cli_windGust_mps", 2.0f);
    }
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

/** @brief The irrigation chain: the meters feed the schedule, the schedule
 *  arms the cycle. Nothing here works with fewer than all three. */
struct IrrigationRig {
    Live                     live;
    Site::WaterMeters        meters{Botanik::SetupContext(live.reg), live.bank};
    Site::IrrigationSchedule sched {Botanik::SetupContext(live.reg), live.bank};
    Site::IrrigationCycle    irr   {Botanik::SetupContext(live.reg), live.bank};
    Plc::Program* const      progs[3] = { &meters, &sched, &irr };
    Cycle                    d{progs, 3, &live.bank, live.reg};
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

TEST_CASE_FIXTURE(Rig<Site::TempRHZone>, "zone: the selector keeps the middle reading and discards the extremes") {
    Vars::RefIn<float> hi = live.bank.resolve_in<float>("cli_temp_hi");
    const VarRef p[3] = { live.ref("cli_temp_hi1"), live.ref("cli_temp_hi2"),
                          live.ref("cli_temp_hi3") };


    for (uint8_t i = 0; i < 3; i++) {
        live.bank.var(p[i]).publish(from_f32(T_F32, (i == 2) ? 40.0f : 20.0f + 0.2f * i), 0);
        // PERCENT, which is what the instrument sends and what the row's own
        // band (0..100) declares.
        live.bank.var(live.ref(i == 0 ? "cli_rh_hi1" : i == 1 ? "cli_rh_hi2" : "cli_rh_hi3"))
              .publish(from_f32(T_F32, 50.0f), 0);
    }
    for (uint64_t t = 0; t <= 60000; t += 100) d.step(t);

    INFO("40 C is stuck high and 20.0 is the low extreme: the middle reading "
         "is what is retained, so neither extreme can ever be the answer");
    CHECK(live.bank.get(hi) == doctest::Approx(20.2f).epsilon(0.02));
}

TEST_CASE_FIXTURE(Rig<Site::TempRHZone>, "zone: an invalid probe is excluded, and one good probe still reads") {
    Vars::RefIn<float> hi = live.bank.resolve_in<float>("cli_temp_hi");
    const VarRef p[3] = { live.ref("cli_temp_hi1"), live.ref("cli_temp_hi2"),
                          live.ref("cli_temp_hi3") };


    // One probe answering 25, the other two dead. The dead pair must not vote:
    // they read their declared default, which would drag any average.
    live.bank.var(p[0]).publish(from_f32(T_F32, 25.0f), 0);
    live.bank.var(p[1]).invalidate(Q_NO_DATA);
    live.bank.var(p[2]).invalidate(Q_NO_DATA);
    live.bank.var(live.ref("cli_rh_hi1")).publish(from_f32(T_F32, 50.0f), 0);
    for (uint64_t t = 0; t <= 60000; t += 100) d.step(t);

    CHECK(live.bank.var(hi.r).valid());
    CHECK(live.bank.get(hi) == doctest::Approx(25.0f).epsilon(0.02));
    INFO("and the quorum alarm is up, because two probes are gone");
    CHECK(live.bank.var(live.ref("alarm_sns_ceiling")).b());
}

TEST_CASE_FIXTURE(Rig<Site::TempRHZone>, "zone: the instruments report percent, the model works in fractions") {

    for (uint8_t i = 1; i <= 3; i++) {
        char t[24], h[24];
        snprintf(t, sizeof t, "cli_temp_hi%u", (unsigned)i);
        snprintf(h, sizeof h, "cli_rh_hi%u", (unsigned)i);
        live.bank.var(live.ref(t)).publish(from_f32(T_F32, 22.0f), 0);
        live.bank.var(live.ref(h)).publish(from_f32(T_F32, 68.0f), 0);
    }
    for (uint64_t t = 0; t <= 60000; t += 100) d.step(t);

    INFO("68% in, 0.68 out - published as a percent it clamped at 1.00 and "
         "every dew-point margin in the plant read zero");
    CHECK(live.bank.get(live.bank.resolve_in<float>("cli_rh_hi")) ==
          doctest::Approx(0.68f).epsilon(0.02));

    INFO("and the psychrometrics that follow are sane: 22 C at 68% dews at "
         "about 16 C");
    CHECK(live.bank.get(live.bank.resolve_in<float>("cli_dewPoint_degC")) ==
          doctest::Approx(15.8f).epsilon(0.05));
    CHECK(live.bank.get(live.bank.resolve_in<float>("cli_dpMargin_degC")) ==
          doctest::Approx(6.2f).epsilon(0.1));
}

TEST_CASE_FIXTURE(Rig<Site::TempRHZone>, "zone: temperature survives losing every humidity probe") {
    // The two quantities carry their own quorum. Coupling them would let one
    // dead RH probe invalidate a temperature three good probes agree on.
    Vars::RefIn<float> t_out = live.bank.resolve_in<float>("cli_temp_hi");
    Vars::RefIn<float> h_out = live.bank.resolve_in<float>("cli_rh_hi");


    const char* temps[3] = { "cli_temp_hi1", "cli_temp_hi2", "cli_temp_hi3" };
    const char* rhs[3]   = { "cli_rh_hi1", "cli_rh_hi2", "cli_rh_hi3" };
    for (uint8_t i = 0; i < 3; i++) {
        live.bank.var(live.ref(temps[i])).publish(from_f32(T_F32, 22.0f), 0);
        live.bank.var(live.ref(rhs[i])).invalidate(Q_NO_DATA);
    }
    for (uint64_t t = 0; t <= 60000; t += 100) d.step(t);

    CHECK(live.bank.var(t_out.r).valid());
    CHECK(live.bank.get(t_out) == doctest::Approx(22.0f).epsilon(0.02));
    CHECK_FALSE(live.bank.var(h_out.r).valid());
}

TEST_CASE_FIXTURE(Rig<Site::TempRHZone>, "zone: with every probe gone the reading is HELD, not defaulted") {
    Vars::RefIn<float> hi = live.bank.resolve_in<float>("cli_temp_hi");
    const VarRef p[3] = { live.ref("cli_temp_hi1"), live.ref("cli_temp_hi2"),
                          live.ref("cli_temp_hi3") };


    for (uint8_t i = 0; i < 3; i++)
        live.bank.var(p[i]).publish(from_f32(T_F32, 21.0f), 0);
    for (uint64_t t = 0; t <= 20000; t += 100) d.step(t);
    REQUIRE(live.bank.var(hi.r).valid());

    for (uint8_t i = 0; i < 3; i++) live.bank.var(p[i]).invalidate(Q_NO_DATA);
    for (uint64_t t = 20000; t <= 40000; t += 100) d.step(t);

    INFO("a law gating on quality must see that nothing is measuring this zone");
    CHECK_FALSE(live.bank.var(hi.r).valid());
    CHECK(live.bank.var(hi.r).value().q == Q_HELD);

    // ...and the VALUE is the last average this zone could make. A reader that
    // does not test quality gets the temperature the greenhouse last had rather
    // than the declared default, which is 0 degC and reads like a measurement
    // of a freezing house.
    INFO("and it holds the last average rather than dropping to its default");
    CHECK(live.bank.get(hi) == doctest::Approx(21.0f));
}

TEST_CASE_FIXTURE(Rig<Site::TempRHZone>, "zone: a zone that has never been measured has nothing to hold") {
    Vars::RefIn<float> cs = live.bank.resolve_in<float>("cli_temp_cs");

    for (uint64_t t = 0; t <= 2000; t += 100) d.step(t);

    INFO("holding would publish a filter that has never been seeded, which is "
         "the declared default wearing a better label");
    CHECK_FALSE(live.bank.var(cs.r).valid());
    CHECK(live.bank.var(cs.r).value().q == Q_NO_DATA);
}

TEST_CASE_FIXTURE(Rig<Site::StratSensor>, "strat: the gap is unsigned, so an inverted profile still calls the fans") {

    Vars::RefIn<float> gap = live.bank.resolve_in<float>("cli_dTStrat_degC");
    const VarRef hi = live.ref("cli_temp_hi"), lo = live.ref("cli_temp_lo");

    live.bank.var(hi).publish(from_f32(T_F32, 24.0f), 0);
    live.bank.var(lo).publish(from_f32(T_F32, 20.0f), 0);
    d.step(100);
    CHECK(live.bank.get(gap) == doctest::Approx(4.0f));

    INFO("a cold roof over a warm floor is as much a case for mixing");
    live.bank.var(hi).publish(from_f32(T_F32, 18.0f), 200);
    live.bank.var(lo).publish(from_f32(T_F32, 22.0f), 200);
    d.step(200);
    CHECK(live.bank.get(gap) == doctest::Approx(4.0f));
}

TEST_CASE_FIXTURE(Rig<Site::StratSensor>, "strat: a level nothing measures makes the gap unmeasured, not zero") {

    live.bank.var(live.ref("cli_temp_hi")).publish(from_f32(T_F32, 24.0f), 0);
    live.bank.var(live.ref("cli_temp_lo")).invalidate(Q_NO_DATA);
    d.step(100);
    CHECK_FALSE(live.bank.var(live.ref("cli_dTStrat_degC")).valid());
}

TEST_CASE_FIXTURE(Rig<Site::DayPhaseController>, "day phase: the banks arm off the published extremes, so one can be forced") {
    // The extremes are gathered over hours, so the only way to rehearse a bank
    // phase is to force the row that decides it. That works because this law
    // arms off what it published rather than off the member behind it.

    const float trg = live.bank.var(live.ref("cli_coldBankTrg_degC")).as_f32();
    field(live, "cli_temp_in", trg - 5.0f);
    d.step(100);
    REQUIRE_FALSE(live.bank.var(live.ref("cli_coldBankArm")).b());

    INFO("a hot yesterday forced by hand arms the cold bank");
    char hot[16];
    snprintf(hot, sizeof hot, "%.1f", (double)(trg + 5.0f));
    REQUIRE(live.bank.override_set(live.bank.var(live.ref("cli_prevDayMax_degC")), hot)
            == Vars::SET_OK);
    d.step(200);
    CHECK(live.bank.var(live.ref("cli_coldBankArm")).b());

    INFO("...and releasing it gives the decision back to the house");
    live.bank.override_clear(live.bank.var(live.ref("cli_prevDayMax_degC")));
    d.step(300);
    CHECK_FALSE(live.bank.var(live.ref("cli_coldBankArm")).b());
}

TEST_CASE_FIXTURE(Rig<Site::TargetTempController>, "target: a bank phase only shifts the target when yesterday armed it") {

    Vars::RefIn<float> target = live.bank.resolve_in<float>("cli_targetTemp_degC");
    const VarRef phase = live.ref("cli_phase");
    const VarRef cold  = live.ref("cli_coldBankArm");
    const VarRef heat  = live.ref("cli_heatBankArm");
    const float base = live.bank.var(live.ref("cli_coolSet_degC")).as_f32();

    // The output filter off, so this case is about the ARMING and reads the
    // number the law computed. Its own case below covers the easing.
    live.bank.param_set(live.bank.var(live.ref("cli_targetTau_s")), 0.0f);

    live.bank.var(phase).set_i((int64_t)Site::PHASE_DAY);
    d.step(100);
    CHECK(live.bank.get(target) == doctest::Approx(base));

    INFO("COLD_BANK in force but not armed runs at the ordinary target");
    live.bank.var(phase).set_i((int64_t)Site::PHASE_COLD_BANK);
    live.bank.var(cold).set(false);
    d.step(200);
    CHECK(live.bank.get(target) == doctest::Approx(base));

    INFO("armed, it pulls the target down by the cold offset");
    live.bank.var(cold).set(true);
    d.step(300);
    const float ofs = live.bank.var(live.ref("cli_coldBankOfs_degC")).as_f32();
    CHECK(live.bank.get(target) == doctest::Approx(base - ofs));

    INFO("and HEAT_BANK uses the HEAT offset, not the cold one");
    live.bank.var(phase).set_i((int64_t)Site::PHASE_HEAT_BANK);
    live.bank.var(heat).set(true);
    d.step(400);
    const float hofs = live.bank.var(live.ref("cli_heatBankOfs_degC")).as_f32();
    REQUIRE(hofs != ofs);          // else the case proves nothing
    CHECK(live.bank.get(target) == doctest::Approx(base + hofs));
}

TEST_CASE_FIXTURE(Rig<Site::TargetTempController>, "target: a phase boundary eases the target instead of stepping it") {

    Vars::RefIn<float> target = live.bank.resolve_in<float>("cli_targetTemp_degC");
    const float base = live.bank.var(live.ref("cli_coolSet_degC")).as_f32();
    const float ofs  = live.bank.var(live.ref("cli_coldBankOfs_degC")).as_f32();

    live.bank.var(live.ref("cli_phase")).set_i((int64_t)Site::PHASE_DAY);
    d.step(100);
    INFO("the first scan ADOPTS the target: easing up from a stale filter would "
         "call for heat the house does not need");
    REQUIRE(live.bank.get(target) == doctest::Approx(base));

    live.bank.var(live.ref("cli_phase")).set_i((int64_t)Site::PHASE_COLD_BANK);
    live.bank.var(live.ref("cli_coldBankArm")).set(true);
    d.step(1100);                       // a second into the new phase
    const float moved = live.bank.get(target);
    CHECK(moved < base);                                  // it has begun
    CHECK(moved > base - ofs * 0.05f);                    // ...by a sliver of it

    INFO("and arrives, given several time constants of the default 300 s");
    for (uint32_t t = 2100; t <= 3000000u; t += 1000) d.step(t);
    CHECK(live.bank.get(target) == doctest::Approx(base - ofs).epsilon(0.001));
}

TEST_CASE_FIXTURE(Rig<Site::TargetTempController>, "target: an aggressive offset is clamped to the safe band") {

    live.bank.param_set(live.bank.var(live.ref("cli_coldBankOfs_degC")), 15.0f);
    live.bank.var(live.ref("cli_phase")).set_i((int64_t)Site::PHASE_COLD_BANK);
    live.bank.var(live.ref("cli_coldBankArm")).set(true);
    d.step(100);

    Vars::RefIn<float> target = live.bank.resolve_in<float>("cli_targetTemp_degC");
    Vars::RefIn<float> floor_ = live.bank.resolve_in<float>("cli_targetMin_degC");
    CHECK(live.bank.get(target) == doctest::Approx(live.bank.get(floor_)));
}

TEST_CASE_FIXTURE(Rig<Site::CoolingController>, "cooling: a demand it cannot make is HELD at the last one it could") {

    const VarRef dem = live.ref("cli_coolDem");
    field(live, "cli_targetTemp_degC", 20.0f);
    interior(live, 30.0f, 30.0f);
    field(live, "cli_temp_cs", 18.0f);
    for (uint32_t i = 0; i < 20; i++) d.step(100u * (i + 1));
    const float was = live.bank.var(dem).as_f32();
    REQUIRE(was > 0.0f);

    live.bank.var(live.ref("cli_temp_hi")).invalidate(Q_NO_DATA);
    live.bank.var(live.ref("cli_temp_lo")).invalidate(Q_NO_DATA);
    live.bank.var(live.ref("cli_temp_in")).invalidate(Q_NO_DATA);
    d.step(3000);

    INFO("every actuator reads the quality and goes to its own safe state, so "
         "what the number says matters only to a reader that does not - and to "
         "that reader, zero would mean nothing is wanted");
    CHECK_FALSE(live.bank.var(dem).valid());
    CHECK(live.bank.var(dem).value().q == Q_HELD);
    CHECK(live.bank.var(dem).as_f32() == doctest::Approx(was));
    CHECK(prog.health() == Plc::Health::FAULTED);
}

TEST_CASE_FIXTURE(Rig<Site::CoolingController>, "cooling: the floor carries it when the ceiling goes, and both gone is unknown") {

    const VarRef dem = live.ref("cli_coolDem");
    field(live, "cli_targetTemp_degC", 20.0f);
    interior(live, 30.0f, 30.0f);
    // The intake gate reads it; without it the law is DEGRADED for a reason
    // that has nothing to do with this case.
    field(live, "cli_temp_cs", 18.0f);
    d.step(100);
    REQUIRE(live.bank.var(dem).valid());
    REQUIRE(live.bank.var(dem).as_f32() > 0.0f);          // hot: cooling
    CHECK(prog.health() == Plc::Health::HEALTHY);

    INFO("one level gone: temp_rh_zone still has a house temperature to give, "
         "and this law says it is working on half a picture");
    live.bank.var(live.ref("cli_temp_hi")).invalidate(Q_NO_DATA);
    d.step(200);
    CHECK(live.bank.var(dem).valid());
    CHECK(live.bank.var(dem).as_f32() > 0.0f);
    CHECK(prog.health() == Plc::Health::DEGRADED);

    INFO("with the house's own row gone a PID on the defaulted reading would "
         "see 0 degC against the target and call for full HEAT");
    live.bank.var(live.ref("cli_temp_lo")).invalidate(Q_NO_DATA);
    live.bank.var(live.ref("cli_temp_in")).invalidate(Q_NO_DATA);
    d.step(300);
    CHECK_FALSE(live.bank.var(dem).valid());
    CHECK(prog.health() == Plc::Health::FAULTED);
}

TEST_CASE("cooling: an unknown demand puts every actuator at its own safe state") {
    Live f;
    fine_weather(f);
    Site::RoofLockout    lock_(Botanik::SetupContext(f.reg), f.bank);
    Site::WindowsController    win(Botanik::SetupContext(f.reg), f.bank);
    Site::IntractorController  intr(Botanik::SetupContext(f.reg), f.bank);
    Site::ScreenController     scr(Botanik::SetupContext(f.reg), f.bank);
    Site::SkirtController      sk(Botanik::SetupContext(f.reg), f.bank);
    Site::FanController        fan(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &lock_, &win, &intr, &scr, &sk, &fan };
    Cycle d(progs, 6, &f.bank, f.reg);

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
          doctest::Approx(safe_open(f)));                         // part open
    CHECK(f.bank.get(f.bank.resolve_in<float>("cli_intr1")) ==
          doctest::Approx(0.15f));                                // some air
    CHECK(f.bank.get(f.bank.resolve_in<bool>("cli_screen")));     // deployed
    CHECK_FALSE(f.bank.get(f.bank.resolve_in<bool>("cli_skirtValve")));  // shut
    CHECK(f.bank.get(f.bank.resolve_in<bool>("cli_destrat1_speed1")));   // speed 1

    for (Plc::Program* p : progs)
        CHECK(p->health() == Plc::Health::DEGRADED);
}

TEST_CASE_FIXTURE(Rig<Site::DestratController>, "destrat: an unmeasured gap makes the demand unknown, not zero") {

    const VarRef dem = live.ref("cli_stratDem");
    field(live, "cli_dTStrat_degC", 8.0f);
    d.step(100);
    REQUIRE(live.bank.var(dem).valid());
    INFO("the demand IS the fans' own top rung: any other number would need "
         "translating twice, and the two ladders would drift apart");
    REQUIRE(live.bank.var(dem).as_f32() ==
            doctest::Approx(live.bank.var(live.ref("cli_fanHighDem")).as_f32()));

    INFO("every rung engages AT its gap, not a hysteresis above it: the "
         "parameter an operator sets is where the speed starts");
    field(live, "cli_dTStrat_degC",
          live.bank.var(live.ref("cli_dTStratLow_degC")).as_f32());
    d.step(200);
    CHECK(live.bank.var(dem).as_f32() ==
          doctest::Approx(live.bank.var(live.ref("cli_fanLowDem")).as_f32()));

    INFO("and coming back down costs the hysteresis: the rungs are contiguous "
         "by default, since a speed's release edge is the one below it");
    field(live, "cli_dTStrat_degC", 1.0f);
    d.step(300);
    CHECK(live.bank.var(dem).as_f32() ==
          doctest::Approx(live.bank.var(live.ref("cli_fanLowDem")).as_f32()));
    field(live, "cli_dTStrat_degC", -0.1f);
    d.step(400);
    CHECK(live.bank.var(dem).as_f32() == doctest::Approx(0.0f));

    INFO("this row's default is 0, which reads as 'no stratification' and would "
         "stop the fans on a dead probe");
    live.bank.var(live.ref("cli_dTStrat_degC")).invalidate(Q_NO_DATA);
    d.step(200);
    CHECK_FALSE(live.bank.var(dem).valid());
    CHECK(prog.health() == Plc::Health::FAULTED);
}

TEST_CASE_FIXTURE(Rig<Site::FanController>, "fans: an unknown gap floors them at their safe state, not at rest") {

    Vars::RefIn<bool> s1 = live.bank.resolve_in<bool>("cli_destrat1_speed1");
    Vars::RefIn<bool> s3 = live.bank.resolve_in<bool>("cli_destrat1_speed3");

    live.bank.var(live.ref("cli_stratDem")).invalidate(Q_NO_DATA);
    live.bank.var(live.ref("cli_coolDem")).set(0.0f);
    d.step(100);

    INFO("still air is itself a rot risk, so an unknown gap is speed 1");
    CHECK(live.bank.get(s1));
    CHECK(prog.health() == Plc::Health::DEGRADED);

    INFO("...and it is a FLOOR: a cooling demand above it still takes them up");
    live.bank.var(live.ref("cli_coolDem")).set(1.0f);
    d.step(200);
    CHECK_FALSE(live.bank.get(s1));
    CHECK(live.bank.get(s3));

    INFO("the screen still outranks it: a mechanical interlock is resolved "
         "before any demand, and before the safe state the floor comes from");
    live.bank.var(live.ref("cli_coolDem")).set(0.0f);
    live.bank.var(live.ref("cli_screenMoving")).set(true);
    d.step(300);
    CHECK_FALSE(live.bank.get(s1));
    CHECK_FALSE(live.bank.get(s3));
}

TEST_CASE_FIXTURE(Rig<Site::HumidityController>, "humidity, rot guard: the ventilation demand rises as the margin falls") {

    Vars::RefIn<float> dem = live.bank.resolve_in<float>("cli_rotVentDem");
    const VarRef margin = live.ref("cli_dpMargin_degC");
    const float limit = live.bank.var(live.ref("cli_rotMargin_degC")).as_f32();

    live.bank.var(margin).publish(from_f32(T_F32, limit + 2.0f), 0);
    d.step(100);
    CHECK(live.bank.get(dem) == doctest::Approx(0.0f));

    INFO("at the limit itself the ramp has not started");
    live.bank.var(margin).publish(from_f32(T_F32, limit), 100);
    d.step(200);
    CHECK(live.bank.get(dem) == doctest::Approx(0.0f));

    INFO("and it is fully on at the dew point itself, not before");
    live.bank.var(margin).publish(from_f32(T_F32, 0.5f * limit), 200);
    d.step(300);
    const float half = live.bank.get(dem);
    CHECK(half > 0.0f);
    CHECK(half < 1.0f);
    live.bank.var(margin).publish(from_f32(T_F32, 0.0f), 300);
    d.step(400);
    CHECK(live.bank.get(dem) == doctest::Approx(1.0f));
}

TEST_CASE_FIXTURE(Rig<Site::HumidityController>, "humidity, rot guard: the lockout outlives the condition, and boots clear") {

    Vars::RefIn<bool> lock = live.bank.resolve_in<bool>("cli_rotLockout");
    const VarRef rot = live.ref("alarm_rot");
    live.bank.var(live.ref("cli_dpMargin_degC")).publish(from_f32(T_F32, 20.0f), 0);

    INFO("dry air at boot is not locked out: the guard has nothing against it");
    d.step(100);
    CHECK_FALSE(live.bank.get(lock));

    live.bank.var(rot).set(true);
    d.step(200);
    CHECK(live.bank.get(lock));

    INFO("the condition clearing does not release it");
    live.bank.var(rot).set(false);
    d.step(300);
    CHECK(live.bank.get(lock));

    uint64_t t = 300;
    for (uint32_t i = 0; i < 3000; i++) d.step(t += 100);
    CHECK_FALSE(live.bank.get(lock));
}

TEST_CASE_FIXTURE(Rig<Site::HumidityController>, "humidity, rot guard: an unmeasured margin ventilates rather than assuming dry air") {

    live.bank.var(live.ref("cli_dpMargin_degC")).invalidate(Q_NO_DATA);
    d.step(100);
    CHECK(live.bank.get(live.bank.resolve_in<float>("cli_rotVentDem")) == doctest::Approx(1.0f));
    CHECK(live.bank.get(live.bank.resolve_in<bool>("cli_rotLockout")));
    CHECK(prog.health() == Plc::Health::DEGRADED);
}

TEST_CASE_FIXTURE(Rig<Site::ScreenController>, "screen: a cold night deploys it, and the traverse cuts the fans") {

    Vars::RefIn<bool> screen = live.bank.resolve_in<bool>("cli_screen");
    Vars::RefIn<bool> moving = live.bank.resolve_in<bool>("cli_screenMoving");

    live.bank.var(live.ref("cli_phase")).set_i((int64_t)Site::PHASE_DAY);
    live.bank.var(live.ref("cli_temp_hi")).publish(from_f32(T_F32, 25.0f), 0);
    live.bank.var(live.ref("cli_coolDem")).set(0.5f);
    d.step(100);
    CHECK_FALSE(live.bank.get(screen));

    INFO("a cold night deploys it, and the fabric is in the fans' way while it "
         "goes. The COLD is what asks - a demand under cli_thermalScreenDem - "
         "and the night is only what permits it");
    live.bank.var(live.ref("cli_phase")).set_i((int64_t)Site::PHASE_NIGHT);
    live.bank.var(live.ref("cli_coolDem"))
          .set(live.bank.var(live.ref("cli_thermalScreenDem")).as_f32() - 0.1f);
    d.step(200);
    CHECK(live.bank.get(screen));
    CHECK(live.bank.get(moving));

    INFO("...and stops being in their way once the traverse has run");
    const uint32_t travel = live.bank.var(live.ref("cli_screenTravel_sec")).value().as_u32();
    uint64_t t = 200;
    for (uint32_t i = 0; i < 10 * travel + 5; i++) d.step(t += 100);
    CHECK(live.bank.get(screen));
    CHECK_FALSE(live.bank.get(moving));
}

TEST_CASE("windows: a forced lockout shuts the roof, with no weather to do it") {
    // The point of publishing a verdict and then reading it back: every stage
    // of the arbitration is a row, so it can be watched on a page and held by
    // hand. A law that obeyed its own local variable would ignore the force
    // and leave the row saying something that was not happening.
    Live f;
    fine_weather(f);
    Site::RoofLockout lock_(Botanik::SetupContext(f.reg), f.bank);
    Site::WindowsController win(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &lock_, &win };
    Cycle d(progs, 2, &f.bank, f.reg);

    f.bank.var(f.ref("cli_windowDem")).set(1.0f);
    uint64_t t = 0;
    for (uint32_t i = 0; i < 40; i++) d.step(t += 1000);
    REQUIRE(f.bank.var(f.ref("cli_window1_pos")).as_f32() > 0.9f);
    REQUIRE_FALSE(f.bank.var(f.ref("cli_windowLockout")).b());

    INFO("the rain lockout forced by hand: the roof shuts on the ROW, and the "
         "verdict beside it says the roof is being held");
    REQUIRE(f.bank.override_set(f.bank.var(f.ref("cli_rainLockout")), "1")
            == Vars::SET_OK);
    for (uint32_t i = 0; i < 60; i++) d.step(t += 1000);
    CHECK(f.bank.var(f.ref("cli_windowLockout")).b());
    CHECK(f.bank.var(f.ref("cli_window1_pos")).as_f32() < 0.1f);

    INFO("...and releasing it gives the roof back to the ladder");
    f.bank.override_clear(f.bank.var(f.ref("cli_rainLockout")));
    for (uint32_t i = 0; i < 60; i++) d.step(t += 1000);
    CHECK_FALSE(f.bank.var(f.ref("cli_windowLockout")).b());
    CHECK(f.bank.var(f.ref("cli_window1_pos")).as_f32() > 0.9f);
}

TEST_CASE_FIXTURE(Rig<Site::CoolingController>, "cooling: an intake gate holds while the two temperatures converge") {

    Vars::RefIn<bool> gate = live.bank.resolve_in<bool>("cli_intakeCoolOk");
    const float margin = live.bank.var(live.ref("cli_ventTempMgn_degC")).as_f32();
    const float hyst   = live.bank.var(live.ref("cli_ventHyst_degC")).as_f32();

    field(live, "cli_targetTemp_degC", 20.0f);
    interior(live, 25.0f, 25.0f);
    field(live, "cli_temp_cs", 25.0f - margin - 1.0f);
    d.step(100);
    REQUIRE(live.bank.get(gate));

    INFO("the crawlspace warms to within the margin - the difference the gate "
         "is made of goes to zero as the two spaces converge - and the gate "
         "holds rather than flipping beat to beat");
    field(live, "cli_temp_cs", 25.0f - margin + hyst * 0.5f);
    for (uint32_t i = 0; i < 20; i++) d.step(200 + 100u * i);
    CHECK(live.bank.get(gate));

    INFO("...and it shuts once the difference is a whole band under the margin");
    field(live, "cli_temp_cs", 25.0f - margin + hyst + 0.1f);
    d.step(3000);
    CHECK_FALSE(live.bank.get(gate));
}

TEST_CASE_FIXTURE(Rig<Site::ScreenController>, "screen: the phase gives permission, the demand only asks") {

    Vars::RefIn<bool> screen = live.bank.resolve_in<bool>("cli_screen");
    const float deploy = live.bank.var(live.ref("cli_thermalScreenDem")).as_f32();

    // The morning opens it: retracting is what the day half of the cycle is
    // allowed to do, and a warm demand asks for it.
    live.bank.var(live.ref("cli_phase")).set_i((int64_t)Site::PHASE_DAWN_BREEZE);
    live.bank.var(live.ref("cli_temp_hi")).publish(from_f32(T_F32, 18.0f), 0);
    live.bank.var(live.ref("cli_coolDem")).set(0.5f);
    d.step(100);
    REQUIRE_FALSE(live.bank.get(screen));

    INFO("a cold hour at MIDDAY does not close the fabric against the sun, "
         "however negative the demand goes");
    live.bank.var(live.ref("cli_phase")).set_i((int64_t)Site::PHASE_DAY);
    live.bank.var(live.ref("cli_temp_hi")).publish(from_f32(T_F32, 8.0f), 100);
    live.bank.var(live.ref("cli_coolDem")).set(deploy - 0.5f);
    d.step(200);
    CHECK_FALSE(live.bank.get(screen));

    INFO("the same demand at dusk deploys it: the day's direction is what "
         "decides, and dusk is closing down");
    live.bank.var(live.ref("cli_phase")).set_i((int64_t)Site::PHASE_DUSK_BREEZE);
    d.step(300);
    CHECK(live.bank.get(screen));

    INFO("and a mild NIGHT does not open it again - retracting belongs to the "
         "half of the day that opens up, so the heat it holds is kept");
    live.bank.var(live.ref("cli_phase")).set_i((int64_t)Site::PHASE_NIGHT);
    live.bank.var(live.ref("cli_temp_hi")).publish(from_f32(T_F32, 22.0f), 300);
    live.bank.var(live.ref("cli_coolDem")).set(0.5f);
    d.step(400);
    CHECK(live.bank.get(screen));

    INFO("the morning is what retracts it");
    live.bank.var(live.ref("cli_phase")).set_i((int64_t)Site::PHASE_DAWN_BREEZE);
    d.step(500);
    CHECK_FALSE(live.bank.get(screen));
}

TEST_CASE_FIXTURE(IrrigationRig, "irrigation: a force is spent on the watering it asked for") {
    summer(live);
    no_soak(live);
    slot(live);
    field(live, "cli_temp_ext", 25.0f);
    HAL::Pulse::attach(Site::pulse_inputs[0].channel, Site::pulse_inputs[0].edge);
    live.bank.param_set(live.bank.var(live.ref("irr_liters_tray2")), 5.0f);
    live.bank.param_set(live.bank.var(live.ref("irr_input_qtyPerImp")), 1.0f);
    REQUIRE(live.bank.param_set(live.bank.var(live.ref("irr_forceOn_tray2")), true) == SET_OK);

    uint64_t t = 0;
    d.step(t += 1000);
    REQUIRE(live.bank.var(live.ref("irr_activeTray")).i() == 2);

    INFO("the force is cleared as the fill completes: left standing it would "
         "ask for the same tray on every cycle for ever, and no other tray "
         "would ever be reached");
    HAL::Pulse::inject(Site::pulse_inputs[0].channel, 20);
    d.step(t += 1000);
    REQUIRE(live.bank.var(live.ref("irr_state")).i() == Site::IRR_DRAINING);
    CHECK_FALSE(live.bank.var(live.ref("irr_forceOn_tray2")).b());

    INFO("...and it is owed to flash, like every other schedule edit");
    CHECK(live.bank.var(live.ref("irr_forceOn_tray2")).param_unsaved());
}

TEST_CASE_FIXTURE(IrrigationRig, "irrigation: a skip is spent too, and sends the tray to the back") {
    summer(live);
    slot(live);
    field(live, "cli_temp_ext", 25.0f);

    // Every tray is due at bring-up - none has ever been served - so the one
    // held back is the one the cycle would otherwise have taken first.
    REQUIRE(live.bank.param_set(live.bank.var(live.ref("irr_forceOff_tray1")), true) == SET_OK);

    d.step(1000);
    INFO("tray 1 is passed over, and the next due tray goes instead");
    CHECK(live.bank.var(live.ref("irr_activeTray")).i() == 2);

    INFO("the skip is spent doing it, and the tray is stamped so it waits its "
         "period rather than arriving again tomorrow with the flag gone");
    CHECK_FALSE(live.bank.var(live.ref("irr_forceOff_tray1")).b());
    CHECK(live.bank.var(live.ref("irr_last_tray1")).i() > 0);
    CHECK(live.bank.var(live.ref("irr_last_tray1")).param_unsaved());
}

TEST_CASE_FIXTURE(IrrigationRig, "irrigation: the tray sits full for its soak before either end opens") {
    summer(live);
    slot(live);
    field(live, "cli_temp_ext", 25.0f);
    HAL::Pulse::attach(Site::pulse_inputs[0].channel, Site::pulse_inputs[0].edge);
    live.bank.param_set(live.bank.var(live.ref("irr_forceOn_tray1")), true);
    live.bank.param_set(live.bank.var(live.ref("irr_liters_tray1")), 5.0f);
    live.bank.param_set(live.bank.var(live.ref("irr_input_qtyPerImp")), 1.0f);
    REQUIRE(live.bank.param_set(live.bank.var(live.ref("irr_soak_sec")), "60") == SET_OK);

    uint64_t t = 0;
    d.step(t += 1000);
    REQUIRE(live.bank.var(live.ref("irr_state")).i() == Site::IRR_FILLING);
    HAL::Pulse::inject(Site::pulse_inputs[0].channel, 20);
    d.step(t += 1000);

    INFO("the dose has arrived, and the tray keeps it: both ends of the "
         "manifold shut, and the tray still connected to it");
    REQUIRE(live.bank.var(live.ref("irr_state")).i() == Site::IRR_SOAKING);
    CHECK_FALSE(live.bank.var(live.ref("irr_input_ro1")).b());
    CHECK_FALSE(live.bank.var(live.ref("irr_output_ro1")).b());
    CHECK(live.bank.var(live.ref("irr_tray1")).b());
    CHECK_FALSE(live.bank.var(live.ref("irr_tray2")).b());
    INFO("...and it is still this tray's cycle while it sits");
    CHECK(live.bank.var(live.ref("irr_activeTray")).i() == 1);

    INFO("the fill is judged before the soak, not after it: a tray that got "
         "its water is not owed a verdict an hour later");
    CHECK_FALSE(live.bank.var(live.ref("alarm_irr_fill")).b());
    CHECK(live.bank.var(live.ref("irr_servedToday")).i() == 1);

    INFO("half way through, nothing has moved");
    for (uint32_t i = 0; i < 30; i++) d.step(t += 1000);
    CHECK(live.bank.var(live.ref("irr_state")).i() == Site::IRR_SOAKING);
    CHECK_FALSE(live.bank.var(live.ref("irr_output_ro1")).b());

    INFO("and the drain opens when the soak is up");
    for (uint32_t i = 0; i < 32; i++) d.step(t += 1000);
    CHECK(live.bank.var(live.ref("irr_state")).i() == Site::IRR_DRAINING);
    CHECK(live.bank.var(live.ref("irr_output_ro1")).b());
    CHECK(live.bank.var(live.ref("irr_tray1")).b());
}

TEST_CASE_FIXTURE(IrrigationRig, "irrigation: a soak of zero is skipped, not passed through") {
    // The field bus carries irr_state, so a state that appears for one
    // activation is a state somebody has to explain.
    summer(live);
    no_soak(live);
    slot(live);
    field(live, "cli_temp_ext", 25.0f);
    HAL::Pulse::attach(Site::pulse_inputs[0].channel, Site::pulse_inputs[0].edge);
    live.bank.param_set(live.bank.var(live.ref("irr_forceOn_tray1")), true);
    live.bank.param_set(live.bank.var(live.ref("irr_liters_tray1")), 5.0f);
    live.bank.param_set(live.bank.var(live.ref("irr_input_qtyPerImp")), 1.0f);

    uint64_t t = 0;
    d.step(t += 1000);
    REQUIRE(live.bank.var(live.ref("irr_state")).i() == Site::IRR_FILLING);
    HAL::Pulse::inject(Site::pulse_inputs[0].channel, 20);
    d.step(t += 1000);
    CHECK(live.bank.var(live.ref("irr_state")).i() == Site::IRR_DRAINING);
}

TEST_CASE_FIXTURE(Rig<Site::WaterMeters>, "irrigation: the drain meter comes back to zero, not to a tenth of one") {
    // A deadband only writes when the value moves MORE than its band, so a
    // flow decaying toward zero leaves the row parked on the last figure it
    // reported. That is a drain meter saying water is moving with every valve
    // shut, which is the one thing a leak detector must not be told.

    Vars::RefIn<float> shown = live.bank.resolve_in<float>("irr_outFlow_Lpm");
    uint64_t t = 0;
    for (uint32_t i = 0; i < 20; i++) {
        field(live, "irr_out_Lpm", 30.0f, t);
        d.step(t += 1000);
    }
    REQUIRE(live.bank.get(shown) > 25.0f);

    INFO("the drain stops, and the row says so exactly");
    for (uint32_t i = 0; i < 20; i++) {
        field(live, "irr_out_Lpm", 0.0f, t);
        d.step(t += 1000);
    }
    CHECK(live.bank.get(shown) == doctest::Approx(0.0f));

    INFO("...and the next real flow is written at once, not after a band from "
         "a figure the plant has left behind");
    field(live, "irr_out_Lpm", 12.0f, t);
    d.step(t += 1000);
    CHECK(live.bank.get(shown) > 0.0f);
}

TEST_CASE_FIXTURE(Rig<Site::WaterMeters>,
                  "irrigation: a state change raises no supply fault the rate window has yet to catch up with") {
    // The inlet rate is the LAST COMPLETED window and stands until the next one
    // closes, so for irr_rateWin_ms after a state change it is still reporting
    // the state before: the flush's flow read as a leak the instant the cycle
    // went idle, and idle's zero read as a pressure fault the instant a fill
    // opened. Both fired on every cycle the plant ran.
    const VarRef st   = live.ref("irr_state");
    const VarRef leak = live.ref("alarm_water_leak");
    const VarRef pres = live.ref("alarm_water_pressure");
    // The law waits two of these: a window straddling the change closes with
    // the old state's water in it, and that figure stands for a window more.
    const uint32_t win =
        2u * (uint32_t)live.bank.var(live.ref("irr_rateWin_ms")).i();
    HAL::Pulse::attach(Site::pulse_inputs[0].channel,
                       Site::pulse_inputs[0].edge);
    live.bank.param_set(live.bank.var(live.ref("irr_input_qtyPerImp")), 1.0f);

    uint64_t t = 0;
    live.bank.var(st).set_i((int64_t)Site::IRR_FILLING);
    for (uint32_t i = 0; i < 20; i++) {          // a litre a second, so 60 Lpm
        HAL::Pulse::inject(Site::pulse_inputs[0].channel, 1);
        d.step(t += 1000);
    }
    REQUIRE(live.bank.var(live.ref("irr_inFlow_Lpm")).as_f32() > 30.0f);

    INFO("the cycle goes idle with the window still full of the fill's water");
    live.bank.var(st).set_i((int64_t)Site::IRR_IDLE);
    for (uint32_t i = 0; i <= win / 1000u + 2u; i++) {
        d.step(t += 1000);
        CHECK_FALSE(live.bank.var(leak).b());
    }

    INFO("...but water still moving a window later IS a leak");
    for (uint32_t i = 0; i < win / 1000u + 4u; i++) {
        HAL::Pulse::inject(Site::pulse_inputs[0].channel, 1);
        d.step(t += 1000);
    }
    CHECK(live.bank.var(leak).b());

    INFO("and the mirror image: a fill opening on an idle window is not a "
         "pressure fault");
    live.bank.var(st).set_i((int64_t)Site::IRR_IDLE);
    for (uint32_t i = 0; i < win / 1000u + 4u; i++) d.step(t += 1000);
    REQUIRE_FALSE(live.bank.var(pres).b());
    live.bank.var(st).set_i((int64_t)Site::IRR_FILLING);
    for (uint32_t i = 0; i + 1u < win / 1000u; i++) {   // up TO the window
        d.step(t += 1000);
        CHECK_FALSE(live.bank.var(pres).b());
    }

    INFO("...while a supply that never arrives still is");
    for (uint32_t i = 0; i < win / 1000u + 4u; i++) d.step(t += 1000);
    CHECK(live.bank.var(pres).b());
}

TEST_CASE("irrigation: the day the schedule runs on is today's date") {
    // Two ways of asking what day it is, and they have to agree. They did not:
    // the date came off cal_time_ms(), which takes an UPTIME-domain instant and
    // projects it onto the wall, so a wall time handed to it was projected a
    // second time - a date around 2083, gaining a day every twelve hours. Every
    // tray's interval was therefore half what its parameter said.
    Live f;
    Site::IrrigationSchedule sched(Botanik::SetupContext(f.reg), f.bank);
    Site::IrrigationCycle irr(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &sched, &irr };
    Cycle d(progs, 2, &f.bank, f.reg);
    summer(f);
    d.step(1000);

    const uint32_t want = Utils::cal_time_ms(HAL::Clock::uptime_ms()).unixDays;
    CHECK((uint32_t)f.bank.var(f.ref("irr_today")).i() == want);
}

TEST_CASE_FIXTURE(IrrigationRig, "irrigation: a plant with no clock still spends the force it was given") {
    // The path a stored force takes on hardware that has just booted: the RTC
    // has never been set, NTP has not answered, and there is no date to stamp
    // the watering against. The force must be spent ANYWAY - unspent it asks
    // for the same tray on the next cycle, and the one after that, and nothing
    // bounds the day either, so one tray is flooded until somebody notices.
    summer(live);
    no_soak(live);
    slot(live);
    field(live, "cli_temp_ext", 25.0f);
    HAL::Pulse::attach(Site::pulse_inputs[0].channel, Site::pulse_inputs[0].edge);
    live.bank.param_set(live.bank.var(live.ref("irr_liters_tray2")), 5.0f);
    live.bank.param_set(live.bank.var(live.ref("irr_input_qtyPerImp")), 1.0f);
    REQUIRE(live.bank.param_set(live.bank.var(live.ref("irr_forceOn_tray2")), true) == SET_OK);

    // No date, which is what the row reads before the clock is set. Forced,
    // because the host running this test does have one.
    REQUIRE(live.bank.override_set(live.bank.var(live.ref("irr_today")), "0") == SET_OK);

    uint64_t t = 0;
    d.step(t += 1000);
    INFO("a force is honoured with no schedule behind it: that is what a force "
         "is for, and the calendar is the only thing the clock is needed by");
    REQUIRE(live.bank.var(live.ref("irr_activeTray")).i() == 2);

    HAL::Pulse::inject(Site::pulse_inputs[0].channel, 20);
    d.step(t += 1000);
    REQUIRE(live.bank.var(live.ref("irr_state")).i() == Site::IRR_DRAINING);
    INFO("the force is spent, and the day's tally counted, though neither has "
         "a date to be recorded against");
    CHECK_FALSE(live.bank.var(live.ref("irr_forceOn_tray2")).b());
    CHECK(live.bank.var(live.ref("irr_servedToday")).i() == 1);
    INFO("...and nothing was stamped, because there is nothing to stamp");
    CHECK(live.bank.var(live.ref("irr_last_tray2")).i() == 0);
}

TEST_CASE_FIXTURE(IrrigationRig, "irrigation: `water now` is spent on the cycle it started") {
    // The other force, and the dangerous one: it overrides both the slot and
    // the day's allowance, so a flag nothing clears runs cycle after cycle at
    // any hour until an operator notices the water bill.
    summer(live);
    no_soak(live);
    field(live, "cli_temp_ext", 25.0f);
    HAL::Pulse::attach(Site::pulse_inputs[0].channel, Site::pulse_inputs[0].edge);
    live.bank.param_set(live.bank.var(live.ref("irr_liters_tray1")), 5.0f);
    live.bank.param_set(live.bank.var(live.ref("irr_input_qtyPerImp")), 1.0f);

    // The middle of the afternoon, which is no kind of watering slot.
    live.bank.var(live.ref("cli_phase")).set_i((int64_t)Site::PHASE_DAY);
    live.bank.var(live.ref("sol_fromRise_min")).set_i(600);

    uint64_t t = 0;
    d.step(t += 1000);
    REQUIRE(live.bank.var(live.ref("irr_state")).i() == Site::IRR_IDLE);

    INFO("`water now` overrides the slot, which is what it is for");
    REQUIRE(live.bank.param_set(live.bank.var(live.ref("irr_forceNow")), true) == SET_OK);
    d.step(t += 1000);
    REQUIRE(live.bank.var(live.ref("irr_state")).i() == Site::IRR_FILLING);

    INFO("...and is spent as the fill completes, so the plant goes back to its "
         "slots rather than running cycles until somebody clears the flag");
    HAL::Pulse::inject(Site::pulse_inputs[0].channel, 20);
    d.step(t += 1000);
    REQUIRE(live.bank.var(live.ref("irr_state")).i() == Site::IRR_DRAINING);
    CHECK_FALSE(live.bank.var(live.ref("irr_forceNow")).b());
    CHECK(live.bank.var(live.ref("irr_forceNow")).param_unsaved());
}

TEST_CASE_FIXTURE(IrrigationRig, "irrigation: the afternoon is not a watering slot") {
    // The meters FIRST: the cycle judges a fill against what they publish.
    summer(live);

    field(live, "cli_temp_ext", 25.0f);
    live.bank.param_set(live.bank.var(live.ref("irr_forceOn_tray1")), true);
    live.bank.param_set(live.bank.var(live.ref("irr_liters_tray1")), 5.0f);

    // DAY, but hours past the morning window: a tray filled now is still wet
    // when the stomata open.
    const int16_t rise = (int16_t)(live.bank.var(live.ref("sol_dayRise_min")).i() +
                                   live.bank.var(live.ref("irr_morningWin_min")).i() + 1);
    live.bank.var(live.ref("cli_phase")).set_i((int64_t)Site::PHASE_DAY);
    live.bank.var(live.ref("sol_fromRise_min")).set_i(rise);
    d.step(1000);
    CHECK(live.bank.var(live.ref("irr_state")).i() == Site::IRR_IDLE);

    INFO("inside the morning window it goes");
    live.bank.var(live.ref("sol_fromRise_min")).set_i((int64_t)(rise - 2));
    d.step(2000);
    CHECK(live.bank.var(live.ref("irr_state")).i() == Site::IRR_FILLING);

    INFO("and a cold end of night waits for that window rather than watering "
         "cold roots");
    Live g;
    Site::WaterMeters     meters2(Botanik::SetupContext(g.reg), g.bank);
    Site::IrrigationCycle irr2(Botanik::SetupContext(g.reg), g.bank);
    Plc::Program* const progs2[] = { &meters2, &irr2 };
    Cycle d2(progs2, 2, &g.bank, g.reg);
    summer(g);
    g.bank.param_set(g.bank.var(g.ref("irr_forceOn_tray1")), true);
    field(g, "cli_temp_ext",
          g.bank.var(g.ref("irr_dawnGate_degC")).as_f32() - 1.0f);
    g.bank.var(g.ref("cli_phase")).set_i((int64_t)Site::PHASE_DAWN_BREEZE);
    d2.step(1000);
    CHECK(g.bank.var(g.ref("irr_state")).i() == Site::IRR_IDLE);
}

TEST_CASE_FIXTURE(Rig<Site::ScreenController>, "screen: a safe request DEPLOYS it, and it parks only once it is there") {

    // A warm DAY, so the law itself wants it removed and the safe state is the
    // only thing that could deploy it.
    live.bank.var(live.ref("cli_phase")).set_i((int64_t)Site::PHASE_DAY);
    live.bank.var(live.ref("cli_temp_hi")).publish(from_f32(T_F32, 25.0f), 0);
    live.bank.var(live.ref("cli_coolDem")).set(0.5f);
    uint64_t t = 0;
    for (uint32_t i = 0; i < 700; i++) d.step(t += 100);
    REQUIRE_FALSE(live.bank.get(live.bank.resolve_in<bool>("cli_screen")));

    prog.request_safe(true);
    d.step(t += 100);
    CHECK(live.bank.get(live.bank.resolve_in<bool>("cli_screen")));
    INFO("deployed is COMMANDED here, not reached: the fabric is still travelling");
    CHECK_FALSE(prog.is_parked());

    for (uint32_t i = 0; i < 700; i++) d.step(t += 100);
    CHECK(prog.is_parked());
}

TEST_CASE_FIXTURE(RoofRig, "windows: exactly one ram is energised at a time") {
    fine_weather(live);

    live.bank.var(live.ref("cli_windowDem")).set(1.0f);

    uint64_t t = 0;
    for (uint32_t i = 0; i < 800; i++) {
        d.step(t += 100);
        uint8_t energised = 0;
        for (uint8_t w = 1; w <= 4; w++) {
            char opn[24], clo[24];
            snprintf(opn, sizeof(opn), "cli_window%u_opn", (unsigned)w);
            snprintf(clo, sizeof(clo), "cli_window%u_clo", (unsigned)w);
            const bool o = live.bank.var(live.ref(opn)).b();
            const bool c = live.bank.var(live.ref(clo)).b();
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
        CHECK(live.bank.var(live.ref(pos)).as_f32() == doctest::Approx(1.0f).epsilon(0.05));
    }
}

TEST_CASE_FIXTURE(RoofRig, "windows: rain shuts them, and the lockout outlives the shower") {
    fine_weather(live);

    Vars::RefIn<float> tgt = live.bank.resolve_in<float>("cli_window1_tgt");
    Vars::RefIn<bool>  rain_lock = live.bank.resolve_in<bool>("cli_rainLockout");
    live.bank.var(live.ref("cli_windowDem")).set(1.0f);

    uint64_t t = 0;
    for (uint32_t i = 0; i < 800; i++) d.step(t += 100);
    REQUIRE(live.bank.get(tgt) == doctest::Approx(1.0f));

    live.bank.var(live.ref("cli_rain_mmph")).publish(from_f32(T_F32, 0.4f), t);
    d.step(t += 100);
    CHECK(live.bank.get(rain_lock));
    CHECK(live.bank.get(tgt) == doctest::Approx(0.0f));

    INFO("the shower stopping does not re-open them");
    live.bank.var(live.ref("cli_rain_mmph")).publish(from_f32(T_F32, 0.0f), t);
    d.step(t += 100);
    CHECK(live.bank.get(rain_lock));

    const uint32_t hold = live.bank.var(live.ref("cli_rainLockout_sec")).value().as_u32();
    for (uint32_t i = 0; i < 10 * hold + 20; i++) d.step(t += 100);
    CHECK_FALSE(live.bank.get(rain_lock));
    CHECK(live.bank.get(tgt) == doctest::Approx(1.0f));
}

TEST_CASE_FIXTURE(RoofRig, "windows: a gust shuts them, and the mean alone does not") {
    fine_weather(live);

    Vars::RefIn<bool> wind_lock = live.bank.resolve_in<bool>("cli_windLockout");
    const float shut = live.bank.var(live.ref("cli_windShut_mps")).as_f32();
    live.bank.var(live.ref("cli_windowDem")).set(1.0f);

    live.bank.var(live.ref("cli_wind_mps")).publish(from_f32(T_F32, shut - 1.0f), 0);
    d.step(100);
    CHECK_FALSE(live.bank.get(wind_lock));

    INFO("the PEAK is what tears a window off its ram, not the average");
    live.bank.var(live.ref("cli_windGust_mps")).publish(from_f32(T_F32, shut + 1.0f), 100);
    // Two beats: roof_lockout decides on the first and the windows obey on the
    // next, because the two are separate tasks and the registry staggers them.
    d.step(200);
    d.step(300);
    CHECK(live.bank.get(wind_lock));
    CHECK(live.bank.get(live.bank.resolve_in<float>("cli_window1_tgt")) ==
          doctest::Approx(0.0f));
}

TEST_CASE_FIXTURE(RoofRig, "windows: an unmeasured sky shuts the roof rather than reading fine") {

    live.bank.var(live.ref("cli_rain_mmph")).invalidate(Q_NO_DATA);
    live.bank.var(live.ref("cli_wind_mps")).invalidate(Q_NO_DATA);
    live.bank.var(live.ref("cli_windowDem")).set(1.0f);
    d.step(100);

    CHECK(live.bank.get(live.bank.resolve_in<bool>("cli_rainLockout")));
    CHECK(live.bank.get(live.bank.resolve_in<bool>("cli_windLockout")));
    // The program that LOOKS at the sky is the one that cannot see it. The
    // windows read a lockout row, which is as readable as ever.
    CHECK(lock_.health() == Plc::Health::DEGRADED);
    CHECK(wc.health() != Plc::Health::DEGRADED);
}

TEST_CASE_FIXTURE(RoofRig, "windows: parking waits for the LAST ram to seat, not the first") {
    fine_weather(live);

    live.bank.var(live.ref("cli_windowDem")).set(1.0f);
    uint64_t t = 0;
    for (uint32_t i = 0; i < 800; i++) d.step(t += 100);
    REQUIRE(live.bank.get(live.bank.resolve_in<float>("cli_window4_pos")) > 0.9f);

    wc.request_safe(true);
    // One at a time, so the first ram reaches the safe opening long before the
    // fourth - which is PART OPEN, not shut.
    for (uint32_t i = 0; i < 200; i++) d.step(t += 100);
    REQUIRE(live.bank.get(live.bank.resolve_in<float>("cli_window1_pos")) ==
            doctest::Approx(safe_open(live)).epsilon(0.15));
    CHECK_FALSE(wc.is_parked());

    for (uint32_t i = 0; i < 800; i++) d.step(t += 100);
    CHECK(wc.is_parked());
}

TEST_CASE_FIXTURE(Rig<Site::IntractorController>, "intractors: the fusion is a maximum of the demands whose gate is open") {

    Vars::RefIn<float> intr = live.bank.resolve_in<float>("cli_intr1");
    const float start = live.bank.var(live.ref("cli_intakeStartDem")).as_f32();

    INFO("under the staging threshold they are at their FLOOR, not stopped: "
         "the row declares the safe state and some air is always wanted");
    live.bank.var(live.ref("cli_intakeCoolOk")).set(true);
    live.bank.var(live.ref("cli_coolDem")).set(start - 0.1f);
    d.step(100);
    CHECK(live.bank.get(intr) == doctest::Approx(0.15f));

    INFO("the rot guard alone gets them going: each input is a floor");
    live.bank.var(live.ref("cli_rotVentDem")).set(1.0f);
    d.step(200);
    CHECK(live.bank.get(intr) == doctest::Approx(1.0f));

    INFO("asking to HUMIDIFY is this device's business too: the air it draws "
         "comes in through the wetted skirt, which is the only humidifier the "
         "plant has");
    live.bank.var(live.ref("cli_rotVentDem")).set(0.0f);
    live.bank.var(live.ref("cli_humidDem")).set(1.0f);
    d.step(300);
    CHECK(live.bank.get(intr) == doctest::Approx(1.0f));
    live.bank.var(live.ref("cli_rotVentDem")).set(1.0f);

    INFO("...but asking to DEHUMIDIFY is, once the air outside is drier: the "
         "gate is the humidity law's verdict, not this one's");
    live.bank.var(live.ref("cli_rotVentDem")).set(0.0f);
    live.bank.var(live.ref("cli_humidDem")).set(-1.0f);
    d.step(400);
    CHECK(live.bank.get(intr) == doctest::Approx(0.15f));
    live.bank.var(live.ref("cli_intakeDryOk")).set(true);
    d.step(500);
    CHECK(live.bank.get(intr) == doctest::Approx(1.0f));
    CHECK(live.bank.get(live.bank.resolve_in<float>("cli_intr2")) == doctest::Approx(1.0f));
}

TEST_CASE_FIXTURE(Rig<Site::IntractorController>, "intractors: cooling only draws air that is worth drawing") {

    Vars::RefIn<float> intr = live.bank.resolve_in<float>("cli_intr1");

    INFO("a full cooling demand with the intake no cooler than the house does "
         "nothing: the fans would only bring the outside in");
    live.bank.var(live.ref("cli_coolDem")).set(1.0f);
    d.step(100);
    CHECK(live.bank.get(intr) == doctest::Approx(0.15f));

    live.bank.var(live.ref("cli_intakeCoolOk")).set(true);
    d.step(200);
    CHECK(live.bank.get(intr) == doctest::Approx(1.0f));

    INFO("and the NEGATIVE half is a demand of its own: cold inside, warm "
         "ground, and the same fans pull the stored heat up");
    live.bank.var(live.ref("cli_intakeCoolOk")).set(false);
    live.bank.var(live.ref("cli_coolDem")).set(-1.0f);
    d.step(300);
    CHECK(live.bank.get(intr) == doctest::Approx(0.15f));
    live.bank.var(live.ref("cli_groundHeatOk")).set(true);
    d.step(400);
    INFO("one rung, at the spec's low speed: pulling harder on a cold house "
         "cools the ground rather than warming the house");
    CHECK(live.bank.get(intr) ==
          doctest::Approx(live.bank.var(live.ref("cli_groundSpeed")).as_f32()));
}

TEST_CASE_FIXTURE(Rig<Site::FanController>, "fans: the day's two breezes are a floor under the brassage") {

    // Nothing else asking: no stratification, no cooling, no cycle.
    live.bank.var(live.ref("cli_stratDem")).set(0.0f);
    live.bank.var(live.ref("cli_coolDem")).set(0.0f);
    live.bank.var(live.ref("cli_phase")).set_i((int64_t)Site::PHASE_DAY);
    d.step(100);
    CHECK_FALSE(live.bank.get(live.bank.resolve_in<bool>("cli_destrat1_speed1")));

    INFO("dusk: a light circulation dries the leaves before the stomata open - "
         "these plants breathe at night, and a wet leaf cannot");
    live.bank.var(live.ref("cli_phase")).set_i((int64_t)Site::PHASE_DUSK_BREEZE);
    d.step(200);
    CHECK(live.bank.get(live.bank.resolve_in<bool>("cli_destrat1_speed1")));

    INFO("...and dawn, for the night's condensation");
    live.bank.var(live.ref("cli_phase")).set_i((int64_t)Site::PHASE_DAWN_BREEZE);
    d.step(300);
    CHECK(live.bank.get(live.bank.resolve_in<bool>("cli_destrat1_speed1")));

    INFO("a FLOOR, not a target: cooling still takes them higher");
    live.bank.var(live.ref("cli_coolDem")).set(1.0f);
    d.step(400);
    CHECK(live.bank.get(live.bank.resolve_in<bool>("cli_destrat1_speed3")));

    live.bank.var(live.ref("cli_phase")).set_i((int64_t)Site::PHASE_NIGHT);
    live.bank.var(live.ref("cli_coolDem")).set(0.0f);
    for (uint32_t i = 0; i < 5; i++) d.step(500 + 100u * i);
    CHECK_FALSE(live.bank.get(live.bank.resolve_in<bool>("cli_destrat1_speed1")));
}

TEST_CASE_FIXTURE(Rig<Site::FanController>, "fans: one speed relay at a time, and a band change costs hysteresis") {

    Vars::RefIn<bool> s1 = live.bank.resolve_in<bool>("cli_destrat1_speed1");
    Vars::RefIn<bool> s2 = live.bank.resolve_in<bool>("cli_destrat1_speed2");
    Vars::RefIn<bool> s3 = live.bank.resolve_in<bool>("cli_destrat1_speed3");
    const float med = live.bank.var(live.ref("cli_fanMedDem")).as_f32();

    live.bank.var(live.ref("cli_stratDem")).set(med + 0.01f);
    d.step(100);
    CHECK_FALSE(live.bank.get(s1));
    CHECK(live.bank.get(s2));
    CHECK_FALSE(live.bank.get(s3));

    INFO("just under the threshold again is not a band change");
    live.bank.var(live.ref("cli_stratDem")).set(med - 0.01f);
    d.step(200);
    CHECK(live.bank.get(s2));

    INFO("...it takes the whole hysteresis to give the band back");
    live.bank.var(live.ref("cli_stratDem")).set(med - 0.2f);
    d.step(300);
    CHECK(live.bank.get(s1));
    CHECK_FALSE(live.bank.get(s2));
}

TEST_CASE_FIXTURE(Rig<Site::FanController>, "fans: a travelling screen cuts them, and a safe request leaves speed 1") {

    Vars::RefIn<bool> s1 = live.bank.resolve_in<bool>("cli_destrat1_speed1");
    Vars::RefIn<bool> s3 = live.bank.resolve_in<bool>("cli_destrat2_speed3");

    live.bank.var(live.ref("cli_stratDem")).set(1.0f);
    d.step(100);
    REQUIRE(live.bank.get(s3));

    live.bank.var(live.ref("cli_screenMoving")).set(true);
    d.step(200);
    CHECK_FALSE(live.bank.get(s1));
    CHECK_FALSE(live.bank.get(s3));

    INFO("the interlock outranks the safe state too: the fabric is still moving");
    prog.request_safe(true);
    d.step(300);
    CHECK_FALSE(live.bank.get(s1));

    INFO("and once it has stopped, safe is RUNNING at speed 1");
    live.bank.var(live.ref("cli_screenMoving")).set(false);
    d.step(400);
    CHECK(live.bank.get(s1));
    CHECK_FALSE(live.bank.get(s3));
    CHECK(prog.is_parked());
}

TEST_CASE_FIXTURE(Rig<Site::FanController>, "fans: a watering cycle is a floor under them") {

    // Both demands present and asking for nothing: an ABSENT one is a floor of
    // its own, which is the case above this one.
    live.bank.var(live.ref("cli_stratDem")).set(0.0f);
    live.bank.var(live.ref("cli_coolDem")).set(0.0f);
    d.step(100);
    CHECK_FALSE(live.bank.get(live.bank.resolve_in<bool>("cli_destrat1_speed1")));

    live.bank.var(live.ref("irr_state")).set_i((int64_t)Site::IRR_FILLING);
    d.step(200);
    CHECK(live.bank.get(live.bank.resolve_in<bool>("cli_destrat1_speed1")));
}

TEST_CASE_FIXTURE(Rig<Site::SkirtController>, "skirt: the humidify arm is proportional, the cooling arm is staged") {

    Vars::RefIn<float> duty = live.bank.resolve_in<float>("cli_skirtDuty");
    const uint32_t period = live.bank.var(live.ref("cli_skirtPeriod_sec")).value().as_u32();
    const uint32_t min_on = live.bank.var(live.ref("cli_skirtMinOn_sec")).value().as_u32();

    // Dry enough for a wet pad to cool, so the cooling arm is not gated out.
    live.bank.var(live.ref("cli_dpMargin_degC")).publish(from_f32(T_F32, 20.0f), 0);

    live.bank.var(live.ref("cli_humidDem")).set(0.5f);
    d.step(100);
    CHECK(live.bank.get(duty) == doctest::Approx(0.5f).epsilon(0.02));

    INFO("a demand whose pulse would be under the minimum does not open at all");
    live.bank.var(live.ref("cli_humidDem")).set(
        0.5f * (float)min_on / (float)period);
    d.step(200);
    CHECK(live.bank.get(duty) == doctest::Approx(0.0f));

    INFO("asking to DEHUMIDIFY never opens it: that arm belongs to the intractors");
    live.bank.var(live.ref("cli_humidDem")).set(-1.0f);
    d.step(300);
    CHECK(live.bank.get(duty) == doctest::Approx(0.0f));

    INFO("and the cooling arm starts only above its own staging threshold");
    const float start = live.bank.var(live.ref("cli_skirtCoolDem")).as_f32();
    live.bank.var(live.ref("cli_coolDem")).set(start - 0.05f);
    d.step(400);
    CHECK(live.bank.get(duty) == doctest::Approx(0.0f));
    live.bank.var(live.ref("cli_coolDem")).set(1.0f);
    d.step(500);
    CHECK(live.bank.get(duty) == doctest::Approx(1.0f));
}

TEST_CASE_FIXTURE(Rig<Site::SkirtController>, "skirt: damp air gates the cooling arm, and the rot lockout cuts both") {

    Vars::RefIn<bool> valve = live.bank.resolve_in<bool>("cli_skirtValve");
    const float dry_min = live.bank.var(live.ref("cli_skirtDryMin_degC")).as_f32();

    INFO("a wet pad in damp air adds water without cooling anything");
    live.bank.var(live.ref("cli_dpMargin_degC")).publish(from_f32(T_F32, dry_min - 1.0f), 0);
    live.bank.var(live.ref("cli_coolDem")).set(1.0f);
    d.step(100);
    CHECK_FALSE(live.bank.get(valve));

    live.bank.var(live.ref("cli_dpMargin_degC")).publish(from_f32(T_F32, dry_min + 1.0f), 100);
    d.step(200);
    CHECK(live.bank.get(valve));

    INFO("and the rot lockout cuts it whatever either arm is asking for");
    live.bank.var(live.ref("cli_rotLockout")).set(true);
    d.step(300);
    CHECK_FALSE(live.bank.get(valve));
    CHECK(live.bank.get(live.bank.resolve_in<float>("cli_skirtDuty")) == doctest::Approx(0.0f));
}

TEST_CASE_FIXTURE(IrrigationRig, "valves: each parks where its own row says it fails, not all one way") {
    // The meters FIRST: the cycle judges a fill against what they publish.
    summer(live);

    irr.request_safe(true);
    d.step(100);

    INFO("the inlet fails SHUT and the drains fail OPEN, so a filling tray empties");
    CHECK_FALSE(live.bank.get(live.bank.resolve_in<bool>("irr_input_ro1")));
    CHECK(live.bank.get(live.bank.resolve_in<bool>("irr_output_ro1")));
    CHECK(live.bank.get(live.bank.resolve_in<bool>("irr_tray1")));
    CHECK(irr.is_parked());
}

TEST_CASE_FIXTURE(IrrigationRig, "irrigation: a fill fault names the tray, not just the cycle") {
    // The meters FIRST: the cycle judges a fill against what they publish.
    summer(live);

    // Tray 2 alone is due, and nothing will ever arrive to fill it.
    for (uint8_t i = 1; i <= 8; i++) {
        char name[28];
        snprintf(name, sizeof name, "irr_forceOff_tray%u", (unsigned)i);
        REQUIRE(live.bank.param_set(live.bank.var(live.ref(name)), i != 2) == SET_OK);
    }
    REQUIRE(live.bank.param_set(live.bank.var(live.ref("irr_forceOn_tray2")), true) == SET_OK);
    REQUIRE(live.bank.param_set(live.bank.var(live.ref("irr_maxFill_sec")), 100.0f) == SET_OK);
    field(live, "cli_temp_ext", 30.0f);
    slot(live);

    uint64_t t = 0;
    d.step(t += 1000);
    REQUIRE(live.bank.var(live.ref("irr_state")).i() == Site::IRR_FILLING);
    REQUIRE(live.bank.var(live.ref("irr_activeTray")).i() == 2);

    for (uint32_t i = 0; i < 102; i++) d.step(t += 1000);

    INFO("the any-form is what an operator sees; the per-tray one is what still "
         "says which tray once the cycle has moved on");
    CHECK(live.bank.var(live.ref("alarm_irr_fill")).b());
    CHECK(live.bank.var(live.ref("alarm_irr_fill_tray2")).b());
    CHECK_FALSE(live.bank.var(live.ref("alarm_irr_fill_tray1")).b());
    CHECK_FALSE(live.bank.var(live.ref("alarm_irr_fill_tray3")).b());
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

TEST_CASE_FIXTURE(IrrigationRig, "irrigation: the cycle fills to its dose, then drains, then idles") {
    // The meters FIRST: the cycle judges a fill against what they publish.
    summer(live);
    no_soak(live);

    // Bare, not REQUIREd: a channel another case already attached answers false.
    HAL::Pulse::attach(Site::pulse_inputs[0].channel, Site::pulse_inputs[0].edge);

    // Warm enough that the dawn gate is open, in the end-of-night slot, and
    // tray 1 forced so the cycle starts without the calendar the due-check
    // still wants.
    live.bank.var(live.ref("cli_temp_ext")).publish(from_f32(T_F32, 20.0f), 0);
    live.bank.var(live.ref("cli_vpd_kPa")).publish(from_f32(T_F32, 1.5f), 0);
    slot(live);
    live.bank.param_set(live.bank.var(live.ref("irr_forceOn_tray1")), true);
    live.bank.param_set(live.bank.var(live.ref("irr_liters_tray1")), 5.0f);
    live.bank.param_set(live.bank.var(live.ref("irr_input_qtyPerImp")), 1.0f);

    uint64_t t = 0;
    d.step(t += 1000);
    CHECK(live.bank.var(live.ref("irr_state")).i() == Site::IRR_FILLING);
    CHECK(live.bank.var(live.ref("irr_activeTray")).i() == 1);
    INFO("filling: inlet and this tray open, outlet shut, no other tray");
    CHECK(live.bank.var(live.ref("irr_input_ro1")).b());
    CHECK(live.bank.var(live.ref("irr_tray1")).b());
    CHECK_FALSE(live.bank.var(live.ref("irr_output_ro1")).b());
    CHECK_FALSE(live.bank.var(live.ref("irr_tray2")).b());

    // The meter delivers the dose. It is modulated, so ask for plenty.
    HAL::Pulse::inject(Site::pulse_inputs[0].channel, 20);
    d.step(t += 1000);
    CHECK(live.bank.var(live.ref("irr_state")).i() == Site::IRR_DRAINING);
    INFO("draining: inlet shut, tray still open, outlet open");
    CHECK_FALSE(live.bank.var(live.ref("irr_input_ro1")).b());
    CHECK(live.bank.var(live.ref("irr_tray1")).b());
    CHECK(live.bank.var(live.ref("irr_output_ro1")).b());
    CHECK_FALSE(live.bank.var(live.ref("alarm_irr_fill")).b());

    INFO("the day is stamped at the END OF THE FILL, so a fault during the long "
         "drain cannot cause a second watering");
    CHECK(live.bank.var(live.ref("irr_servedToday")).i() == 1);
    const uint32_t stamped = (uint32_t)live.bank.var(live.ref("irr_last_tray1")).i();
    CHECK(stamped > 0);
    // The bank under test has no store bound, so the write stays marked and a
    // later save retries it. That mark is the whole point: drive() would have
    // written the value and left NOTHING owed to flash, so the schedule would
    // restart from nothing at the next boot.
    INFO("it went through the parameter path, so flash is owed the new value");
    CHECK(live.bank.var(live.ref("irr_last_tray1")).param_unsaved());

    const uint32_t window = (uint32_t)live.bank.var(live.ref("irr_drainWindow_sec")).i();
    for (uint32_t i = 0; i <= window + 2; i++) d.step(t += 1000);
    CHECK(live.bank.var(live.ref("irr_activeTray")).i() == 0);
    CHECK(live.bank.var(live.ref("irr_last_tray1")).i() == stamped);
}

namespace {

/** @brief Fill tray 1, then hold @p lpm on the outlet meter for the whole drain
 *  window. @return the litres the meter says came back. */
float drain_at(Live& live, Cycle& d, Site::WaterMeters& meters,
               Site::IrrigationCycle& irr, float lpm, bool meter_live,
               Plc::Health* worst = nullptr) {
    (void)irr;
    no_soak(live);
    live.bank.var(live.ref("cli_temp_ext")).publish(from_f32(T_F32, 20.0f), 0);
    slot(live);
    live.bank.param_set(live.bank.var(live.ref("irr_forceOn_tray1")), true);
    live.bank.param_set(live.bank.var(live.ref("irr_liters_tray1")), 5.0f);
    live.bank.param_set(live.bank.var(live.ref("irr_input_qtyPerImp")), 1.0f);

    uint64_t t = 0;
    d.step(t += 1000);
    HAL::Pulse::inject(Site::pulse_inputs[0].channel, 20);
    d.step(t += 1000);
    REQUIRE(live.bank.var(live.ref("irr_state")).i() == Site::IRR_DRAINING);

    const uint32_t window = (uint32_t)live.bank.var(live.ref("irr_drainWindow_sec")).i();
    for (uint32_t i = 0; i <= window + 2; i++) {
        // Every second: the row's timeout is 5 s, and a meter that went stale
        // half way through would be testing the invalid path instead.
        if (meter_live)
            live.bank.var(live.ref("irr_out_Lpm")).publish(from_f32(T_F32, lpm), t);
        d.step(t += 1000);
        // Health is per ACTIVATION, so it has to be read inside the window: by
        // the time the loop ends the cycle is flushing and healthy again. The
        // METERS are what report a silent instrument now - the cycle only
        // declines to judge on one.
        if (worst != nullptr && meters.health() > *worst) *worst = meters.health();
    }
    return live.bank.var(live.ref("irr_drained_L")).as_f32();
}

}  // namespace

TEST_CASE_FIXTURE(IrrigationRig, "irrigation: the two meters read as a counter and a rate") {
    // The meters FIRST: the cycle judges a fill against what they publish.
    summer(live);
    HAL::Pulse::attach(Site::pulse_inputs[0].channel, Site::pulse_inputs[0].edge);
    live.bank.param_set(live.bank.var(live.ref("irr_input_qtyPerImp")), 1.0f);

    uint64_t t = 0;
    d.step(t += 1000);
    // 20 pulses a second at a litre each, for ten seconds.
    for (uint32_t i = 0; i < 10; i++) {
        HAL::Pulse::inject(Site::pulse_inputs[0].channel, 20);
        live.bank.var(live.ref("irr_out_Lpm")).publish(from_f32(T_F32, 30.0f), t);
        d.step(t += 1000);
    }
    INFO("the inlet counter is the measurement, and its rate is derived");
    CHECK(live.bank.var(live.ref("irr_inTotal_L")).as_f32() == doctest::Approx(200.0f));
    CHECK(live.bank.var(live.ref("irr_inFlow_Lpm")).as_f32() == doctest::Approx(1200.0f).epsilon(0.2));

    INFO("the outlet rate is the measurement, and its counter is derived");
    CHECK(live.bank.var(live.ref("irr_outFlow_Lpm")).as_f32() == doctest::Approx(30.0f).epsilon(0.1));
    const float out = live.bank.var(live.ref("irr_outTotal_L")).as_f32();
    CHECK(out > 3.0f);
    CHECK(out < 6.0f);
}

TEST_CASE_FIXTURE(IrrigationRig, "irrigation: what comes back out is judged against what went in") {
    // The meters FIRST: the cycle judges a fill against what they publish.
    summer(live);
    // Bare, not REQUIREd: a channel another case already attached answers
    // false, and this one only needs it counting.
    HAL::Pulse::attach(Site::pulse_inputs[0].channel, Site::pulse_inputs[0].edge);

    // 20 L went in; 5 L/min over the 300 s window is far more than the 30% the
    // margin allows to be missing.
    const float back = drain_at(live, d, meters, irr, 5.0f, true);
    CHECK(back > 20.0f);
    CHECK_FALSE(live.bank.var(live.ref("alarm_irr_drain")).b());
    CHECK_FALSE(live.bank.var(live.ref("alarm_irr_drain_tray1")).b());
}

TEST_CASE_FIXTURE(IrrigationRig, "irrigation: a tray that does not drain names itself") {
    // The meters FIRST: the cycle judges a fill against what they publish.
    summer(live);
    HAL::Pulse::attach(Site::pulse_inputs[0].channel, Site::pulse_inputs[0].edge);

    // A trickle: half a litre back out of twenty.
    const float back = drain_at(live, d, meters, irr, 0.1f, true);
    CHECK(back < 1.0f);
    CHECK(live.bank.var(live.ref("alarm_irr_drain")).b());
    CHECK(live.bank.var(live.ref("alarm_irr_drain_tray1")).b());
    INFO("the OTHER trays are not accused");
    CHECK_FALSE(live.bank.var(live.ref("alarm_irr_drain_tray2")).b());
}

TEST_CASE_FIXTURE(IrrigationRig, "irrigation: a silent outlet meter gives no verdict, not a clog") {
    // The meters FIRST: the cycle judges a fill against what they publish.
    summer(live);
    HAL::Pulse::attach(Site::pulse_inputs[0].channel, Site::pulse_inputs[0].edge);

    Plc::Health worst = Plc::Health::HEALTHY;
    const float back = drain_at(live, d, meters, irr, 0.0f, false, &worst);
    CHECK(back == 0.0f);
    INFO("nothing was measured, so nothing is blamed - the meter has an alarm "
         "of its own");
    CHECK_FALSE(live.bank.var(live.ref("alarm_irr_drain")).b());
    CHECK_FALSE(live.bank.var(live.ref("alarm_irr_drain_tray1")).b());
    INFO("but it does not pass in silence");
    CHECK(worst == Plc::Health::DEGRADED);
}

TEST_CASE_FIXTURE(IrrigationRig, "irrigation: a fill that never arrives raises, and still opens the drain") {
    // The meters FIRST: the cycle judges a fill against what they publish.
    summer(live);
    HAL::Pulse::attach(Site::pulse_inputs[0].channel, Site::pulse_inputs[0].edge);

    live.bank.var(live.ref("cli_temp_ext")).publish(from_f32(T_F32, 20.0f), 0);
    slot(live);
    live.bank.param_set(live.bank.var(live.ref("irr_forceOn_tray1")), true);
    live.bank.param_set(live.bank.var(live.ref("irr_liters_tray1")), 5.0f);
    // The shortest the band allows; a rejected edit would leave the default and
    // the case would just be waiting out five minutes it never reaches.
    REQUIRE(live.bank.param_set(live.bank.var(live.ref("irr_maxFill_sec")), 100.0f) == SET_OK);

    uint64_t t = 0;
    d.step(t += 1000);
    REQUIRE(live.bank.var(live.ref("irr_state")).i() == Site::IRR_FILLING);

    for (uint32_t i = 0; i < 102; i++) d.step(t += 1000);   // no pulses at all

    INFO("leaving the inlet open on a fault is how a tray overflows, and the "
         "soak is skipped with it: this tray was given nothing to steep in");
    CHECK(live.bank.var(live.ref("alarm_irr_fill")).b());
    CHECK(live.bank.var(live.ref("irr_state")).i() == Site::IRR_DRAINING);
    CHECK_FALSE(live.bank.var(live.ref("irr_input_ro1")).b());
}

}  // namespace site_programs
