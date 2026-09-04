/**
 * @file test_site_actuators.cpp
 * @brief The actuator controllers: what reaches a coil, and where it goes when control is withdrawn.
 *
 * Driven through the real scan cycle against the site's real tables.
 * The fixtures are program_rig.h; the cycle itself is test_scan.cpp.
 */

#include "program_rig.h"

namespace site_programs {

TEST_CASE("scan: every ST program is its own task, at the roster's period") {
    // The RESOURCE would dispatch them as one fixed sequence. We call each
    // body ourselves instead, which is what keeps a program a program: its own
    // period, its own entry in the TASKS table, its own health.
    TestScan::Live live;
    Plc::Runtime::Plant st(Botanik::SetupContext(live.reg), live.bank);
    st.wire();
    st.register_tasks();

    for (uint8_t i = 0; i < Plc::Runtime::PROGRAM_COUNT; i++) {
        const Plc::Runtime::Program& p = Plc::Runtime::programs[i];
        const Tasks::Task* task = nullptr;
        for (uint8_t j = 0; j < live.reg.count(); j++)
            if (strcmp(live.reg.task(j).name, p.name) == 0) task = &live.reg.task(j);
        INFO("program ", p.name);
        REQUIRE(task != nullptr);
        CHECK(task->period_ms == p.period_ms);
        CHECK(task->phase == Tasks::PHASE_CONTROL);
        INFO("...and it bound every name it asked for");
        CHECK(st.health(i) == Plc::Health::HEALTHY);
    }
}

TEST_CASE("scan: an ST program publishes its own health row") {
    // The prog_<name> rows came back with per-program dispatch. They are what
    // an operator reads to see that a law is running, and the roster's name is
    // what ties one to the other.
    TestScan::Live live;
    Plc::Runtime::Plant st(Botanik::SetupContext(live.reg), live.bank);
    st.wire();

    char row[40];
    for (uint8_t i = 0; i < Plc::Runtime::PROGRAM_COUNT; i++) {
        snprintf(row, sizeof row, "prog_%s", Plc::Runtime::programs[i].name);
        const uint16_t r = live.ref(row);
        INFO("row ", row);
        REQUIRE(r != Vars::VAR_NONE);
        INFO("...and it says HEALTHY rather than the INHIBITED it declares");
        CHECK(live.bank.var(r).value().as<uint32_t>() ==
              (uint32_t)Plc::Health::HEALTHY);
    }
}

TEST_CASE_FIXTURE(StRig, "scan: an ST timer measures VIRTUAL time, whatever the beat size") {
    // The property the clock buys over a threaded dt: a block differences
    // sys_now_ms against its OWN last reading, so it is rate-independent. The
    // simulator accelerates by advancing the virtual clock faster in real
    // time, never by changing the step, so this is also what makes a
    // greenhouse day in a minute give the same answers as a greenhouse day.
    Vars::Ref<bool> lock = live.bank.resolve<bool>("cli_rainLockout");
    const uint32_t hold_ms =
        1000u * live.bank.var(live.ref("cli_rainLockout_sec")).value().as<uint32_t>();

    // Released between the last beat before the overrun and the first after
    // it: a step can only ever land on one side or the other.
    uint64_t t = 0;
    for (uint32_t step : { 100u, 700u }) {
        // FORCED: wx_sensors owns these three, and with no station answering
        // it publishes the companions FALSE - which is the roof going blind,
        // not the shower this case is about.
        force(live, "cli_rain_mmph", 0.4f);
        force(live, "cli_wind_mps", 0.0f);
        d.step(t += step);
        REQUIRE(live.bank.get(lock));

        force(live, "cli_rain_mmph", 0.0f);
        const uint64_t stopped = t;
        while (live.bank.get(lock) && t - stopped < 2 * hold_ms) d.step(t += step);

        const uint64_t elapsed = t - stopped;
        INFO("released after ", (unsigned)elapsed, " ms with a ", step, " ms beat");
        CHECK(elapsed >= hold_ms);
        CHECK(elapsed < hold_ms + step);
    }
}

TEST_CASE_FIXTURE(StRig, "fans: an unknown gap floors them at their safe state, not at rest") {
    interlocks_clear(live);

    Vars::Ref<bool> s1 = live.bank.resolve<bool>("cli_destrat1_speed1");
    Vars::Ref<bool> s3 = live.bank.resolve<bool>("cli_destrat1_speed3");

    // The PROBES: every row between them and the fans is an ST output now, so
    // an "unknown gap" is the levels nobody is measuring. strat_sensor then
    // publishes the low rung and the ladder engages speed 1.
    for (uint8_t i = 1; i <= 3; i++) {
        char n[24];
        snprintf(n, sizeof n, "cli_temp_hi%u", (unsigned)i);
        live.bank.var(live.ref(n)).set_quality(Q_NO_DATA);
        snprintf(n, sizeof n, "cli_temp_lo%u", (unsigned)i);
        live.bank.var(live.ref(n)).set_quality(Q_NO_DATA);
    }
    force(live, "cli_coolDem", 0.0f);
    d.step(100);

    INFO("still air is itself a rot risk, so an unknown gap is speed 1");
    CHECK(live.bank.get(s1));

    INFO("...and it is a FLOOR: a cooling demand above it still takes them up");
    force(live, "cli_coolDem", 1.0f);
    d.step(200);
    CHECK_FALSE(live.bank.get(s1));
    CHECK(live.bank.get(s3));
}

TEST_CASE_FIXTURE(StRig, "screen: a cold night deploys it, and the traverse is published while it runs") {

    Vars::Ref<bool> screen = live.bank.resolve<bool>("cli_screen");
    Vars::Ref<bool> moving = live.bank.resolve<bool>("cli_screenMoving");

    put_phase(live, Site::PHASE_DAY);
    live.bank.var(live.ref("cli_temp_hi")).publish(from_f32(T_F32, 25.0f), 0);
    force(live, "cli_coolDem", 0.5f);
    d.step(100);
    CHECK_FALSE(live.bank.get(screen));

    INFO("a cold night deploys it, and it says so while it goes. The COLD is "
         "what asks - a demand under cli_thermalScreenDem - and the night is "
         "only what permits it");
    put_phase(live, Site::PHASE_NIGHT);
    force(live, "cli_coolDem",
          live.bank.var(live.ref("cli_thermalScreenDem")).as_f32() - 0.1f);
    d.step(200);
    CHECK(live.bank.get(screen));
    CHECK(live.bank.get(moving));

    INFO("...and stops saying so once the traverse has run");
    const uint32_t travel = live.bank.var(live.ref("cli_screenTravel_sec")).value().as<uint32_t>();
    uint64_t t = 200;
    for (uint32_t i = 0; i < 10 * travel + 5; i++) d.step(t += 100);
    CHECK(live.bank.get(screen));
    CHECK_FALSE(live.bank.get(moving));
}

/** @brief The roof is entirely ST now - the lockout and the rams both - so a
 *  case that used to hold its own drive just steps the RESOURCE. */
struct RoofDrive {
    Plc::Runtime::Plant st;
    explicit RoofDrive(Live& f) : st(Botanik::SetupContext(f.reg), f.bank) {
        st.wire();
    }
    void step(uint64_t now_ms) { st.scan(now_ms); }
};

TEST_CASE("windows: the wind takes the roof down a ramp before it shuts it") {
    // Three parameters and a straight line between two of them: below
    // cli_windShutBegin_mps the wind has no say, at it the roof may open
    // cli_windShutMax, and that ceiling falls to nothing by cli_windShutEnd_mps -
    // which is also the wind that IS a storm. A cliff from wide open to shut
    // would slam four rams at whatever moment a gust crossed one number.
    Live f;
    fine_weather(f);
    interlocks_clear(f);
    RoofDrive d(f);

    const float ease = f.bank.var(f.ref("cli_windShutBegin_mps")).as_f32();
    const float shut = f.bank.var(f.ref("cli_windShutEnd_mps")).as_f32();
    const float top  = f.bank.var(f.ref("cli_windShutMax")).as_f32();
    REQUIRE(shut > ease);

    Vars::Ref<float> cap = f.bank.resolve<float>("cli_windowCap");
    force(f, "cli_windowDem", 1.0f);
    uint64_t t = 0;

    INFO("under the ease threshold the wind has no opinion at all");
    force(f, "cli_wind_mps", ease - 2.0f);
    force(f, "cli_windGust_mps", ease - 2.0f);
    for (uint32_t i = 0; i < 40; i++) d.step(t += 1000);
    CHECK(f.bank.get(cap) == doctest::Approx(1.0f));
    CHECK(f.bank.var(f.ref("cli_window1_pos")).as_f32() > 0.9f);

    INFO("at it, the roof may open cli_windShutMax and no more");
    force(f, "cli_wind_mps", ease);
    force(f, "cli_windGust_mps", ease);
    for (uint32_t i = 0; i < 60; i++) d.step(t += 1000);
    CHECK(f.bank.get(cap) == doctest::Approx(top).epsilon(0.02));
    CHECK(f.bank.var(f.ref("cli_window1_pos")).as_f32() <= doctest::Approx(top).epsilon(0.1));

    INFO("half way up the band, half the ceiling");
    force(f, "cli_wind_mps", (ease + shut) / 2.0f);
    force(f, "cli_windGust_mps", (ease + shut) / 2.0f);
    for (uint32_t i = 0; i < 60; i++) d.step(t += 1000);
    CHECK(f.bank.get(cap) == doctest::Approx(top / 2.0f).epsilon(0.05));

    INFO("and at cli_windShutEnd_mps the roof is shut, not nearly shut");
    force(f, "cli_wind_mps", shut);
    force(f, "cli_windGust_mps", shut);
    for (uint32_t i = 0; i < 80; i++) d.step(t += 1000);
    CHECK(f.bank.var(f.ref("cli_windLockout")).as_bool());
    CHECK(f.bank.get(cap) == doctest::Approx(0.0f));
    CHECK(f.bank.var(f.ref("cli_window1_pos")).as_f32() < 0.05f);
}

TEST_CASE("windows: rain shuts the roof whatever the wind is doing") {
    Live f;
    fine_weather(f);
    interlocks_clear(f);
    RoofDrive d(f);

    Vars::Ref<float> cap = f.bank.resolve<float>("cli_windowCap");
    force(f, "cli_windowDem", 1.0f);
    uint64_t t = 0;
    for (uint32_t i = 0; i < 40; i++) d.step(t += 1000);
    REQUIRE(f.bank.get(cap) == doctest::Approx(1.0f));

    INFO("a still day, and rain: shut, and it outlives the shower by "
         "cli_rainLockout_sec");
    force(f, "cli_rain_mmph", 4.0f);
    for (uint32_t i = 0; i < 40; i++) d.step(t += 1000);
    CHECK(f.bank.var(f.ref("cli_rainLockout")).as_bool());
    CHECK(f.bank.get(cap) == doctest::Approx(0.0f));
    CHECK(f.bank.var(f.ref("cli_window1_pos")).as_f32() < 0.05f);
}

TEST_CASE("windows: a forced lockout shuts the roof, with no weather to do it") {
    // The point of publishing a verdict and then reading it back: every stage
    // of the arbitration is a row, so it can be watched on a page and held by
    // hand. A law that obeyed its own local variable would ignore the force
    // and leave the row saying something that was not happening.
    Live f;
    fine_weather(f);
    interlocks_clear(f);
    RoofDrive d(f);

    force(f, "cli_windowDem", 1.0f);
    uint64_t t = 0;
    for (uint32_t i = 0; i < 40; i++) d.step(t += 1000);
    REQUIRE(f.bank.var(f.ref("cli_window1_pos")).as_f32() > 0.9f);
    REQUIRE_FALSE(f.bank.var(f.ref("cli_windowLockout")).as_bool());

    INFO("the rain lockout forced by hand: the roof shuts on the ROW, and the "
         "verdict beside it says the roof is being held");
    REQUIRE(f.bank.override_set(f.bank.var(f.ref("cli_rainLockout")), "1")
            == Vars::SET_OK);
    for (uint32_t i = 0; i < 60; i++) d.step(t += 1000);
    CHECK(f.bank.var(f.ref("cli_windowLockout")).as_bool());
    CHECK(f.bank.var(f.ref("cli_window1_pos")).as_f32() < 0.1f);

    INFO("...and releasing it gives the roof back to the ladder");
    f.bank.override_clear(f.bank.var(f.ref("cli_rainLockout")));
    for (uint32_t i = 0; i < 60; i++) d.step(t += 1000);
    CHECK_FALSE(f.bank.var(f.ref("cli_windowLockout")).as_bool());
    CHECK(f.bank.var(f.ref("cli_window1_pos")).as_f32() > 0.9f);
}

TEST_CASE_FIXTURE(StRig, "screen: the phase gives permission, the demand only asks") {

    Vars::Ref<bool> screen = live.bank.resolve<bool>("cli_screen");
    const float deploy = live.bank.var(live.ref("cli_thermalScreenDem")).as_f32();

    // The morning opens it: retracting is what the day half of the cycle is
    // allowed to do, and a warm demand asks for it.
    put_phase(live, Site::PHASE_DAWN_BREEZE);
    live.bank.var(live.ref("cli_temp_hi")).publish(from_f32(T_F32, 18.0f), 0);
    force(live, "cli_coolDem", 0.5f);
    d.step(100);
    REQUIRE_FALSE(live.bank.get(screen));

    INFO("a cold hour at MIDDAY does not close the fabric against the sun, "
         "however negative the demand goes");
    put_phase(live, Site::PHASE_DAY);
    live.bank.var(live.ref("cli_temp_hi")).publish(from_f32(T_F32, 8.0f), 100);
    force(live, "cli_coolDem", deploy - 0.5f);
    d.step(200);
    CHECK_FALSE(live.bank.get(screen));

    INFO("the same demand at dusk deploys it: the day's direction is what "
         "decides, and dusk is closing down");
    put_phase(live, Site::PHASE_DUSK_BREEZE);
    d.step(300);
    CHECK(live.bank.get(screen));

    INFO("and a mild NIGHT does not open it again - retracting belongs to the "
         "half of the day that opens up, so the heat it holds is kept");
    put_phase(live, Site::PHASE_NIGHT);
    live.bank.var(live.ref("cli_temp_hi")).publish(from_f32(T_F32, 22.0f), 300);
    force(live, "cli_coolDem", 0.5f);
    d.step(400);
    CHECK(live.bank.get(screen));

    INFO("the morning is what retracts it");
    put_phase(live, Site::PHASE_DAWN_BREEZE);
    d.step(500);
    CHECK_FALSE(live.bank.get(screen));
}

TEST_CASE_FIXTURE(StRig, "windows: exactly one ram is energised at a time") {
    fine_weather(live);
    interlocks_clear(live);

    force(live, "cli_windowDem", 1.0f);

    uint64_t t = 0;
    for (uint32_t i = 0; i < 800; i++) {
        d.step(t += 100);
        uint8_t energised = 0;
        for (uint8_t w = 1; w <= 4; w++) {
            char opn[24], clo[24];
            snprintf(opn, sizeof(opn), "cli_window%u_opn", (unsigned)w);
            snprintf(clo, sizeof(clo), "cli_window%u_clo", (unsigned)w);
            const bool o = live.bank.var(live.ref(opn)).as_bool();
            const bool c = live.bank.var(live.ref(clo)).as_bool();
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

TEST_CASE_FIXTURE(StRig, "windows: rain shuts them, and the lockout outlives the shower") {
    fine_weather(live);
    interlocks_clear(live);

    Vars::Ref<float> tgt = live.bank.resolve<float>("cli_window1_tgt");
    Vars::Ref<bool>  rain_lock = live.bank.resolve<bool>("cli_rainLockout");
    force(live, "cli_windowDem", 1.0f);

    uint64_t t = 0;
    for (uint32_t i = 0; i < 800; i++) d.step(t += 100);
    REQUIRE(live.bank.get(tgt) == doctest::Approx(1.0f));

    force(live, "cli_rain_mmph", 0.4f);
    d.step(t += 100);
    CHECK(live.bank.get(rain_lock));
    CHECK(live.bank.get(tgt) == doctest::Approx(0.0f));

    INFO("the shower stopping does not re-open them");
    force(live, "cli_rain_mmph", 0.0f);
    d.step(t += 100);
    CHECK(live.bank.get(rain_lock));

    const uint32_t hold = live.bank.var(live.ref("cli_rainLockout_sec")).value().as<uint32_t>();
    for (uint32_t i = 0; i < 10 * hold + 20; i++) d.step(t += 100);
    CHECK_FALSE(live.bank.get(rain_lock));
    CHECK(live.bank.get(tgt) == doctest::Approx(1.0f));
}

TEST_CASE_FIXTURE(StRig, "windows: a gust shuts them, and the mean alone does not") {
    fine_weather(live);
    interlocks_clear(live);

    Vars::Ref<bool> wind_lock = live.bank.resolve<bool>("cli_windLockout");
    const float shut = live.bank.var(live.ref("cli_windShutEnd_mps")).as_f32();
    force(live, "cli_windowDem", 1.0f);

    force(live, "cli_wind_mps", shut - 1.0f);
    d.step(100);
    CHECK_FALSE(live.bank.get(wind_lock));

    INFO("the PEAK is what tears a window off its ram, not the average");
    force(live, "cli_windGust_mps", shut + 1.0f);
    // Two beats: roof_lockout decides on the first and the windows obey on the
    // next, because the two are separate tasks and the registry staggers them.
    d.step(200);
    d.step(300);
    CHECK(live.bank.get(wind_lock));
    CHECK(live.bank.get(live.bank.resolve<float>("cli_window1_tgt")) ==
          doctest::Approx(0.0f));
}

TEST_CASE_FIXTURE(StRig, "windows: an unmeasured sky shuts the roof rather than reading fine") {

    fine_weather(live);
    interlocks_clear(live);
    force(live, "cli_windowDem", 1.0f);
    d.step(100);
    REQUIRE_FALSE(live.bank.get(live.bank.resolve<bool>("cli_windowLockout")));

    INFO("the SAME readings, no longer believed: both rows still say calm and "
         "dry, which is the trap - only the companions separate a fine sky "
         "from one nobody is watching");
    // FORCED false: wx_sensors owns both companions and republishes them
    // every scan, so a case rehearsing a blind sky has to shadow them.
    distrust(live, "cli_rain_mmph");
    distrust(live, "cli_wind_mps");
    d.step(200);
    CHECK(live.bank.get(live.bank.resolve<bool>("cli_rainLockout")));
    CHECK(live.bank.get(live.bank.resolve<bool>("cli_windLockout")));
    // The program that LOOKS at the sky is the one that cannot see it. The
    // windows read a lockout row, which is as readable as ever.
}

TEST_CASE_FIXTURE(StRig, "intractors: a start and a full set alike is full speed, not a divide") {
    // Both are an operator's, and the ramp between them used to be written out
    // by hand: set them to the same number and the divide produced a NaN that
    // went straight onto both fan speed rows, past the safe-state floor.
    Vars::Ref<float> intr = live.bank.resolve<float>("cli_intr1");
    Vars::Var start = live.bank.var(live.ref("cli_intakeStartDem"));
    Vars::Var full  = live.bank.var(live.ref("cli_intakeFullDem"));

    // Legal: the band holds start AT or under full, and equal is at.
    REQUIRE(live.bank.param_set(full, 0.5f) == Vars::SET_OK);
    REQUIRE(live.bank.param_set(start, 0.5f) == Vars::SET_OK);

    force(live, "cli_intakeCoolOk", true);
    force(live, "cli_coolDem", 0.7f);
    d.step(100);
    const float v = live.bank.get(intr);
    INFO("a degenerate ramp reads " << v);
    CHECK(v == v);                            // not NaN
    CHECK(v == doctest::Approx(1.0f));        // and fails toward ACTING

    INFO("under the pair it is back at the safe floor, still a number");
    force(live, "cli_coolDem", 0.2f);
    d.step(200);
    CHECK(live.bank.get(intr) == doctest::Approx(0.15f));
}

TEST_CASE_FIXTURE(StRig, "intractors: the fusion is a maximum of the demands whose gate is open") {

    Vars::Ref<float> intr = live.bank.resolve<float>("cli_intr1");
    const float start = live.bank.var(live.ref("cli_intakeStartDem")).as_f32();

    INFO("under the staging threshold they are at their FLOOR, not stopped: "
         "the row declares the safe state and some air is always wanted");
    force(live, "cli_intakeCoolOk", true);
    force(live, "cli_coolDem", start - 0.1f);
    d.step(100);
    CHECK(live.bank.get(intr) == doctest::Approx(0.15f));

    INFO("the rot guard alone gets them going: each input is a floor");
    force(live, "cli_rotVentDem", 1.0f);
    d.step(200);
    CHECK(live.bank.get(intr) == doctest::Approx(1.0f));

    INFO("asking to HUMIDIFY is this device's business too: the air it draws "
         "comes in through the wetted skirt, which is the only humidifier the "
         "plant has");
    force(live, "cli_rotVentDem", 0.0f);
    force(live, "cli_humidDem", 1.0f);
    d.step(300);
    CHECK(live.bank.get(intr) == doctest::Approx(1.0f));
    force(live, "cli_rotVentDem", 1.0f);

    INFO("...but asking to DEHUMIDIFY is, once the air outside is drier: the "
         "gate is the humidity law's verdict, not this one's");
    force(live, "cli_rotVentDem", 0.0f);
    force(live, "cli_humidDem", -1.0f);
    d.step(400);
    CHECK(live.bank.get(intr) == doctest::Approx(0.15f));
    force(live, "cli_intakeDryOk", true);
    d.step(500);
    CHECK(live.bank.get(intr) == doctest::Approx(1.0f));
    CHECK(live.bank.get(live.bank.resolve<float>("cli_intr2")) == doctest::Approx(1.0f));
}

TEST_CASE_FIXTURE(StRig, "intractors: cooling only draws air that is worth drawing") {

    Vars::Ref<float> intr = live.bank.resolve<float>("cli_intr1");

    INFO("a full cooling demand with the intake no cooler than the house does "
         "nothing: the fans would only bring the outside in");
    force(live, "cli_coolDem", 1.0f);
    d.step(100);
    CHECK(live.bank.get(intr) == doctest::Approx(0.15f));

    force(live, "cli_intakeCoolOk", true);
    d.step(200);
    CHECK(live.bank.get(intr) == doctest::Approx(1.0f));

    INFO("and the NEGATIVE half is a demand of its own: cold inside, warm "
         "ground, and the same fans pull the stored heat up");
    force(live, "cli_intakeCoolOk", false);
    force(live, "cli_coolDem", -1.0f);
    d.step(300);
    CHECK(live.bank.get(intr) == doctest::Approx(0.15f));
    force(live, "cli_groundHeatOk", true);
    d.step(400);
    INFO("one rung, at the spec's low speed: pulling harder on a cold house "
         "cools the ground rather than warming the house");
    CHECK(live.bank.get(intr) ==
          doctest::Approx(live.bank.var(live.ref("cli_groundSpeed")).as_f32()));
}

TEST_CASE_FIXTURE(StRig, "fans: the day's two breezes are a floor under the brassage") {
    interlocks_clear(live);

    // Nothing else asking: no stratification, no cooling, no cycle.
    live.bank.var(live.ref("cli_stratDem")).set(0.0f);
    force(live, "cli_coolDem", 0.0f);
    put_phase(live, Site::PHASE_DAY);
    d.step(100);
    CHECK_FALSE(live.bank.get(live.bank.resolve<bool>("cli_destrat1_speed1")));

    INFO("dusk: a light circulation dries the leaves before the stomata open - "
         "these plants breathe at night, and a wet leaf cannot");
    put_phase(live, Site::PHASE_DUSK_BREEZE);
    d.step(200);
    CHECK(live.bank.get(live.bank.resolve<bool>("cli_destrat1_speed1")));

    INFO("...and dawn, for the night's condensation");
    put_phase(live, Site::PHASE_DAWN_BREEZE);
    d.step(300);
    CHECK(live.bank.get(live.bank.resolve<bool>("cli_destrat1_speed1")));

    INFO("a FLOOR, not a target: cooling still takes them higher");
    force(live, "cli_coolDem", 1.0f);
    d.step(400);
    CHECK(live.bank.get(live.bank.resolve<bool>("cli_destrat1_speed3")));

    put_phase(live, Site::PHASE_NIGHT);
    force(live, "cli_coolDem", 0.0f);
    // The whole plant is on this beat now, irrigation included, and a cycle
    // that started in the dawn slot is one of the four floors under the fan
    // ladder. This case is about the BREEZE floor, so the others are quiet.
    force(live, "irr_state", (int64_t)Site::IRR_IDLE);
    for (uint32_t i = 0; i < 5; i++) d.step(500 + 100u * i);
    CHECK_FALSE(live.bank.get(live.bank.resolve<bool>("cli_destrat1_speed1")));
}

TEST_CASE_FIXTURE(StRig, "fans: one speed relay at a time, and a band change costs hysteresis") {
    interlocks_clear(live);

    Vars::Ref<bool> s1 = live.bank.resolve<bool>("cli_destrat1_speed1");
    Vars::Ref<bool> s2 = live.bank.resolve<bool>("cli_destrat1_speed2");
    Vars::Ref<bool> s3 = live.bank.resolve<bool>("cli_destrat1_speed3");
    const float med = live.bank.var(live.ref("cli_fanMedDem")).as_f32();

    // Driven through the COOLING floor: cli_stratDem is destrat's output now
    // and would be overwritten on every scan of the resource.
    force(live, "cli_coolDem", med + 0.01f);
    d.step(100);
    CHECK_FALSE(live.bank.get(s1));
    CHECK(live.bank.get(s2));
    CHECK_FALSE(live.bank.get(s3));

    INFO("just under the threshold again is not a band change");
    force(live, "cli_coolDem", med - 0.01f);
    d.step(200);
    CHECK(live.bank.get(s2));

    INFO("...it takes the whole hysteresis to give the band back");
    force(live, "cli_coolDem", med - 0.2f);
    d.step(300);
    CHECK(live.bank.get(s1));
    CHECK_FALSE(live.bank.get(s2));
}

TEST_CASE_FIXTURE(StRig, "fans: a watering cycle is a floor under them") {
    interlocks_clear(live);

    // Both demands present and asking for nothing: an ABSENT one is a floor of
    // its own, which is the case above this one.
    live.bank.var(live.ref("cli_stratDem")).set(0.0f);
    force(live, "cli_coolDem", 0.0f);
    d.step(100);
    CHECK_FALSE(live.bank.get(live.bank.resolve<bool>("cli_destrat1_speed1")));

    live.bank.var(live.ref("irr_state")).set_i((int64_t)Site::IRR_FILLING);
    d.step(200);
    CHECK(live.bank.get(live.bank.resolve<bool>("cli_destrat1_speed1")));
}

TEST_CASE_FIXTURE(StRig, "skirt: the humidify arm is proportional, the cooling arm is staged") {

    Vars::Ref<float> duty = live.bank.resolve<float>("cli_skirtDuty");
    const uint32_t period = live.bank.var(live.ref("cli_skirtPeriod_sec")).value().as<uint32_t>();
    const uint32_t min_on = live.bank.var(live.ref("cli_skirtMinOn_sec")).value().as<uint32_t>();

    // Dry enough for a wet pad to cool, so the cooling arm is not gated out.
    // FORCED: temp_rh_zone owns the margin and humidity owns the lockout, and
    // both are on this beat.
    force(live, "cli_dpMargin_degC", 20.0f);
    force(live, "cli_rotLockout", false);
    interlocks_clear(live);

    force(live, "cli_humidDem", 0.5f);
    d.step(100);
    CHECK(live.bank.get(duty) == doctest::Approx(0.5f).epsilon(0.02));

    INFO("a demand whose pulse would be under the minimum does not open at all");
    force(live, "cli_humidDem", 0.5f * (float)min_on / (float)period);
    d.step(200);
    CHECK(live.bank.get(duty) == doctest::Approx(0.0f));

    INFO("asking to DEHUMIDIFY never opens it: that arm belongs to the intractors");
    force(live, "cli_humidDem", -1.0f);
    d.step(300);
    CHECK(live.bank.get(duty) == doctest::Approx(0.0f));

    INFO("and the cooling arm starts only above its own staging threshold");
    const float start = live.bank.var(live.ref("cli_skirtCoolDem")).as_f32();
    force(live, "cli_coolDem", start - 0.05f);
    d.step(400);
    CHECK(live.bank.get(duty) == doctest::Approx(0.0f));
    force(live, "cli_coolDem", 1.0f);
    d.step(500);
    CHECK(live.bank.get(duty) == doctest::Approx(1.0f));
}

TEST_CASE_FIXTURE(StRig, "skirt: damp air gates the cooling arm, and the rot lockout cuts both") {

    Vars::Ref<bool> valve = live.bank.resolve<bool>("cli_skirtValve");
    const float dry_min = live.bank.var(live.ref("cli_skirtDryMin_degC")).as_f32();
    interlocks_clear(live);
    force(live, "cli_rotLockout", false);

    INFO("a wet pad in damp air adds water without cooling anything");
    force(live, "cli_dpMargin_degC", dry_min - 1.0f);
    force(live, "cli_coolDem", 1.0f);
    d.step(100);
    CHECK_FALSE(live.bank.get(valve));

    force(live, "cli_dpMargin_degC", dry_min + 1.0f);
    d.step(200);
    CHECK(live.bank.get(valve));

    INFO("and the rot lockout cuts it whatever either arm is asking for");
    force(live, "cli_rotLockout", true);
    d.step(300);
    CHECK_FALSE(live.bank.get(valve));
    CHECK(live.bank.get(live.bank.resolve<float>("cli_skirtDuty")) == doctest::Approx(0.0f));
}

TEST_CASE("program: a name that resolves to nothing is what FAULTS a program") {
    // The signal Plc::Runtime::Plant::wire() reads: it brackets each program's
    // bind with this count and marks the slot INVALID when it moved, so a law
    // whose refs are inert sinks never runs. Asserted on the count itself,
    // because the site's own programs all resolve - test_site.cpp fails the
    // gate if one does not - so there is no faulted program to observe.
    Live live;
    const uint16_t before = live.bank.unresolved_count();
    (void)live.bank.resolve<float>("no_such_row_anywhere");
    CHECK(live.bank.unresolved_count() == before + 1);

    INFO("and the sink reads 0 for ever, which is the reason it is a fault: a "
         "law bound to one would run on zeros and say nothing at all");
    Vars::Ref<float> sink = live.bank.resolve<float>("no_such_row_either");
    CHECK(live.bank.get(sink) == doctest::Approx(0.0f));
}

}  // namespace site_programs
