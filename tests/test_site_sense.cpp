/**
 * @file test_site_sense.cpp
 * @brief The sensing and climate laws: what each derives, and what it says when it cannot.
 *
 * Driven through the real scan cycle against the site's real tables.
 * The fixtures are program_rig.h; the cycle itself is test_scan.cpp.
 */

#include "program_rig.h"

namespace site_programs {

TEST_CASE_FIXTURE(StRig, "zone: the selector keeps the middle reading and discards the extremes") {
    Vars::Ref<float> hi = live.bank.resolve<float>("cli_temp_hi");
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

TEST_CASE_FIXTURE(StRig, "zone: an invalid probe is excluded, and one good probe still reads") {
    Vars::Ref<float> hi = live.bank.resolve<float>("cli_temp_hi");
    const VarRef p[3] = { live.ref("cli_temp_hi1"), live.ref("cli_temp_hi2"),
                          live.ref("cli_temp_hi3") };


    // One probe answering 25, the other two dead. The dead pair must not vote:
    // they read their declared default, which would drag any average.
    live.bank.var(p[0]).publish(from_f32(T_F32, 25.0f), 0);
    live.bank.var(p[1]).set_quality(Q_NO_DATA);
    live.bank.var(p[2]).set_quality(Q_NO_DATA);
    live.bank.var(live.ref("cli_rh_hi1")).publish(from_f32(T_F32, 50.0f), 0);
    for (uint64_t t = 0; t <= 60000; t += 100) d.step(t);

    CHECK(live.bank.var(hi.r).valid());
    CHECK(live.bank.get(hi) == doctest::Approx(25.0f).epsilon(0.02));
    INFO("and the quorum alarm is up, because two probes are gone");
    CHECK(live.bank.var(live.ref("alarm_sns_ceiling")).as_bool());
}

TEST_CASE_FIXTURE(StRig, "zone: the instruments report percent, the model works in fractions") {

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
    CHECK(live.bank.get(live.bank.resolve<float>("cli_rh_hi")) ==
          doctest::Approx(0.68f).epsilon(0.02));

    INFO("and the psychrometrics that follow are sane: 22 C at 68% dews at "
         "about 16 C");
    CHECK(live.bank.get(live.bank.resolve<float>("cli_dewPoint_degC")) ==
          doctest::Approx(15.8f).epsilon(0.05));
    CHECK(live.bank.get(live.bank.resolve<float>("cli_dpMargin_degC")) ==
          doctest::Approx(6.2f).epsilon(0.1));
}

TEST_CASE_FIXTURE(StRig, "zone: temperature survives losing every humidity probe") {
    // The two quantities carry their own quorum. Coupling them would let one
    // dead RH probe invalidate a temperature three good probes agree on.
    Vars::Ref<float> t_out = live.bank.resolve<float>("cli_temp_hi");
    Vars::Ref<float> h_out = live.bank.resolve<float>("cli_rh_hi");


    const char* temps[3] = { "cli_temp_hi1", "cli_temp_hi2", "cli_temp_hi3" };
    const char* rhs[3]   = { "cli_rh_hi1", "cli_rh_hi2", "cli_rh_hi3" };
    for (uint8_t i = 0; i < 3; i++) {
        live.bank.var(live.ref(temps[i])).publish(from_f32(T_F32, 22.0f), 0);
        live.bank.var(live.ref(rhs[i])).set_quality(Q_NO_DATA);
    }
    for (uint64_t t = 0; t <= 60000; t += 100) d.step(t);

    CHECK(live.bank.get(t_out) == doctest::Approx(22.0f).epsilon(0.02));
    INFO("the humidity says so on its own companion, and the temperature's "
         "is untouched: two quorums, two answers");
    CHECK_FALSE(live.bank.var(live.ref("cli_rh_hi")).valid());
    CHECK(live.bank.var(live.ref("cli_temp_hi")).valid());
    (void)h_out;
}

TEST_CASE("lifecycle: no law runs outside RUN") {
    // Defence in depth at both ends. Before RUN the field has not answered and
    // every row reads its DECLARED value; after RUN the unit is on its way out.
    Live f;
    Plc::Runtime::Plant st(Botanik::SetupContext(f.reg), f.bank);
    st.wire();
    Scheduler::RunState state = Scheduler::RunState::START;
    st.set_run_state(&state);

    const Vars::VarRef out = f.ref("cli_temp_hi");
    const float before = f.bank.var(out).as_f32();
    for (uint8_t i = 1; i <= 3; i++) {
        char n[24]; snprintf(n, sizeof n, "cli_temp_hi%u", (unsigned)i);
        f.bank.var(f.ref(n)).publish(from_f32(T_F32, 45.0f), 0);
    }

    uint64_t t = 0;
    for (; t < 2000; t += 100) st.scan(t);
    INFO("START: temp_rh_sensors has not run, so the zone has published nothing");
    CHECK(f.bank.var(out).as_f32() == doctest::Approx(before));

    state = Scheduler::RunState::RUN;
    for (; t < 4000; t += 100) st.scan(t);
    INFO("RUN: it runs, seeding on the reading it starts from");
    CHECK(f.bank.var(out).as_f32() == doctest::Approx(45.0f).epsilon(0.02));

    state = Scheduler::RunState::STOP;
    for (; t < 8000; t += 100) st.scan(t);
    INFO("STOP: what it was driving DROPS to its declared safe state, rather "
         "than freezing at the last thing a law happened to decide");
    CHECK(f.bank.var(out).as_f32() == doctest::Approx(before));
    CHECK_FALSE(f.bank.var(out).valid());
}

TEST_CASE("control: taking one program out of the scan drops what it drives safe") {
    Live f;
    Plc::Runtime::Plant st(Botanik::SetupContext(f.reg), f.bank);
    st.wire();

    uint8_t skirt = 0xFF;
    for (uint8_t i = 0; i < st.count(); i++)
        if (strcmp(st.name(i), "skirt") == 0) skirt = i;
    REQUIRE(skirt != 0xFF);
    REQUIRE(st.running(skirt));

    const Vars::VarRef valve = f.ref("cli_skirtValve");
    f.bank.var(valve).set(true);
    for (uint64_t t = 0; t < 500; t += 100) st.scan(skirt, t);

    st.set_enabled(skirt, false);
    CHECK_FALSE(st.running(skirt));
    st.scan(skirt, 500);
    INFO("the coil is not FROZEN where the law left it: it reads the declared "
         "safe state, which is a value somebody chose rather than a code every "
         "reader has to interpret");
    CHECK(f.bank.var(valve).as_bool() ==
          as_bool(T_BOOL, f.bank.descriptor(valve.index).def));
    CHECK_FALSE(f.bank.var(valve).as_bool());
    INFO("...and it is OWED to the field, or the coil would stay where it was");
    CHECK(f.bank.var(valve).field_dirty());

    INFO("...and its neighbours are untouched: the gate is per program");
    for (uint8_t i = 0; i < st.count(); i++)
        if (i != skirt) CHECK(st.running(i));

    st.set_enabled(skirt, true);
    CHECK(st.running(skirt));
}

TEST_CASE("control: a parking program travels to its safe state before it is dropped") {
    Live f;
    Plc::Runtime::Plant st(Botanik::SetupContext(f.reg), f.bank);
    st.wire();

    uint8_t windows = 0xFF;
    for (uint8_t i = 0; i < st.count(); i++)
        if (strcmp(st.name(i), "windows") == 0) windows = i;
    REQUIRE(windows != 0xFF);

    const Vars::VarRef park   = f.ref("prog_windows_park");
    const Vars::VarRef parked = f.ref("prog_windows_parked");
    const Vars::VarRef opn    = f.ref("cli_window1_opn");
    REQUIRE(park.index != Vars::VAR_NONE);

    // Open, so the park has somewhere to travel FROM. The rams datum by
    // running to a stop, so this takes a while whatever it is asked for.
    force(f, "cli_windowDem", 1.0f);
    force(f, "cli_windowCap", 1.0f);
    force(f, "cli_windowLockout", false);
    uint64_t t = 0;
    for (uint32_t i = 0; i < 900; i++) st.scan(windows, t += 100);
    REQUIRE(f.bank.get(f.bank.resolve<float>("cli_window1_pos")) > 0.9f);

    st.set_enabled(windows, false);
    st.scan(windows, t += 100);
    INFO("out of the scan, but told to park rather than simply dropped - and "
         "the plant says so, which is what holds the lifecycle in STOP");
    CHECK(f.bank.var(park).as_bool());
    CHECK(st.parking());
    CHECK_FALSE(f.bank.var(parked).as_bool());

    INFO("...and the body is still being called: something has to drive the "
         "rams shut, and a program nobody scans drives nothing");
    bool moved = false;
    for (uint32_t i = 0; i < 40 && !moved; i++) {
        st.scan(windows, t += 100);
        if (f.bank.var(f.ref("cli_window1_clo")).as_bool()) moved = true;
    }
    CHECK(moved);

    for (uint32_t i = 0; i < 900 && st.parking(); i++) st.scan(windows, t += 100);
    INFO("the roof reaches SHUT - which is the point: the declared value of "
         "cli_window1_tgt is a part-open park for a house nobody can see, and "
         "arriving there without travelling would leave the roof wherever the "
         "last scan had it");
    CHECK_FALSE(st.parking());
    CHECK(f.bank.var(f.ref("cli_window1_pos")).as_f32() < 0.05f);
    INFO("...and only then do the rows go to their declared values, with the "
         "coils off and the park released");
    CHECK_FALSE(f.bank.var(park).as_bool());
    CHECK_FALSE(f.bank.var(parked).as_bool());
    CHECK_FALSE(f.bank.var(opn).as_bool());
    CHECK_FALSE(f.bank.var(f.ref("cli_window1_clo")).as_bool());
}

TEST_CASE("control: a program that never parks is dropped anyway, and says so") {
    Live f;
    Plc::Runtime::Plant st(Botanik::SetupContext(f.reg), f.bank);
    st.wire();

    uint8_t windows = 0xFF;
    for (uint8_t i = 0; i < st.count(); i++)
        if (strcmp(st.name(i), "windows") == 0) windows = i;
    REQUIRE(windows != 0xFF);

    // A ram that cannot travel: the timeout is the only way out, and it must
    // exist or a seized actuator holds the whole shutdown open.
    REQUIRE(f.bank.param_set_i(f.bank.var(f.ref("plc_parkTimeout_sec")), 2)
            == SET_OK);
    force(f, "cli_windowTravel_sec", (int64_t)600);

    uint64_t t = 0;
    st.scan(windows, t += 100);
    st.set_enabled(windows, false);
    st.scan(windows, t += 100);
    REQUIRE(st.parking());

    for (uint32_t i = 0; i < 40 && st.parking(); i++) st.scan(windows, t += 100);
    INFO("past plc_parkTimeout_sec the park is given up and the rows go safe "
         "regardless: the way out is not allowed to depend on an actuator");
    CHECK_FALSE(st.parking());
    CHECK_FALSE(f.bank.var(f.ref("prog_windows_park")).as_bool());
    CHECK_FALSE(f.bank.var(f.ref("cli_window1_opn")).as_bool());
}

TEST_CASE("control: sys_control off stops every law, whatever its own gate says") {
    Live f;
    Plc::Runtime::Plant st(Botanik::SetupContext(f.reg), f.bank);
    st.wire();
    REQUIRE(st.control_allowed());
    REQUIRE(st.running(0));

    f.bank.var(f.bank.find("sys_control")).set(false);
    CHECK_FALSE(st.control_allowed());
    INFO("the two sources are INDEPENDENT and the stricter wins, so a program "
         "whose own gate is open still does not run");
    for (uint8_t i = 0; i < st.count(); i++) {
        CHECK(st.enabled(i));
        CHECK_FALSE(st.running(i));
    }

    f.bank.var(f.bank.find("sys_control")).set(true);
    CHECK(st.running(0));
}

TEST_CASE("lifecycle: a plant with no lifecycle at all runs") {
    // A rig that constructs a Plant and nothing else is asking for the laws,
    // not for the unit's life.
    Live f;
    Plc::Runtime::Plant st(Botanik::SetupContext(f.reg), f.bank);
    st.wire();
    for (uint8_t i = 1; i <= 3; i++) {
        char n[24]; snprintf(n, sizeof n, "cli_temp_hi%u", (unsigned)i);
        f.bank.var(f.ref(n)).publish(from_f32(T_F32, 21.0f), 0);
    }
    st.scan(0);
    st.scan(100);
    CHECK(f.bank.var(f.ref("cli_temp_hi")).as_f32() == doctest::Approx(21.0f).epsilon(0.02));
}

TEST_CASE_FIXTURE(StRig, "watch: a dead ceiling does not silence the humidity alarm") {
    // One gate over the temperature AND the margin used to cover all three
    // alarms, so losing the ceiling took the humidity alarm with it - even
    // though the floor still gives a margin the model believes.
    for (uint8_t i = 1; i <= 3; i++) {
        char n[24];
        snprintf(n, sizeof n, "cli_temp_hi%u", (unsigned)i);
        live.bank.var(live.ref(n)).set_quality(Q_NO_DATA);
        snprintf(n, sizeof n, "cli_temp_lo%u", (unsigned)i);
        live.bank.var(live.ref(n)).publish(from_f32(T_F32, 18.0f), 0);
        snprintf(n, sizeof n, "cli_rh_lo%u", (unsigned)i);
        live.bank.var(live.ref(n)).publish(from_f32(T_F32, 99.0f), 0);
    }
    for (uint64_t t = 0; t <= 60000; t += 100) d.step(t);

    REQUIRE_FALSE(live.bank.var(live.ref("cli_temp_hi")).valid());
    REQUIRE(live.bank.var(live.ref("cli_dpMargin_degC")).valid());
    INFO("99% at 18 degC is on the dew point: the margin is believable and tiny");
    CHECK(live.bank.var(live.ref("alarm_too_humid")).as_bool());
    INFO("...and the temperature alarms, which have no ceiling, stay down");
    CHECK_FALSE(live.bank.var(live.ref("alarm_too_hot")).as_bool());
}

TEST_CASE_FIXTURE(StRig, "watch: stratification nobody measured is not a stratification alarm") {
    // cli_dTStrat_degC substitutes cli_dTStratLow_degC when unmeasured, so the
    // alarm was held down by the DECLARED values agreeing rather than by a gate.
    live.bank.var(live.ref("cli_dTStratHigh_degC")).publish(from_f32(T_F32, 0.5f), 0);
    for (uint8_t i = 1; i <= 3; i++) {
        char n[24];
        snprintf(n, sizeof n, "cli_temp_lo%u", (unsigned)i);
        live.bank.var(live.ref(n)).set_quality(Q_NO_DATA);
        snprintf(n, sizeof n, "cli_temp_hi%u", (unsigned)i);
        live.bank.var(live.ref(n)).publish(from_f32(T_F32, 22.0f), 0);
    }
    for (uint64_t t = 0; t <= 60000; t += 100) d.step(t);

    REQUIRE_FALSE(live.bank.var(live.ref("cli_dTStrat_degC")).valid());
    INFO("the gap reads past a 0.5 degC limit, but nothing measured the floor");
    CHECK_FALSE(live.bank.var(live.ref("alarm_strat")).as_bool());
}

TEST_CASE_FIXTURE(StRig, "zone: a probe that comes back rejoins the average, it does not snap to it") {
    // The filter is fed the SCAN CLOCK, not an elapsed time, so a block that is
    // only called while its input is believable stops updating its `last`. The
    // first sample after an outage then arrives with the WHOLE outage as dt,
    // alpha goes to nearly 1, and the average jumps to the new reading in one
    // beat - which is the opposite of what every one of these blocks says it
    // does. A dead probe coming back must not move the house four degrees.
    Vars::Ref<float> hi = live.bank.resolve<float>("cli_temp_hi");
    const VarRef p[3] = { live.ref("cli_temp_hi1"), live.ref("cli_temp_hi2"),
                          live.ref("cli_temp_hi3") };

    uint64_t t = 0;
    for (; t <= 60000; t += 100) {
        for (uint8_t i = 0; i < 3; i++)
            live.bank.var(p[i]).publish(from_f32(T_F32, 20.0f), t);
        d.step(t);
    }
    REQUIRE(live.bank.get(hi) == doctest::Approx(20.0f).epsilon(0.02));

    // held(), not get(): with no probe the zone is not believable and get()
    // is the law's read, which substitutes. What this case is about is the
    // FILTER's own state, which is the slot.
    INFO("five minutes with no probe at all");
    for (uint8_t i = 0; i < 3; i++) live.bank.var(p[i]).set_quality(Q_NO_DATA);
    for (uint32_t k = 0; k < 3000; k++) { t += 100; d.step(t); }
    CHECK(hi.held() == doctest::Approx(20.0f).epsilon(0.02));

    INFO("...and they come back reading ten degrees higher: ONE beat of that "
         "is one beat's worth, not the whole outage's");
    for (uint8_t i = 0; i < 3; i++)
        live.bank.var(p[i]).publish(from_f32(T_F32, 30.0f), t);
    t += 100;
    d.step(t);
    CHECK(live.bank.get(hi) < 21.0f);
}

TEST_CASE_FIXTURE(StRig, "zone: with every probe gone the reading is HELD, not defaulted") {
    Vars::Ref<float> hi = live.bank.resolve<float>("cli_temp_hi");
    const VarRef p[3] = { live.ref("cli_temp_hi1"), live.ref("cli_temp_hi2"),
                          live.ref("cli_temp_hi3") };


    for (uint8_t i = 0; i < 3; i++)
        live.bank.var(p[i]).publish(from_f32(T_F32, 21.0f), 0);
    for (uint64_t t = 0; t <= 20000; t += 100) d.step(t);
    REQUIRE(live.bank.var(live.ref("cli_temp_hi")).valid());

    for (uint8_t i = 0; i < 3; i++) live.bank.var(p[i]).set_quality(Q_NO_DATA);
    for (uint64_t t = 20000; t <= 40000; t += 100) d.step(t);

    INFO("temp_rh_sensors writes the zone's quality, so the row itself is what "
         "goes unbelievable - there is no second place for the doubt to live");
    CHECK_FALSE(live.bank.var(live.ref("cli_temp_hi")).valid());

    INFO("the SLOT holds the last average this zone could make, which is what "
         "an operator chasing a dead probe wants to see");
    CHECK(live.bank.var(hi.r).as_f32() == doctest::Approx(21.0f));

    INFO("...and a LAW reading it gets the declared value instead, because a "
         "held average of nothing is not a reading of the house");
    CHECK(live.bank.get(hi) ==
          doctest::Approx(as_f32(T_F32, live.bank.descriptor(hi.r.index).def)));
}

TEST_CASE_FIXTURE(StRig, "zone: a zone that has never been measured has nothing to hold") {
    for (uint64_t t = 0; t <= 2000; t += 100) d.step(t);

    INFO("holding would publish a filter that has never been seeded, which is "
         "the declared default wearing a better label");
    CHECK_FALSE(live.bank.var(live.ref("cli_temp_cs")).valid());
}

/** A level and the companion that says it was measured, which is what
 *  TempRHSensors publishes as a pair. */
static void levels(Live& f, float hi, float lo, uint64_t at = 0,
                   bool hi_ok = true, bool lo_ok = true) {
    // The PROBES: cli_temp_hi and cli_temp_lo are temp_rh_sensors' outputs
    // now, so publishing them directly is overwritten on the next scan. A
    // level "nobody measures" is one whose three probes stopped answering.
    char n[24];
    for (uint8_t i = 1; i <= 3; i++) {
        snprintf(n, sizeof n, "cli_temp_hi%u", (unsigned)i);
        if (hi_ok) f.bank.var(f.ref(n)).publish(from_f32(T_F32, hi), at);
        else       f.bank.var(f.ref(n)).set_quality(Q_NO_DATA);
        snprintf(n, sizeof n, "cli_temp_lo%u", (unsigned)i);
        if (lo_ok) f.bank.var(f.ref(n)).publish(from_f32(T_F32, lo), at);
        else       f.bank.var(f.ref(n)).set_quality(Q_NO_DATA);
    }
}

/** @brief Hold the two levels @p degrees apart until the zone filter has
 *  arrived. The gap is three programs downstream of the probes now - zone,
 *  then strat_sensor - so a case about the destratification LADDER still has
 *  to come through the sensing chain to reach it. */
template <typename R>
static void settle_gap(R& rig, uint64_t& t, float degrees) {
    for (uint32_t i = 0; i < 2000; i++) {
        levels(rig.live, 20.0f + degrees, 20.0f, t);
        rig.d.step(t += 100);
    }
}

TEST_CASE_FIXTURE(StRig, "strat: the gap is unsigned, so an inverted profile still calls the fans") {

    Vars::Ref<float> gap = live.bank.resolve<float>("cli_dTStrat_degC");

    // Long enough for the zone filter to arrive: the levels are EWMA outputs
    // now, with a 30-second time constant, so a gap is only exact after a few
    // minutes of beats rather than one.
    uint64_t t = 0;
    levels(live, 24.0f, 20.0f, t);
    for (uint32_t i = 0; i < 2000; i++) { levels(live, 24.0f, 20.0f, t); d.step(t += 100); }
    CHECK(live.bank.get(gap) == doctest::Approx(4.0f).epsilon(0.02));

    INFO("a cold roof over a warm floor is as much a case for mixing");
    for (uint32_t i = 0; i < 2000; i++) { levels(live, 18.0f, 22.0f, t); d.step(t += 100); }
    CHECK(live.bank.get(gap) == doctest::Approx(4.0f).epsilon(0.02));
}

TEST_CASE_FIXTURE(StRig, "strat: a level nothing measures makes the gap unmeasured, not zero") {

    levels(live, 24.0f, 20.0f, 0, true, false);
    d.step(100);
    INFO("the COMPANION is what says so - the row holds a number either way, "
         "and it is the one that would fabricate a four-degree gap");
    CHECK_FALSE(live.bank.var(live.ref("cli_dTStrat_degC")).valid());
    INFO("...and the gap it does publish is the low rung, so the fans keep "
         "mixing rather than reading a profile nobody saw");
    CHECK(live.bank.var(live.ref("cli_dTStrat_degC")).as_f32() ==
          doctest::Approx(live.bank.var(live.ref("cli_dTStratLow_degC")).as_f32()));
}

TEST_CASE_FIXTURE(StRig, "day phase: the banks arm off the published extremes, so one can be forced") {
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

TEST_CASE_FIXTURE(StRig, "target: a bank phase only shifts the target when yesterday armed it") {

    Vars::Ref<float> target = live.bank.resolve<float>("cli_targetTemp_degC");
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

    // The tracker has to be READY, or day_phase publishes cli_phase_qlt false
    // and target_temp applies no offset whatever the phase says.
    live.bank.var(live.ref("sol_ready")).set(true);

    // FORCED, not written: day_phase owns cli_phase and the two arms now, and
    // rewrites them every scan. An override SHADOWS the live value, so this is
    // both what survives a scan and what an operator actually does - it is how
    // a cold-bank morning gets rehearsed in July.
    INFO("COLD_BANK in force but not armed runs at the ordinary target");
    live.bank.override_set(live.bank.var(phase),
                           Vars::from_i64(Vars::T_U8, Site::PHASE_COLD_BANK));
    live.bank.override_set(live.bank.var(cold), Vars::from_bool(Vars::T_BOOL, false));
    d.step(200);
    CHECK(live.bank.get(target) == doctest::Approx(base));

    INFO("armed, it pulls the target down by the cold offset");
    live.bank.override_set(live.bank.var(cold), Vars::from_bool(Vars::T_BOOL, true));
    d.step(300);
    const float ofs = live.bank.var(live.ref("cli_coldBankOfs_degC")).as_f32();
    CHECK(live.bank.get(target) == doctest::Approx(base - ofs));

    INFO("and HEAT_BANK uses the HEAT offset, not the cold one");
    live.bank.override_set(live.bank.var(phase),
                           Vars::from_i64(Vars::T_U8, Site::PHASE_HEAT_BANK));
    live.bank.override_set(live.bank.var(heat), Vars::from_bool(Vars::T_BOOL, true));
    d.step(400);
    const float hofs = live.bank.var(live.ref("cli_heatBankOfs_degC")).as_f32();
    REQUIRE(hofs != ofs);          // else the case proves nothing
    CHECK(live.bank.get(target) == doctest::Approx(base + hofs));
}

TEST_CASE_FIXTURE(StRig, "target: an unplaced phase takes the base setpoint, and stays believable") {

    Vars::Ref<float> target = live.bank.resolve<float>("cli_targetTemp_degC");
    const VarRef phase = live.ref("cli_phase");
    const VarRef cold  = live.ref("cli_coldBankArm");
    const float base = live.bank.var(live.ref("cli_coolSet_degC")).as_f32();
    REQUIRE(base == doctest::Approx(20.0f));      // the site's safe cooling target
    live.bank.param_set(live.bank.var(live.ref("cli_targetTau_s")), 0.0f);

    live.bank.var(live.ref("sol_ready")).set(true);
    INFO("armed and in the bank phase, the offset applies");
    live.bank.override_set(live.bank.var(phase),
                           Vars::from_i64(Vars::T_U8, Site::PHASE_COLD_BANK));
    live.bank.override_set(live.bank.var(cold), Vars::from_bool(Vars::T_BOOL, true));
    d.step(100);
    const float ofs = live.bank.var(live.ref("cli_coldBankOfs_degC")).as_f32();
    REQUIRE(live.bank.get(target) == doctest::Approx(base - ofs));
    REQUIRE(live.bank.var(live.ref("cli_targetTemp_degC")).valid());

    INFO("the phase goes unplaceable - no wall clock, so no sun - and the shift "
         "goes with it: an offset from a phase nobody placed is a setpoint "
         "nobody asked for");
    // The QUALITY, because that is what target_temp reads. The phase itself
    // has to stay forced or day_phase overwrites it every scan - and a forced
    // value is believed, so saying it is unplaceable takes the second act.
    live.bank.var(live.ref("sol_ready")).set(false);
    distrust(live, "cli_phase");
    d.step(200);
    CHECK(live.bank.get(target) == doctest::Approx(base));

    INFO("and the CHAIN STOPS HERE: the base setpoint is a proper value, so "
         "the row stays believable for every cooling stage that keys off it");
    CHECK(live.bank.var(live.ref("cli_targetTemp_degC")).valid());
}

TEST_CASE_FIXTURE(StRig, "target: a phase boundary eases the target instead of stepping it") {

    Vars::Ref<float> target = live.bank.resolve<float>("cli_targetTemp_degC");
    const float base = live.bank.var(live.ref("cli_coolSet_degC")).as_f32();
    const float ofs  = live.bank.var(live.ref("cli_coldBankOfs_degC")).as_f32();

    put_phase(live, Site::PHASE_DAY);
    d.step(100);
    INFO("the first scan ADOPTS the target: easing up from a stale filter would "
         "call for heat the house does not need");
    REQUIRE(live.bank.get(target) == doctest::Approx(base));

    put_phase(live, Site::PHASE_COLD_BANK);
    live.bank.var(live.ref("cli_coldBankArm")).set(true);
    d.step(1100);                       // a second into the new phase
    const float moved = live.bank.get(target);
    CHECK(moved < base);                                  // it has begun
    CHECK(moved > base - ofs * 0.05f);                    // ...by a sliver of it

    INFO("and arrives, given several time constants of the default 300 s");
    for (uint32_t t = 2100; t <= 3000000u; t += 1000) d.step(t);
    CHECK(live.bank.get(target) == doctest::Approx(base - ofs).epsilon(0.001));
}

TEST_CASE_FIXTURE(StRig, "target: an aggressive offset is clamped to the safe band") {

    live.bank.param_set(live.bank.var(live.ref("cli_coldBankOfs_degC")), 15.0f);
    put_phase(live, Site::PHASE_COLD_BANK);
    live.bank.var(live.ref("cli_coldBankArm")).set(true);
    d.step(100);

    Vars::Ref<float> target = live.bank.resolve<float>("cli_targetTemp_degC");
    Vars::Ref<float> floor_ = live.bank.resolve<float>("cli_targetMin_degC");
    CHECK(live.bank.get(target) == doctest::Approx(live.bank.get(floor_)));
}

TEST_CASE_FIXTURE(StRig, "cooling: a demand it cannot measure is still made, and marked") {

    const VarRef dem = live.ref("cli_coolDem");
    field(live, "cli_targetTemp_degC", 20.0f);
    interior(live, 30.0f, 30.0f);
    force(live, "cli_temp_cs", 18.0f);
    for (uint32_t i = 0; i < 20; i++) d.step(100u * (i + 1));
    const float was = live.bank.var(dem).as_f32();
    REQUIRE(was > 0.0f);

    distrust(live, "cli_temp_in");
    d.step(3000);

    INFO("the verdict travels: the law hands its own output the quality of the "
         "reading it could not get, so every reader downstream learns it in "
         "one read rather than by asking about the house");
    CHECK_FALSE(live.bank.var(live.ref("cli_coolDem")).valid());

    INFO("the ladder keeps running - there is still a demand for an actuator "
         "to fall back from - but on the house the row DECLARES, which is a "
         "cold one, so it stops asking for cooling");
    CHECK(live.bank.var(dem).as_f32() < was);
    CHECK(live.bank.var(dem).as_f32() <= 0.0f);

    INFO("...and the integral goes with it: effort wound up against a house "
         "nobody can see is a claim about a past this law no longer has");
    CHECK(live.bank.var(dem).as_f32() != doctest::Approx(was));
}

TEST_CASE_FIXTURE(StRig, "cooling: the floor carries it when the ceiling goes, and both gone is unknown") {

    // What the house temperature is MADE of, so it comes through the probes:
    // temp_rh_zone is on this beat and would put its own answer over a forced
    // one. The setpoint belongs to another law on the same beat, and is forced
    // because this case is not about it.
    const VarRef dem = live.ref("cli_coolDem");
    force(live, "cli_targetTemp_degC", 20.0f);
    force(live, "cli_temp_cs", 18.0f);

    uint64_t t = 0;
    levels(live, 30.0f, 30.0f, t);
    d.step(t += 100);
    REQUIRE(live.bank.var(dem).valid());
    REQUIRE(live.bank.var(dem).as_f32() > 0.0f);          // hot: cooling
    for (uint8_t i = 0; i < Plc::Runtime::PROGRAM_COUNT; i++)
        CHECK(st.health(i) == Plc::Health::HEALTHY);

    INFO("the ceiling goes: temp_rh_zone still has a house temperature to "
         "give, it is the floor's, and this law is still on a measured one");
    for (uint32_t i = 0; i < 5; i++) {
        levels(live, 30.0f, 30.0f, t, false, true);
        d.step(t += 100);
    }
    CHECK(live.bank.var(live.ref("cli_temp_in")).valid());
    CHECK(live.bank.var(dem).valid());
    CHECK(live.bank.var(dem).as_f32() > 0.0f);

    INFO("the floor goes too, and nothing measures the house: a PID left "
         "trusting a held reading would still be reporting on a greenhouse");
    for (uint32_t i = 0; i < 5; i++) {
        levels(live, 30.0f, 30.0f, t, false, false);
        d.step(t += 100);
    }
    CHECK_FALSE(live.bank.var(live.ref("cli_temp_in")).valid());
    CHECK_FALSE(live.bank.var(live.ref("cli_coolDem")).valid());
}

TEST_CASE("cooling: an unknown demand puts every actuator at its own safe state") {
    Live f;
    fine_weather(f);
    interlocks_clear(f);
    // Every one of the five actuators is ST now, so the RESOURCE is the whole
    // beat.
    Plc::Runtime::Plant st(Botanik::SetupContext(f.reg), f.bank);
    st.wire();
    struct { Plc::Runtime::Plant* st;
             void step(uint64_t n) { st->scan(n); } } d{ &st };

    // Everything else says GO: only the cooling ladder cannot see the house,
    // and its two outputs go together. Reached the way the plant reaches it -
    // the ladder is ST now, so it publishes every scan and there is no
    // invalidating its outputs from here. What goes is the INTERIOR, and the
    // ladder's own answer to that is what this case is about.
    distrust(f, "cli_temp_in");
    f.bank.var(f.ref("cli_stratDem")).set(0.0f);
    f.bank.var(f.ref("cli_humidDem")).set(0.0f);
    f.bank.var(f.ref("cli_rotVentDem")).set(0.0f);
    // temp_rh_zone owns the margin now, and the humidity law behind these
    // actuators reads it: forced, and forced into the ladder's DEAD ZONE.
    // 20 degC is past cli_humidStart_degC, which asks the skirt to humidify -
    // a demand of its own, and this case is about the ones that are missing.
    force(f, "cli_dpMargin_degC", 10.0f);
    field(f, "cli_temp_hi", 25.0f);
    // The closing half of the day: the screen's safe state is DEPLOYED, and
    // deploying is only ever permitted while the day is shutting down.
    put_phase(f, Site::PHASE_DUSK_BREEZE);
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
    CHECK(f.bank.get(f.bank.resolve<float>("cli_window1_tgt")) ==
          doctest::Approx(safe_open(f)));                         // part open
    CHECK(f.bank.get(f.bank.resolve<float>("cli_intr1")) ==
          doctest::Approx(0.15f));                                // some air
    CHECK(f.bank.get(f.bank.resolve<bool>("cli_screen")));     // deployed
    CHECK_FALSE(f.bank.get(f.bank.resolve<bool>("cli_skirtValve")));  // shut
    CHECK(f.bank.get(f.bank.resolve<bool>("cli_destrat1_speed1")));   // speed 1

    // The five that READ a missing demand, not the ST behind them: every
    // program on the resource has every input it wants. Health is its own
    // again, per program, exactly as it was when these were C++.
    for (uint8_t i = 0; i < Plc::Runtime::PROGRAM_COUNT; i++) {
        INFO("ST program ", (unsigned)i);
        CHECK(st.health(i) == Plc::Health::HEALTHY);
    }
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

TEST_CASE_FIXTURE(StRig, "destrat: the ladder is three rungs, and coming down costs the hysteresis") {

    const VarRef dem = live.ref("cli_stratDem");
    uint64_t t = 0;

    settle_gap(*this, t, 8.0f);
    INFO("the demand IS the fans' own top rung: any other number would need "
         "translating twice, and the two ladders would drift apart");
    CHECK(live.bank.var(dem).as_f32() ==
          doctest::Approx(live.bank.var(live.ref("cli_fanHighDem")).as_f32()));

    // EITHER SIDE of a rung's edge, never ON it: the gap arrives through an
    // EWMA now, which approaches its target asymptotically. Settling from 8.0
    // to "exactly 2.0" lands on 2.007, and a rung releasing at `<= 2.0` then
    // holds - which is the filter being correct, not the ladder being wrong.
    INFO("a gap under the middle rung's release edge gives the speed back");
    settle_gap(*this, t, 1.5f);
    CHECK(live.bank.var(dem).as_f32() ==
          doctest::Approx(live.bank.var(live.ref("cli_fanLowDem")).as_f32()));

    INFO("and coming back down costs the hysteresis: the rungs are contiguous "
         "by default, since a speed's release edge is the one below it");
    settle_gap(*this, t, 1.0f);
    CHECK(live.bank.var(dem).as_f32() ==
          doctest::Approx(live.bank.var(live.ref("cli_fanLowDem")).as_f32()));
    // ...and a well-mixed house STOPS them. The release edge is
    // cli_dTStratLow_degC minus cli_dTStratHyst_degC, and the hysteresis is
    // half the rung spacing rather than all of it for exactly this reason: at
    // 2.0 the edge was a gap of 0.0, which an ABSOLUTE difference never
    // reaches, and the fans held speed 1 for the life of the run.
    settle_gap(*this, t, 0.05f);
    INFO("a gap under the low rung's release edge stops them");
    CHECK(live.bank.var(dem).as_f32() == doctest::Approx(0.0f));

    INFO("...and it takes the whole hysteresis to get there: still running "
         "just above the edge");
    settle_gap(*this, t, 8.0f);
    settle_gap(*this, t,
               live.bank.var(live.ref("cli_dTStratLow_degC")).as_f32()
             - live.bank.var(live.ref("cli_dTStratHyst_degC")).as_f32() + 0.2f);
    CHECK(live.bank.var(dem).as_f32() ==
          doctest::Approx(live.bank.var(live.ref("cli_fanLowDem")).as_f32()));
}

TEST_CASE_FIXTURE(StRig, "destrat: a level nothing measures keeps the fans mixing") {

    // strat_sensor publishes the LOW RUNG when it cannot see a profile, so the
    // ladder engages speed 1 rather than reading zero and stopping. That
    // substitution is written out in the ST: an ST-written row is always
    // valid, so it cannot come from the row's declared value any more.
    uint64_t t = 0;
    settle_gap(*this, t, -0.1f);
    REQUIRE(live.bank.var(live.ref("cli_stratDem")).as_f32() ==
            doctest::Approx(0.0f));

    levels(live, 24.0f, 20.0f, t, false, false);
    for (uint32_t i = 0; i < 50; i++) d.step(t += 100);

    CHECK_FALSE(live.bank.var(live.ref("cli_dTStrat_degC")).valid());
    CHECK(live.bank.var(live.ref("cli_stratDem")).as_f32() ==
          doctest::Approx(live.bank.var(live.ref("cli_fanLowDem")).as_f32()));
}

TEST_CASE_FIXTURE(StRig, "humidity, rot guard: the ventilation demand rises as the margin falls") {

    Vars::Ref<float> dem = live.bank.resolve<float>("cli_rotVentDem");
    const float limit = live.bank.var(live.ref("cli_rotMargin_degC")).as_f32();

    force(live, "cli_dpMargin_degC", limit + 2.0f);
    d.step(100);
    CHECK(live.bank.get(dem) == doctest::Approx(0.0f));

    INFO("at the limit itself the ramp has not started");
    force(live, "cli_dpMargin_degC", limit);
    d.step(200);
    CHECK(live.bank.get(dem) == doctest::Approx(0.0f));

    INFO("and it is fully on at the dew point itself, not before");
    force(live, "cli_dpMargin_degC", 0.5f * limit);
    d.step(300);
    const float half = live.bank.get(dem);
    CHECK(half > 0.0f);
    CHECK(half < 1.0f);
    force(live, "cli_dpMargin_degC", 0.0f);
    d.step(400);
    CHECK(live.bank.get(dem) == doctest::Approx(1.0f));
}

TEST_CASE_FIXTURE(StRig, "humidity, rot guard: the lockout outlives the condition, and boots clear") {

    Vars::Ref<bool> lock = live.bank.resolve<bool>("cli_rotLockout");
    // alarm_rot is temp_rh_zone's verdict on the margin now, so the case
    // reaches the condition through the margin rather than around it.
    force(live, "cli_dpMargin_degC", 20.0f);

    INFO("dry air at boot is not locked out: the guard has nothing against it");
    d.step(100);
    CHECK_FALSE(live.bank.get(lock));

    force(live, "cli_dpMargin_degC", 0.0f);
    d.step(200);
    REQUIRE(live.bank.var(live.ref("alarm_rot")).as_bool());
    CHECK(live.bank.get(lock));

    INFO("the condition clearing does not release it");
    force(live, "cli_dpMargin_degC", 20.0f);
    d.step(300);
    CHECK(live.bank.get(lock));

    uint64_t t = 300;
    for (uint32_t i = 0; i < 3000; i++) d.step(t += 100);
    CHECK_FALSE(live.bank.get(lock));
}

TEST_CASE_FIXTURE(StRig, "humidity, rot guard: an unmeasured margin ventilates rather than assuming dry air") {

    // The COMPANION: temp_rh_zone writes the margin every scan, so the row is
    // always valid and it is the companion that says nothing measured it.
    distrust(live, "cli_dpMargin_degC");
    d.step(100);
    CHECK(live.bank.get(live.bank.resolve<float>("cli_rotVentDem")) == doctest::Approx(1.0f));
    CHECK(live.bank.get(live.bank.resolve<bool>("cli_rotLockout")));
}

TEST_CASE_FIXTURE(StRig,
                  "weather: a storm is 75 kmph of sustained wind, not a stiff breeze") {
    // It used to be read off the roof's own lockouts, so a passing shower or a
    // gust over the vent-closing threshold put a storm on the ribbon. Those are
    // reasons to shut the roof, which is a much lower bar than the weather
    // being severe.
    Vars::Ref<bool> storm = live.bank.resolve<bool>("alarm_weather_storm");
    const float shut = live.bank.var(live.ref("cli_windShutEnd_mps")).as_f32();
    uint64_t t = 0;

    force(live, "cli_rain_mmph", 0.0f);
    force(live, "cli_wind_mps", shut - 6.0f);      // a gale, and not a storm
    force(live, "cli_windGust_mps", shut + 4.0f);  // gusting past it, even
    for (uint32_t i = 0; i < 20; i++) d.step(t += 1000);
    CHECK_FALSE(live.bank.get(storm));

    INFO("rain alone is not one either");
    force(live, "cli_rain_mmph", 8.0f);
    for (uint32_t i = 0; i < 20; i++) d.step(t += 1000);
    CHECK_FALSE(live.bank.get(storm));

    INFO("...and the MEAN reaching it is");
    force(live, "cli_wind_mps", shut + 0.5f);
    for (uint32_t i = 0; i < 20; i++) d.step(t += 1000);
    CHECK(live.bank.get(storm));
}

TEST_CASE_FIXTURE(StRig,
                  "grid: the meter's readings are mirrored, and judged") {
    // Every one of these comes out of AC_SUPPLY, which the program drives once
    // per beat. A law that never called it published a supply that was always
    // in spec and never faulted, on any wiring at all.
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

TEST_CASE_FIXTURE(StRig,
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

TEST_CASE_FIXTURE(StRig, "intake: a gate holds while the two temperatures converge") {

    Vars::Ref<bool> gate = live.bank.resolve<bool>("cli_intakeCoolOk");
    const float margin = live.bank.var(live.ref("cli_ventTempMgn_degC")).as_f32();
    const float hyst   = live.bank.var(live.ref("cli_ventHyst_degC")).as_f32();

    field(live, "cli_targetTemp_degC", 20.0f);
    interior(live, 25.0f, 25.0f);
    force(live, "cli_temp_cs", 25.0f - margin - 1.0f);
    d.step(100);
    REQUIRE(live.bank.get(gate));

    INFO("the crawlspace warms to within the margin - the difference the gate "
         "is made of goes to zero as the two spaces converge - and the gate "
         "holds rather than flipping beat to beat");
    force(live, "cli_temp_cs", 25.0f - margin + hyst * 0.5f);
    for (uint32_t i = 0; i < 20; i++) d.step(200 + 100u * i);
    CHECK(live.bank.get(gate));

    INFO("...and it shuts once the difference is a whole band under the margin");
    force(live, "cli_temp_cs", 25.0f - margin + hyst + 0.1f);
    d.step(3000);
    CHECK_FALSE(live.bank.get(gate));
}

}  // namespace site_programs
