/**
 * @file test_site_actuators.cpp
 * @brief The actuator controllers: what reaches a coil, and where it goes when control is withdrawn.
 *
 * Driven through the real scan cycle against the site's real tables.
 * The fixtures are program_rig.h; the cycle itself is test_scan.cpp.
 */

#include "program_rig.h"

/* matiec's, from the generated CONFIGURATION. Global, and declared without
   extern "C" because the generated files compile as C++ here. */
extern unsigned long long common_ticktime__;

namespace site_programs {

namespace {

/** @brief A program that names a row no table declares. */
class MisnamedProgram final : public Plc::Program {
public:
    Vars::RefIn<float> missing;
    MisnamedProgram(const SetupContext& ctx, Vars::Bank& io)
        : Plc::Program(ctx, "misnamed", Tasks::GROUP_FAST, io) {}

protected:
    void bind(Vars::Bank& io) override {
        missing = io.resolve_in<float>("no_such_row_anywhere");
    }
    void run(ProgramContext& c) override { (void)c; }
};

}  // namespace

TEST_CASE("scan: the ST plant runs at matiec's period, not at one we picked") {
    // common_ticktime__ is the GCD of the TASK intervals in plant.st, so it
    // MOVES the day a program joins on a faster task. Nothing on this side
    // may assume its value: the registration reads it.
    TestScan::Live live;
    Site::StRuntime st(Botanik::SetupContext(live.reg), live.bank);
    st.wire();
    st.register_tasks();

    const Tasks::Task* plant = nullptr;
    for (uint8_t i = 0; i < live.reg.count(); i++)
        if (strcmp(live.reg.task(i).name, "st.plant") == 0) plant = &live.reg.task(i);
    REQUIRE(plant != nullptr);

    INFO("the plant's period is matiec's number, whatever it currently is");
    CHECK(plant->period_ms ==
          (uint32_t)(common_ticktime__ / 1000000ULL));

    INFO("and the beat has to be able to release it: a period that is not a "
         "multiple of TASK_PERIOD_MS drifts rather than runs slow");
    CHECK(plant->period_ms >= Config::TASK_PERIOD_MS);
    CHECK(plant->period_ms % Config::TASK_PERIOD_MS == 0);
}

TEST_CASE_FIXTURE(StRig, "scan: an ST timer measures VIRTUAL time, whatever the beat size") {
    // The property the clock buys over a threaded dt: a block differences
    // sys_now_ms against its OWN last reading, so it is rate-independent. The
    // simulator accelerates by advancing the virtual clock faster in real
    // time, never by changing the step, so this is also what makes a
    // greenhouse day in a minute give the same answers as a greenhouse day.
    Vars::RefIn<bool> lock = live.bank.resolve_in<bool>("cli_rainLockout");
    const uint32_t hold_ms =
        1000u * live.bank.var(live.ref("cli_rainLockout_sec")).value().as<uint32_t>();

    // Released between the last beat before the overrun and the first after
    // it: a step can only ever land on one side or the other.
    uint64_t t = 0;
    for (uint32_t step : { 100u, 700u }) {
        live.bank.var(live.ref("cli_rain_mmph_qlt")).publish(from_bool(T_BOOL, true), t);
        live.bank.var(live.ref("cli_wind_mps_qlt")).publish(from_bool(T_BOOL, true), t);
        live.bank.var(live.ref("cli_rain_mmph")).publish(from_f32(T_F32, 0.4f), t);
        d.step(t += step);
        REQUIRE(live.bank.get(lock));

        live.bank.var(live.ref("cli_rain_mmph")).publish(from_f32(T_F32, 0.0f), t);
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

    Vars::RefIn<bool> s1 = live.bank.resolve_in<bool>("cli_destrat1_speed1");
    Vars::RefIn<bool> s3 = live.bank.resolve_in<bool>("cli_destrat1_speed3");

    // The GAP, not the demand: destrat publishes cli_stratDem every scan now,
    // so an unreadable gap is what reaches the fans. It substitutes the row's
    // declared 2.0 degC, which is the low rung.
    live.bank.var(live.ref("cli_dTStrat_degC")).invalidate(Q_NO_DATA);
    live.bank.var(live.ref("cli_coolDem")).set(0.0f);
    d.step(100);

    INFO("still air is itself a rot risk, so an unknown gap is speed 1");
    CHECK(live.bank.get(s1));

    INFO("...and it is a FLOOR: a cooling demand above it still takes them up");
    live.bank.var(live.ref("cli_coolDem")).set(1.0f);
    d.step(200);
    CHECK_FALSE(live.bank.get(s1));
    CHECK(live.bank.get(s3));
}

TEST_CASE_FIXTURE(Rig<Site::ScreenController>, "screen: a cold night deploys it, and the traverse is published while it runs") {

    Vars::RefIn<bool> screen = live.bank.resolve_in<bool>("cli_screen");
    Vars::RefIn<bool> moving = live.bank.resolve_in<bool>("cli_screenMoving");

    live.bank.var(live.ref("cli_phase")).set_i((int64_t)Site::PHASE_DAY);
    live.bank.var(live.ref("cli_temp_hi")).publish(from_f32(T_F32, 25.0f), 0);
    live.bank.var(live.ref("cli_coolDem")).set(0.5f);
    d.step(100);
    CHECK_FALSE(live.bank.get(screen));

    INFO("a cold night deploys it, and it says so while it goes. The COLD is "
         "what asks - a demand under cli_thermalScreenDem - and the night is "
         "only what permits it");
    live.bank.var(live.ref("cli_phase")).set_i((int64_t)Site::PHASE_NIGHT);
    live.bank.var(live.ref("cli_coolDem"))
          .set(live.bank.var(live.ref("cli_thermalScreenDem")).as_f32() - 0.1f);
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

/** @brief roof_lockout is ST now, so the RESOURCE runs and then the windows
 *  program does - the same order the beat uses, and the one the lockout needs:
 *  cli_windowCap has to reach the rams on the beat it was published. */
struct RoofDrive {
    Site::StRuntime         st;
    Site::WindowsController win;
    Plc::Program*           progs[1];
    Cycle                   cyc;

    explicit RoofDrive(Live& f)
        : st(Botanik::SetupContext(f.reg), f.bank),
          win(Botanik::SetupContext(f.reg), f.bank),
          progs{ &win }, cyc(progs, 1, &f.bank, f.reg) { st.wire(); }

    void step(uint64_t now_ms) {
        st.scan(now_ms);
        cyc.step(now_ms);
    }
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

    Vars::RefIn<float> cap = f.bank.resolve_in<float>("cli_windowCap");
    f.bank.var(f.ref("cli_windowDem")).set(1.0f);
    uint64_t t = 0;

    INFO("under the ease threshold the wind has no opinion at all");
    field(f, "cli_wind_mps", ease - 2.0f);
    field(f, "cli_windGust_mps", ease - 2.0f);
    for (uint32_t i = 0; i < 40; i++) d.step(t += 1000);
    CHECK(f.bank.get(cap) == doctest::Approx(1.0f));
    CHECK(f.bank.var(f.ref("cli_window1_pos")).as_f32() > 0.9f);

    INFO("at it, the roof may open cli_windShutMax and no more");
    field(f, "cli_wind_mps", ease);
    field(f, "cli_windGust_mps", ease);
    for (uint32_t i = 0; i < 60; i++) d.step(t += 1000);
    CHECK(f.bank.get(cap) == doctest::Approx(top).epsilon(0.02));
    CHECK(f.bank.var(f.ref("cli_window1_pos")).as_f32() <= doctest::Approx(top).epsilon(0.1));

    INFO("half way up the band, half the ceiling");
    field(f, "cli_wind_mps", (ease + shut) / 2.0f);
    field(f, "cli_windGust_mps", (ease + shut) / 2.0f);
    for (uint32_t i = 0; i < 60; i++) d.step(t += 1000);
    CHECK(f.bank.get(cap) == doctest::Approx(top / 2.0f).epsilon(0.05));

    INFO("and at cli_windShutEnd_mps the roof is shut, not nearly shut");
    field(f, "cli_wind_mps", shut);
    field(f, "cli_windGust_mps", shut);
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

    Vars::RefIn<float> cap = f.bank.resolve_in<float>("cli_windowCap");
    f.bank.var(f.ref("cli_windowDem")).set(1.0f);
    uint64_t t = 0;
    for (uint32_t i = 0; i < 40; i++) d.step(t += 1000);
    REQUIRE(f.bank.get(cap) == doctest::Approx(1.0f));

    INFO("a still day, and rain: shut, and it outlives the shower by "
         "cli_rainLockout_sec");
    field(f, "cli_rain_mmph", 4.0f);
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

    f.bank.var(f.ref("cli_windowDem")).set(1.0f);
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

TEST_CASE_FIXTURE(RoofRig, "windows: exactly one ram is energised at a time") {
    fine_weather(live);
    interlocks_clear(live);

    live.bank.var(live.ref("cli_windowDem")).set(1.0f);

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

TEST_CASE_FIXTURE(RoofRig, "windows: rain shuts them, and the lockout outlives the shower") {
    fine_weather(live);
    interlocks_clear(live);

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

    const uint32_t hold = live.bank.var(live.ref("cli_rainLockout_sec")).value().as<uint32_t>();
    for (uint32_t i = 0; i < 10 * hold + 20; i++) d.step(t += 100);
    CHECK_FALSE(live.bank.get(rain_lock));
    CHECK(live.bank.get(tgt) == doctest::Approx(1.0f));
}

TEST_CASE_FIXTURE(RoofRig, "windows: a gust shuts them, and the mean alone does not") {
    fine_weather(live);
    interlocks_clear(live);

    Vars::RefIn<bool> wind_lock = live.bank.resolve_in<bool>("cli_windLockout");
    const float shut = live.bank.var(live.ref("cli_windShutEnd_mps")).as_f32();
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

    fine_weather(live);
    interlocks_clear(live);
    live.bank.var(live.ref("cli_windowDem")).set(1.0f);
    d.step(100);
    REQUIRE_FALSE(live.bank.get(live.bank.resolve_in<bool>("cli_windowLockout")));

    INFO("the SAME readings, no longer believed: both rows still say calm and "
         "dry, which is the trap - only the companions separate a fine sky "
         "from one nobody is watching");
    live.bank.var(live.ref("cli_rain_mmph_qlt")).set(false);
    live.bank.var(live.ref("cli_wind_mps_qlt")).set(false);
    d.step(200);
    CHECK(live.bank.get(live.bank.resolve_in<bool>("cli_rainLockout")));
    CHECK(live.bank.get(live.bank.resolve_in<bool>("cli_windLockout")));
    // The program that LOOKS at the sky is the one that cannot see it. The
    // windows read a lockout row, which is as readable as ever.
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

TEST_CASE_FIXTURE(StRig, "fans: the day's two breezes are a floor under the brassage") {
    interlocks_clear(live);

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

TEST_CASE_FIXTURE(StRig, "fans: one speed relay at a time, and a band change costs hysteresis") {
    interlocks_clear(live);

    Vars::RefIn<bool> s1 = live.bank.resolve_in<bool>("cli_destrat1_speed1");
    Vars::RefIn<bool> s2 = live.bank.resolve_in<bool>("cli_destrat1_speed2");
    Vars::RefIn<bool> s3 = live.bank.resolve_in<bool>("cli_destrat1_speed3");
    const float med = live.bank.var(live.ref("cli_fanMedDem")).as_f32();

    // Driven through the COOLING floor: cli_stratDem is destrat's output now
    // and would be overwritten on every scan of the resource.
    live.bank.var(live.ref("cli_coolDem")).set(med + 0.01f);
    d.step(100);
    CHECK_FALSE(live.bank.get(s1));
    CHECK(live.bank.get(s2));
    CHECK_FALSE(live.bank.get(s3));

    INFO("just under the threshold again is not a band change");
    live.bank.var(live.ref("cli_coolDem")).set(med - 0.01f);
    d.step(200);
    CHECK(live.bank.get(s2));

    INFO("...it takes the whole hysteresis to give the band back");
    live.bank.var(live.ref("cli_coolDem")).set(med - 0.2f);
    d.step(300);
    CHECK(live.bank.get(s1));
    CHECK_FALSE(live.bank.get(s2));
}

TEST_CASE_FIXTURE(StRig, "fans: a watering cycle is a floor under them") {
    interlocks_clear(live);

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
    const uint32_t period = live.bank.var(live.ref("cli_skirtPeriod_sec")).value().as<uint32_t>();
    const uint32_t min_on = live.bank.var(live.ref("cli_skirtMinOn_sec")).value().as<uint32_t>();

    // Dry enough for a wet pad to cool, so the cooling arm is not gated out...
    live.bank.var(live.ref("cli_dpMargin_degC")).publish(from_f32(T_F32, 20.0f), 0);
    interlocks_clear(live);

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
    interlocks_clear(live);

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

TEST_CASE("program: a name that resolves to nothing FAULTS the program that asked") {
    Live live;
    MisnamedProgram p{Botanik::SetupContext(live.reg), live.bank};
    Plc::Program* const progs[1] = { &p };
    Cycle d{progs, 1, &live.bank, live.reg};

    INFO("the sink reads 0 for ever, so a law bound to one would run on zeros "
         "and say nothing at all - the program says it instead");
    d.step(100);
    CHECK(live.bank.unresolved_count() == 1);
    CHECK(p.health() == Plc::Health::INVALID);

    INFO("and it stays said: the ref never stops being the sink, so a later "
         "activation cannot read as healthy");
    d.step(200);
    CHECK(p.health() == Plc::Health::INVALID);
}

}  // namespace site_programs
