/**
 * @file test_site_sense.cpp
 * @brief The sensing and climate laws: what each derives, and what it says when it cannot.
 *
 * Driven through the real scan cycle against the site's real tables.
 * The fixtures are program_rig.h; the cycle itself is test_scan.cpp.
 */

#include "program_rig.h"

namespace site_programs {

TEST_CASE_FIXTURE(Rig<Site::TempRHSensors>, "zone: the selector keeps the middle reading and discards the extremes") {
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

TEST_CASE_FIXTURE(Rig<Site::TempRHSensors>, "zone: an invalid probe is excluded, and one good probe still reads") {
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
    CHECK(live.bank.var(live.ref("alarm_sns_ceiling")).as_bool());
}

TEST_CASE_FIXTURE(ZoneRig, "zone: the instruments report percent, the model works in fractions") {

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

TEST_CASE_FIXTURE(Rig<Site::TempRHSensors>, "zone: temperature survives losing every humidity probe") {
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

TEST_CASE_FIXTURE(Rig<Site::TempRHSensors>, "zone: with every probe gone the reading is HELD, not defaulted") {
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
    CHECK(live.bank.var(hi.r).value().q == Q_UNCERTAIN);

    // The two readers see different things, and that is the whole design. The
    // ROW holds the last average this zone could make, so an operator chasing
    // a dead probe finds what the house last really was...
    INFO("the row holds the last average, for whoever is diagnosing it");
    CHECK(live.bank.var(hi.r).as_f32() == doctest::Approx(21.0f));

    // ...while a control program reading through its typed reference gets the
    // DECLARED value: a number somebody chose and reviewed, rather than a
    // reading of unbounded age that looks exactly like a fresh one.
    const Vars::Descriptor& d = live.bank.descriptor(hi.r.index);
    INFO("a law reading it gets the declared value instead");
    CHECK(live.bank.get(hi) == doctest::Approx(Vars::as_f32(d.type, d.def)));
    CHECK(live.bank.get(hi) != doctest::Approx(21.0f));
}

TEST_CASE_FIXTURE(Rig<Site::TempRHSensors>, "zone: a zone that has never been measured has nothing to hold") {
    Vars::RefIn<float> cs = live.bank.resolve_in<float>("cli_temp_cs");

    for (uint64_t t = 0; t <= 2000; t += 100) d.step(t);

    INFO("holding would publish a filter that has never been seeded, which is "
         "the declared default wearing a better label");
    CHECK_FALSE(live.bank.var(cs.r).valid());
    CHECK(live.bank.var(cs.r).value().q == Q_NO_DATA);
}

/** A level and the companion that says it was measured, which is what
 *  TempRHSensors publishes as a pair. */
static void levels(Live& f, float hi, float lo, uint64_t at = 0,
                   bool hi_ok = true, bool lo_ok = true) {
    f.bank.var(f.ref("cli_temp_hi")).publish(from_f32(T_F32, hi), at);
    f.bank.var(f.ref("cli_temp_lo")).publish(from_f32(T_F32, lo), at);
    f.bank.var(f.ref("cli_temp_hi_qlt")).publish(from_bool(T_BOOL, hi_ok), at);
    f.bank.var(f.ref("cli_temp_lo_qlt")).publish(from_bool(T_BOOL, lo_ok), at);
}

TEST_CASE_FIXTURE(Rig<Site::StratSensor>, "strat: the gap is unsigned, so an inverted profile still calls the fans") {

    Vars::RefIn<float> gap = live.bank.resolve_in<float>("cli_dTStrat_degC");

    levels(live, 24.0f, 20.0f, 0);
    d.step(100);
    CHECK(live.bank.get(gap) == doctest::Approx(4.0f));

    INFO("a cold roof over a warm floor is as much a case for mixing");
    levels(live, 18.0f, 22.0f, 200);
    d.step(200);
    CHECK(live.bank.get(gap) == doctest::Approx(4.0f));
}

TEST_CASE_FIXTURE(Rig<Site::StratSensor>, "strat: a level nothing measures makes the gap unmeasured, not zero") {

    levels(live, 24.0f, 20.0f, 0, true, false);
    d.step(100);
    INFO("the companion is what says so - the level row still holds a number, "
         "and it is the one that would fabricate a four-degree gap");
    CHECK_FALSE(live.bank.var(live.ref("cli_dTStrat_degC")).valid());
}

TEST_CASE_FIXTURE(Rig<Site::DayPhaseController>, "day phase: the banks arm off the published extremes, so one can be forced") {
    // The extremes are gathered over hours, so the only way to rehearse a bank
    // phase is to force the row that decides it. That works because this law
    // arms off what it published rather than off the member behind it.

    const float trg = live.bank.var(live.ref("cli_coldBankTrg_degC")).as_f32();
    field(live, "cli_temp_in", trg - 5.0f);
    d.step(100);
    REQUIRE_FALSE(live.bank.var(live.ref("cli_coldBankArm")).as_bool());

    INFO("a hot yesterday forced by hand arms the cold bank");
    char hot[16];
    snprintf(hot, sizeof hot, "%.1f", (double)(trg + 5.0f));
    REQUIRE(live.bank.override_set(live.bank.var(live.ref("cli_prevDayMax_degC")), hot)
            == Vars::SET_OK);
    d.step(200);
    CHECK(live.bank.var(live.ref("cli_coldBankArm")).as_bool());

    INFO("...and releasing it gives the decision back to the house");
    live.bank.override_clear(live.bank.var(live.ref("cli_prevDayMax_degC")));
    d.step(300);
    CHECK_FALSE(live.bank.var(live.ref("cli_coldBankArm")).as_bool());
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

TEST_CASE_FIXTURE(Rig<Site::TargetTempController>, "target: an unplaced phase takes the base setpoint, and stays believable") {

    Vars::RefIn<float> target = live.bank.resolve_in<float>("cli_targetTemp_degC");
    const VarRef phase = live.ref("cli_phase");
    const VarRef cold  = live.ref("cli_coldBankArm");
    const float base = live.bank.var(live.ref("cli_coolSet_degC")).as_f32();
    REQUIRE(base == doctest::Approx(20.0f));      // the site's safe cooling target
    live.bank.param_set(live.bank.var(live.ref("cli_targetTau_s")), 0.0f);

    INFO("armed and in the bank phase, the offset applies");
    live.bank.var(phase).set_i((int64_t)Site::PHASE_COLD_BANK);
    live.bank.var(cold).set(true);
    d.step(100);
    const float ofs = live.bank.var(live.ref("cli_coldBankOfs_degC")).as_f32();
    REQUIRE(live.bank.get(target) == doctest::Approx(base - ofs));
    REQUIRE(live.bank.var(live.ref("cli_targetTemp_degC")).valid());

    INFO("the phase goes unplaceable - no wall clock, so no sun - and the shift "
         "goes with it: an offset from a phase nobody placed is a setpoint "
         "nobody asked for");
    live.bank.var(phase).invalidate(Q_NO_DATA);
    d.step(200);
    CHECK(live.bank.get(target) == doctest::Approx(base));

    INFO("and the CHAIN STOPS HERE: the base setpoint is a proper value, so "
         "the row stays believable for every cooling stage that keys off it");
    CHECK(live.bank.var(live.ref("cli_targetTemp_degC")).valid());
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

TEST_CASE_FIXTURE(Rig<Site::CoolingController>, "cooling: a demand it cannot measure is still made, and marked") {

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

    INFO("the ladder keeps running, on the interior row's DECLARED value - so "
         "there is still a demand for an actuator to fall back from, and the "
         "quality is what says it is not about this greenhouse");
    CHECK_FALSE(live.bank.var(dem).valid());
    CHECK(live.bank.var(dem).value().q == Q_UNCERTAIN);

    // 14 degC declared against a 20 degC target is BELOW it, and the ladder is
    // reverse-acting, so the honest answer on that reading asks for warmth -
    // the opposite of the 30 degC house it was reading a moment ago.
    const Vars::Descriptor& d = live.bank.descriptor(live.ref("cli_temp_in").index);
    REQUIRE(Vars::as_f32(d.type, d.def) < 20.0f);
    CHECK(live.bank.var(dem).as_f32() < 0.0f);
    CHECK(live.bank.var(dem).as_f32() != doctest::Approx(was));
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

    INFO("with the house's own row gone a PID on the defaulted reading would "
         "see 0 degC against the target and call for full HEAT");
    live.bank.var(live.ref("cli_temp_lo")).invalidate(Q_NO_DATA);
    live.bank.var(live.ref("cli_temp_in")).invalidate(Q_NO_DATA);
    d.step(300);
    CHECK_FALSE(live.bank.var(dem).valid());
}

TEST_CASE("cooling: an unknown demand puts every actuator at its own safe state") {
    Live f;
    fine_weather(f);
    interlocks_clear(f);
    // The lockout is ST, so the resource runs ahead of the actuators.
    Site::StRuntime            st(Botanik::SetupContext(f.reg), f.bank);
    Site::WindowsController    win(Botanik::SetupContext(f.reg), f.bank);
    Site::IntractorController  intr(Botanik::SetupContext(f.reg), f.bank);
    Site::ScreenController     scr(Botanik::SetupContext(f.reg), f.bank);
    Site::SkirtController      sk(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &win, &intr, &scr, &sk };
    Cycle cyc(progs, 4, &f.bank, f.reg);
    st.wire();
    struct { Site::StRuntime* st; Cycle* c;
             void step(uint64_t n) { st->scan(n); c->step(n); } } d{ &st, &cyc };

    // Everything else says GO: only the cooling ladder's two outputs are
    // missing, which is how they go - cooling HOLDS both together when it
    // cannot see the house.
    f.bank.var(f.ref("cli_coolDem")).invalidate(Q_NO_DATA);
    f.bank.var(f.ref("cli_windowDem")).invalidate(Q_NO_DATA);
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
    REQUIRE_FALSE(f.bank.var(f.ref("cli_screenMoving")).as_bool());

    INFO("each one substitutes ITS OWN safe state for the input it cannot read, "
         "and not one of the five is simply OFF");
    CHECK(f.bank.get(f.bank.resolve_in<float>("cli_window1_tgt")) ==
          doctest::Approx(safe_open(f)));                         // part open
    CHECK(f.bank.get(f.bank.resolve_in<float>("cli_intr1")) ==
          doctest::Approx(0.15f));                                // some air
    CHECK(f.bank.get(f.bank.resolve_in<bool>("cli_screen")));     // deployed
    CHECK_FALSE(f.bank.get(f.bank.resolve_in<bool>("cli_skirtValve")));  // shut
    CHECK(f.bank.get(f.bank.resolve_in<bool>("cli_destrat1_speed1")));   // speed 1

    // The five that READ a missing demand, not the lockout: roof_lockout is on
    // the beat because the windows need its cap, and it has every input it
    // wants. The ST plant has no per-program health, so what stands in is that
    // it WIRED: every VAR_EXTERNAL in it resolved to a row.
    CHECK(st.wired());
}

TEST_CASE_FIXTURE(StRig, "scan: sys_now_ms is the clock, and a block differences it itself") {
    // A C++ law is HANDED dt through ProgramContext; ST has no argument to be
    // handed anything, so the scan publishes a clock and each block keeps its
    // own last reading. One row serves every rate, and it is the simulator's
    // virtual clock, so accelerated time works unchanged.
    const VarRef now = live.ref("sys_now_ms");
    REQUIRE(now.index != Vars::VAR_NONE);

    d.step(100);
    CHECK(live.bank.var(now).value().as<uint32_t>() == 100u);
    d.step(700);
    CHECK(live.bank.var(now).value().as<uint32_t>() == 700u);
}

TEST_CASE_FIXTURE(StRig, "destrat: an unmeasured gap makes the demand unknown, not zero") {

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

    INFO("an unreadable gap declares the LOW rung, so substitution keeps the "
         "fans mixing rather than reading as 'no stratification' and stopping "
         "them - the demand is written either way");
    live.bank.var(live.ref("cli_dTStrat_degC")).invalidate(Q_NO_DATA);
    d.step(500);
    CHECK(live.bank.var(dem).as_f32() ==
          doctest::Approx(live.bank.var(live.ref("cli_fanLowDem")).as_f32()));
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
}

TEST_CASE_FIXTURE(Rig<Site::WeatherWatch>,
                  "weather: a storm is 75 kmph of sustained wind, not a stiff breeze") {
    // It used to be read off the roof's own lockouts, so a passing shower or a
    // gust over the vent-closing threshold put a storm on the ribbon. Those are
    // reasons to shut the roof, which is a much lower bar than the weather
    // being severe.
    Vars::RefIn<bool> storm = live.bank.resolve_in<bool>("alarm_weather_storm");
    const float shut = live.bank.var(live.ref("cli_windShutEnd_mps")).as_f32();
    uint64_t t = 0;

    field(live, "cli_rain_mmph", 0.0f);
    field(live, "cli_wind_mps", shut - 6.0f);      // a gale, and not a storm
    field(live, "cli_windGust_mps", shut + 4.0f);  // gusting past it, even
    for (uint32_t i = 0; i < 20; i++) d.step(t += 1000);
    CHECK_FALSE(live.bank.get(storm));

    INFO("rain alone is not one either");
    field(live, "cli_rain_mmph", 8.0f);
    for (uint32_t i = 0; i < 20; i++) d.step(t += 1000);
    CHECK_FALSE(live.bank.get(storm));

    INFO("...and the MEAN reaching it is");
    field(live, "cli_wind_mps", shut + 0.5f);
    for (uint32_t i = 0; i < 20; i++) d.step(t += 1000);
    CHECK(live.bank.get(storm));
}

TEST_CASE_FIXTURE(Rig<Site::AcGridWatch>,
                  "grid: the meter's readings are mirrored, and judged") {
    // Every one of these comes out of AcSupply::step(), which the program
    // drives once per beat. A law that never called it published a supply
    // that was always in spec and never faulted, on any wiring at all.
    const VarRef ok  = live.ref("sys_grid_ok");
    const VarRef flt = live.ref("alarm_ac_grid");
    const VarRef v   = live.ref("sys_grid_V");

    const float lo = live.bank.var(live.ref("pwr_gridLo_V")).as_f32();
    const float hi = live.bank.var(live.ref("pwr_gridHi_V")).as_f32();
    REQUIRE(hi > lo);
    const float good = 0.5f * (lo + hi);

    field(live, "grid_LN_V", good);
    field(live, "grid_Hz", 50.0f);
    field(live, "grid_total_A", 12.0f);
    d.step(100);
    INFO("in spec: the verdict says so and the mirror carries the reading");
    CHECK(live.bank.var(ok).as_bool());
    CHECK_FALSE(live.bank.var(flt).as_bool());
    CHECK(live.bank.var(v).as_f32() == doctest::Approx(good).epsilon(0.02));

    INFO("...and out of it, both move - which is the whole of what this "
         "program is for");
    field(live, "grid_LN_V", lo - 10.0f, 200);
    d.step(200);
    CHECK_FALSE(live.bank.var(ok).as_bool());
    CHECK(live.bank.var(flt).as_bool());
}

TEST_CASE_FIXTURE(Rig<Site::WxSensors>,
                  "wx: the voted rows carry a TCP register the stations' own do not") {
    // Nothing controls on irradiance, but the field rows are SCOPE_MI and so
    // have no TCP register: without the voted row a historian on the plant
    // network cannot poll the one reading that says how much sun the glass got.
    const VarRef raw = live.ref("cli_irrad1_Wpm2");
    const VarRef pub = live.ref("cli_irrad_Wpm2");
    REQUIRE(Vars::var_mapped(*live.bank.var(pub).meta()));
    REQUIRE_FALSE(Vars::var_mapped(*live.bank.var(raw).meta()));

    field(live, "cli_irrad1_Wpm2", 800.0f);
    field(live, "cli_irrad2_Wpm2", 840.0f);
    d.step(100);
    INFO("the MEAN, because nothing controls on it and a worst case would "
         "mean nothing");
    CHECK(live.bank.var(pub).as_f32() == doctest::Approx(820.0f));
    CHECK(live.bank.var(pub).valid());
}

TEST_CASE_FIXTURE(CoolingRig, "intake: a gate holds while the two temperatures converge") {

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

}  // namespace site_programs
