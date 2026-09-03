/**
 * @file test_site_irrigation.cpp
 * @brief The watering cycle end to end: the schedule, the valves, and the two meters.
 *
 * Driven through the real scan cycle against the site's real tables.
 * The fixtures are program_rig.h; the cycle itself is test_scan.cpp.
 */

#include "program_rig.h"

namespace site_programs {

namespace {

/** @brief Fill tray 1, then hold @p lpm on the outlet meter for the whole drain
 *  window. @return the litres the meter says came back. */
template <typename D>
float drain_at(Live& live, D& d, float lpm, bool meter_live,
               Plc::Health* worst = nullptr) {
    no_soak(live);
    slot(live);
    live.bank.param_set(live.bank.var(live.ref("irr_forceOn_tray1")), true);
    live.bank.param_set(live.bank.var(live.ref("irr_liters_tray1")), 5.0f);
    live.bank.param_set(live.bank.var(live.ref("irr_input_qtyPerImp")), 1.0f);

    uint64_t t = 0;
    d.step(t += 1000);
    HAL::Pulse::inject(pulse_line(live, "irr_inCount"), 20);
    d.step(t += 1000);
    REQUIRE(live.bank.var(live.ref("irr_state")).as_i64() == Site::IRR_DRAINING);

    const uint32_t window = (uint32_t)live.bank.var(live.ref("irr_drainWindow_sec")).as_i64();
    for (uint32_t i = 0; i <= window + 2; i++) {
        // Every second: the row's timeout is 5 s, and a meter that went stale
        // half way through would be testing the invalid path instead.
        if (meter_live)
            live.bank.var(live.ref("irr_out_Lpm")).publish(from_f32(T_F32, lpm), t);
        d.step(t += 1000);
        // The outlet meter is ST now, so a silent instrument is a COMPANION
        // rather than a health: the meter binds every name it asks for either
        // way, and irr_outFlow_Lpm_qlt is what the cycle reads.
        if (worst != nullptr && !live.bank.var(live.ref("irr_outFlow_Lpm")).valid())
            *worst = Plc::Health::INVALID;
    }
    return live.bank.var(live.ref("irr_drained_L")).as_f32();
}

}  // namespace

TEST_CASE_FIXTURE(StRig, "irrigation: every row it reads back is written before it is read") {
    // The four inout rows are published and then JUDGED on, so each is a
    // watched INPUT too: one the law has not written yet is invalid, and the
    // program reports DEGRADED for it. drained was only written on entering the
    // drain, so a plant whose slot was hours away sat degraded until then.
    uint64_t t = 0;
    for (uint32_t i = 0; i < 4; i++) d.step(t += 1000);
    const char* const readback[] = { "irr_delivered_L", "irr_drained_L",
                                     "irr_servedToday", "irr_today" };
    for (const char* r : readback) {
        Vars::VarRef ref = live.bank.find(r);
        REQUIRE(ref.index != Vars::VAR_NONE);
        INFO("row: ", r);
        CHECK(live.bank.var(ref).valid());
    }
}

TEST_CASE_FIXTURE(StRig, "irrigation: a modulation nobody can read still waters, and says it guessed") {
    // The two halves of this used to disagree: the CYCLE was told to run on
    // the declared modulation, while the SCHEDULE still refused to name a tray
    // without a measured one - so nothing started and the tray stayed dry.
    no_soak(live);
    slot(live);
    live.bank.param_set(live.bank.var(live.ref("irr_liters_tray1")), 5.0f);
    live.bank.param_set(live.bank.var(live.ref("irr_forceOn_tray1")), true);
    live.bank.var(live.ref("irr_mod")).set_quality(Q_NO_DATA);
    // ...and the reason it cannot be read, which is where the chain actually
    // breaks: irr_modulation folds only a MEASURED vpd, and temp_rh_zone
    // publishes both the VPD and its one companion. Forced, because both
    // programs are on this beat and would put their own answer back.
    distrust(live, "cli_dpMargin_degC");

    const VarRef due = live.ref("irr_dueTray");
    const VarRef vol = live.ref("irr_volume_L");
    uint64_t t = 0;
    for (uint32_t i = 0; i < 5; i++) d.step(t += 1000);
    REQUIRE(live.bank.var(live.ref("irr_state")).as_i64() != Site::IRR_IDLE);

    INFO("it fills: the declared modulation covers the ordinary case, and a "
         "tray left dry because the weather stopped being legible is the worse "
         "outcome");
    CHECK(live.bank.var(vol).as_f32() > 0.0f);

    INFO("...and the modulation says it was guessed at, which is all a reader "
         "needs to know the dose should not be trended. The COMPANION says it: "
         "irr_volume_L is ST-written and so always valid.");
    CHECK_FALSE(live.bank.var(live.ref("irr_mod")).valid());

    INFO("...while the TRAY NUMBER stays good: its declared value is 0, so a "
         "doubtful one would read as no tray owed and the cycle would refuse");
    CHECK(live.bank.var(due).valid());
    CHECK(live.bank.var(due).as_i64() != 0);
}

TEST_CASE_FIXTURE(StRig, "irrigation: a force is spent on the watering it asked for") {
    summer(live);
    no_soak(live);
    slot(live);
    field(live, "cli_temp_ext", 25.0f);
    live.bank.param_set(live.bank.var(live.ref("irr_liters_tray2")), 5.0f);
    live.bank.param_set(live.bank.var(live.ref("irr_input_qtyPerImp")), 1.0f);
    REQUIRE(live.bank.param_set(live.bank.var(live.ref("irr_forceOn_tray2")), true) == SET_OK);

    uint64_t t = 0;
    d.step(t += 1000);
    REQUIRE(live.bank.var(live.ref("irr_activeTray")).as_i64() == 2);

    INFO("the force is cleared as the fill completes: left standing it would "
         "ask for the same tray on every cycle for ever, and no other tray "
         "would ever be reached");
    HAL::Pulse::inject(pulse_line(live, "irr_inCount"), 20);
    d.step(t += 1000);
    REQUIRE(live.bank.var(live.ref("irr_state")).as_i64() == Site::IRR_DRAINING);
    CHECK_FALSE(live.bank.var(live.ref("irr_forceOn_tray2")).as_bool());

    INFO("...and it is owed to flash, like every other schedule edit");
    CHECK(live.bank.var(live.ref("irr_forceOn_tray2")).param_owed());
}

TEST_CASE_FIXTURE(StRig, "irrigation: a skip is spent too, and sends the tray to the back") {
    summer(live);
    slot(live);
    field(live, "cli_temp_ext", 25.0f);

    // Every tray is due at bring-up - none has ever been served - so the one
    // held back is the one the cycle would otherwise have taken first.
    REQUIRE(live.bank.param_set(live.bank.var(live.ref("irr_forceOff_tray1")), true) == SET_OK);

    d.step(1000);
    INFO("tray 1 is passed over, and the next due tray goes instead");
    CHECK(live.bank.var(live.ref("irr_activeTray")).as_i64() == 2);

    INFO("the skip is spent doing it, and the tray is stamped so it waits its "
         "period rather than arriving again tomorrow with the flag gone");
    CHECK_FALSE(live.bank.var(live.ref("irr_forceOff_tray1")).as_bool());
    CHECK(live.bank.var(live.ref("irr_last_tray1")).as_i64() > 0);
    CHECK(live.bank.var(live.ref("irr_last_tray1")).param_owed());
}

TEST_CASE_FIXTURE(StRig, "irrigation: the tray sits full for its soak before either end opens") {
    summer(live);
    slot(live);
    field(live, "cli_temp_ext", 25.0f);
    live.bank.param_set(live.bank.var(live.ref("irr_forceOn_tray1")), true);
    live.bank.param_set(live.bank.var(live.ref("irr_liters_tray1")), 5.0f);
    live.bank.param_set(live.bank.var(live.ref("irr_input_qtyPerImp")), 1.0f);
    REQUIRE(live.bank.param_set(live.bank.var(live.ref("irr_soak_sec")), "60") == SET_OK);

    uint64_t t = 0;
    d.step(t += 1000);
    REQUIRE(live.bank.var(live.ref("irr_state")).as_i64() == Site::IRR_FILLING);
    HAL::Pulse::inject(pulse_line(live, "irr_inCount"), 20);
    d.step(t += 1000);

    INFO("the dose has arrived, and the tray keeps it: both ends of the "
         "manifold shut, and the tray still connected to it");
    REQUIRE(live.bank.var(live.ref("irr_state")).as_i64() == Site::IRR_SOAKING);
    CHECK_FALSE(live.bank.var(live.ref("irr_input_ro1")).as_bool());
    CHECK_FALSE(live.bank.var(live.ref("irr_output_ro1")).as_bool());
    CHECK(live.bank.var(live.ref("irr_tray1")).as_bool());
    CHECK_FALSE(live.bank.var(live.ref("irr_tray2")).as_bool());
    INFO("...and it is still this tray's cycle while it sits");
    CHECK(live.bank.var(live.ref("irr_activeTray")).as_i64() == 1);

    INFO("the fill is judged before the soak, not after it: a tray that got "
         "its water is not owed a verdict an hour later");
    CHECK_FALSE(live.bank.var(live.ref("alarm_irr_fill")).as_bool());
    CHECK(live.bank.var(live.ref("irr_servedToday")).as_i64() == 1);

    INFO("half way through, nothing has moved");
    for (uint32_t i = 0; i < 30; i++) d.step(t += 1000);
    CHECK(live.bank.var(live.ref("irr_state")).as_i64() == Site::IRR_SOAKING);
    CHECK_FALSE(live.bank.var(live.ref("irr_output_ro1")).as_bool());

    INFO("and the drain opens when the soak is up");
    for (uint32_t i = 0; i < 32; i++) d.step(t += 1000);
    CHECK(live.bank.var(live.ref("irr_state")).as_i64() == Site::IRR_DRAINING);
    CHECK(live.bank.var(live.ref("irr_output_ro1")).as_bool());
    CHECK(live.bank.var(live.ref("irr_tray1")).as_bool());
}

TEST_CASE_FIXTURE(StRig, "irrigation: a soak of zero is skipped, not passed through") {
    // The field bus carries irr_state, so a state that appears for one
    // activation is a state somebody has to explain.
    summer(live);
    no_soak(live);
    slot(live);
    field(live, "cli_temp_ext", 25.0f);
    live.bank.param_set(live.bank.var(live.ref("irr_forceOn_tray1")), true);
    live.bank.param_set(live.bank.var(live.ref("irr_liters_tray1")), 5.0f);
    live.bank.param_set(live.bank.var(live.ref("irr_input_qtyPerImp")), 1.0f);

    uint64_t t = 0;
    d.step(t += 1000);
    REQUIRE(live.bank.var(live.ref("irr_state")).as_i64() == Site::IRR_FILLING);
    HAL::Pulse::inject(pulse_line(live, "irr_inCount"), 20);
    d.step(t += 1000);
    CHECK(live.bank.var(live.ref("irr_state")).as_i64() == Site::IRR_DRAINING);
}

TEST_CASE_FIXTURE(StRig, "irrigation: the drain meter comes back to zero, not to a tenth of one") {
    // A deadband only writes when the value moves MORE than its band, so a
    // flow decaying toward zero leaves the row parked on the last figure it
    // reported. That is a drain meter saying water is moving with every valve
    // shut, which is the one thing a leak detector must not be told.

    Vars::Ref<float> shown = live.bank.resolve<float>("irr_outFlow_Lpm");
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

TEST_CASE_FIXTURE(StRig,
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
        2u * (uint32_t)live.bank.var(live.ref("irr_rateWin_ms")).as_i64();
    live.bank.param_set(live.bank.var(live.ref("irr_input_qtyPerImp")), 1.0f);

    uint64_t t = 0;
    force(live, "irr_state", (int64_t)Site::IRR_FILLING);
    for (uint32_t i = 0; i < 20; i++) {          // a litre a second, so 60 Lpm
        HAL::Pulse::inject(pulse_line(live, "irr_inCount"), 1);
        d.step(t += 1000);
    }
    REQUIRE(live.bank.var(live.ref("irr_inFlow_Lpm")).as_f32() > 30.0f);

    INFO("the cycle goes idle with the window still full of the fill's water");
    force(live, "irr_state", (int64_t)Site::IRR_IDLE);
    for (uint32_t i = 0; i <= win / 1000u + 2u; i++) {
        d.step(t += 1000);
        CHECK_FALSE(live.bank.var(leak).as_bool());
    }

    INFO("...but water still moving a window later IS a leak");
    for (uint32_t i = 0; i < win / 1000u + 4u; i++) {
        HAL::Pulse::inject(pulse_line(live, "irr_inCount"), 1);
        d.step(t += 1000);
    }
    CHECK(live.bank.var(leak).as_bool());

    INFO("and the mirror image: a fill opening on an idle window is not a "
         "pressure fault");
    force(live, "irr_state", (int64_t)Site::IRR_IDLE);
    for (uint32_t i = 0; i < win / 1000u + 4u; i++) d.step(t += 1000);
    REQUIRE_FALSE(live.bank.var(pres).as_bool());
    force(live, "irr_state", (int64_t)Site::IRR_FILLING);
    for (uint32_t i = 0; i + 1u < win / 1000u; i++) {   // up TO the window
        d.step(t += 1000);
        CHECK_FALSE(live.bank.var(pres).as_bool());
    }

    INFO("...while a supply that never arrives still is");
    for (uint32_t i = 0; i < win / 1000u + 4u; i++) d.step(t += 1000);
    CHECK(live.bank.var(pres).as_bool());
}

TEST_CASE("irrigation: the day the schedule runs on is today's date") {
    // Two ways of asking what day it is, and they have to agree. They did not:
    // the date came off cal_time_ms(), which takes an UPTIME-domain instant and
    // projects it onto the wall, so a wall time handed to it was projected a
    // second time - a date around 2083, gaining a day every twelve hours. Every
    // tray's interval was therefore half what its parameter said.
    Live f;
    Plc::Runtime::Plant st(Botanik::SetupContext(f.reg), f.bank);
    st.wire();
    summer(f);
    st.scan(1000);

    const uint32_t want = Utils::cal_time_ms(HAL::Clock::uptime_ms()).unixDays;
    CHECK((uint32_t)f.bank.var(f.ref("irr_today")).as_i64() == want);
}

TEST_CASE_FIXTURE(StRig, "irrigation: a plant with no clock still spends the force it was given") {
    // The path a stored force takes on hardware that has just booted: the RTC
    // has never been set, NTP has not answered, and there is no date to stamp
    // the watering against. The force must be spent ANYWAY - unspent it asks
    // for the same tray on the next cycle, and the one after that, and nothing
    // bounds the day either, so one tray is flooded until somebody notices.
    summer(live);
    no_soak(live);
    slot(live);
    field(live, "cli_temp_ext", 25.0f);
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
    REQUIRE(live.bank.var(live.ref("irr_activeTray")).as_i64() == 2);

    HAL::Pulse::inject(pulse_line(live, "irr_inCount"), 20);
    d.step(t += 1000);
    REQUIRE(live.bank.var(live.ref("irr_state")).as_i64() == Site::IRR_DRAINING);
    INFO("the force is spent, and the day's tally counted, though neither has "
         "a date to be recorded against");
    CHECK_FALSE(live.bank.var(live.ref("irr_forceOn_tray2")).as_bool());
    CHECK(live.bank.var(live.ref("irr_servedToday")).as_i64() == 1);
    INFO("...and nothing was stamped, because there is nothing to stamp");
    CHECK(live.bank.var(live.ref("irr_last_tray2")).as_i64() == 0);
}

TEST_CASE_FIXTURE(StRig, "irrigation: `water now` is spent on the cycle it started") {
    // The other force, and the dangerous one: it overrides both the slot and
    // the day's allowance, so a flag nothing clears runs cycle after cycle at
    // any hour until an operator notices the water bill.
    summer(live);
    no_soak(live);
    field(live, "cli_temp_ext", 25.0f);
    live.bank.param_set(live.bank.var(live.ref("irr_liters_tray1")), 5.0f);
    live.bank.param_set(live.bank.var(live.ref("irr_input_qtyPerImp")), 1.0f);

    // The middle of the afternoon, which is no kind of watering slot.
    put_phase(live, Site::PHASE_DAY);
    live.bank.var(live.ref("sol_fromRise_min")).set_i(600);

    uint64_t t = 0;
    d.step(t += 1000);
    REQUIRE(live.bank.var(live.ref("irr_state")).as_i64() == Site::IRR_IDLE);

    INFO("`water now` overrides the slot, which is what it is for");
    REQUIRE(live.bank.param_set(live.bank.var(live.ref("irr_forceNow")), true) == SET_OK);
    d.step(t += 1000);
    REQUIRE(live.bank.var(live.ref("irr_state")).as_i64() == Site::IRR_FILLING);

    INFO("...and is spent as the fill completes, so the plant goes back to its "
         "slots rather than running cycles until somebody clears the flag");
    HAL::Pulse::inject(pulse_line(live, "irr_inCount"), 20);
    d.step(t += 1000);
    REQUIRE(live.bank.var(live.ref("irr_state")).as_i64() == Site::IRR_DRAINING);
    CHECK_FALSE(live.bank.var(live.ref("irr_forceNow")).as_bool());
    CHECK(live.bank.var(live.ref("irr_forceNow")).param_owed());
}

TEST_CASE_FIXTURE(StRig, "irrigation: the afternoon is not a watering slot") {
    // The meters FIRST: the cycle judges a fill against what they publish.
    summer(live);

    field(live, "cli_temp_ext", 25.0f);
    live.bank.param_set(live.bank.var(live.ref("irr_forceOn_tray1")), true);
    live.bank.param_set(live.bank.var(live.ref("irr_liters_tray1")), 5.0f);

    // DAY, but hours past the morning window: a tray filled now is still wet
    // when the stomata open.
    const int16_t rise = (int16_t)(live.bank.var(live.ref("sol_dayRise_min")).as_i64() +
                                   live.bank.var(live.ref("irr_morningWin_min")).as_i64() + 1);
    put_phase(live, Site::PHASE_DAY);
    live.bank.var(live.ref("sol_fromRise_min")).set_i(rise);
    d.step(1000);
    CHECK(live.bank.var(live.ref("irr_state")).as_i64() == Site::IRR_IDLE);

    INFO("inside the morning window it goes");
    live.bank.var(live.ref("sol_fromRise_min")).set_i((int64_t)(rise - 2));
    d.step(2000);
    CHECK(live.bank.var(live.ref("irr_state")).as_i64() == Site::IRR_FILLING);

    INFO("and a cold end of night waits for that window rather than watering "
         "cold roots");
    Live g;
    // Every irrigation law is ST, so the second plant is just the RESOURCE.
    Plc::Runtime::Plant st2(Botanik::SetupContext(g.reg), g.bank);
    st2.wire();
    struct { Plc::Runtime::Plant* st;
             void step(uint64_t n) { st->scan(n); } } d2{ &st2 };
    summer(g);
    g.bank.param_set(g.bank.var(g.ref("irr_forceOn_tray1")), true);
    put_phase(g, Site::PHASE_DAWN_BREEZE);
    // Below the gate, and FORCED under it: slot() puts every other case above.
    force(g, "cli_temp_ext",
          g.bank.var(g.ref("irr_dawnGate_degC")).as_f32() - 1.0f);
    d2.step(1000);
    CHECK(g.bank.var(g.ref("irr_state")).as_i64() == Site::IRR_IDLE);
}

TEST_CASE_FIXTURE(StRig, "irrigation: a fill fault names the tray, not just the cycle") {
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
    REQUIRE(live.bank.var(live.ref("irr_state")).as_i64() == Site::IRR_FILLING);
    REQUIRE(live.bank.var(live.ref("irr_activeTray")).as_i64() == 2);

    for (uint32_t i = 0; i < 102; i++) d.step(t += 1000);

    INFO("the any-form is what an operator sees; the per-tray one is what still "
         "says which tray once the cycle has moved on");
    CHECK(live.bank.var(live.ref("alarm_irr_fill")).as_bool());
    CHECK(live.bank.var(live.ref("alarm_irr_fill_tray2")).as_bool());
    CHECK_FALSE(live.bank.var(live.ref("alarm_irr_fill_tray1")).as_bool());
    CHECK_FALSE(live.bank.var(live.ref("alarm_irr_fill_tray3")).as_bool());
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

TEST_CASE_FIXTURE(StRig, "irrigation: the cycle fills to its dose, then drains, then idles") {
    // The meters FIRST: the cycle judges a fill against what they publish.
    summer(live);
    no_soak(live);

    // Bare, not REQUIREd: a channel another case already attached answers false.

    // Warm enough that the dawn gate is open, in the end-of-night slot, and
    // tray 1 forced so the cycle starts without the calendar the due-check
    // still wants.
    live.bank.var(live.ref("cli_vpd_kPa")).publish(from_f32(T_F32, 1.5f), 0);
    slot(live);
    live.bank.param_set(live.bank.var(live.ref("irr_forceOn_tray1")), true);
    live.bank.param_set(live.bank.var(live.ref("irr_liters_tray1")), 5.0f);
    live.bank.param_set(live.bank.var(live.ref("irr_input_qtyPerImp")), 1.0f);

    uint64_t t = 0;
    d.step(t += 1000);
    CHECK(live.bank.var(live.ref("irr_state")).as_i64() == Site::IRR_FILLING);
    CHECK(live.bank.var(live.ref("irr_activeTray")).as_i64() == 1);
    INFO("filling: inlet and this tray open, outlet shut, no other tray");
    CHECK(live.bank.var(live.ref("irr_input_ro1")).as_bool());
    CHECK(live.bank.var(live.ref("irr_tray1")).as_bool());
    CHECK_FALSE(live.bank.var(live.ref("irr_output_ro1")).as_bool());
    CHECK_FALSE(live.bank.var(live.ref("irr_tray2")).as_bool());

    // The meter delivers the dose. It is modulated, so ask for plenty.
    HAL::Pulse::inject(pulse_line(live, "irr_inCount"), 20);
    d.step(t += 1000);
    CHECK(live.bank.var(live.ref("irr_state")).as_i64() == Site::IRR_DRAINING);
    INFO("draining: inlet shut, tray still open, outlet open");
    CHECK_FALSE(live.bank.var(live.ref("irr_input_ro1")).as_bool());
    CHECK(live.bank.var(live.ref("irr_tray1")).as_bool());
    CHECK(live.bank.var(live.ref("irr_output_ro1")).as_bool());
    CHECK_FALSE(live.bank.var(live.ref("alarm_irr_fill")).as_bool());

    INFO("the day is stamped at the END OF THE FILL, so a fault during the long "
         "drain cannot cause a second watering");
    CHECK(live.bank.var(live.ref("irr_servedToday")).as_i64() == 1);
    const uint32_t stamped = (uint32_t)live.bank.var(live.ref("irr_last_tray1")).as_i64();
    CHECK(stamped > 0);
    // The bank under test has no store bound, so the write stays marked and a
    // later save retries it. That mark is the whole point: a plain set() would
    // have written the value and left NOTHING owed to flash, so the schedule
    // would restart from nothing at the next boot.
    INFO("it went through the parameter path, so flash is owed the new value");
    CHECK(live.bank.var(live.ref("irr_last_tray1")).param_owed());
    INFO("...and it is the PROGRAM that owes it: the params page counts what a "
         "person typed, and a watering date is not that");
    CHECK_FALSE(live.bank.var(live.ref("irr_last_tray1")).param_unsaved());

    const uint32_t window = (uint32_t)live.bank.var(live.ref("irr_drainWindow_sec")).as_i64();
    for (uint32_t i = 0; i <= window + 2; i++) d.step(t += 1000);
    CHECK(live.bank.var(live.ref("irr_activeTray")).as_i64() == 0);
    CHECK(live.bank.var(live.ref("irr_last_tray1")).as_i64() == stamped);
}

TEST_CASE_FIXTURE(StRig, "irrigation: the two meters read as a counter and a rate") {
    // The meters FIRST: the cycle judges a fill against what they publish.
    summer(live);
    live.bank.param_set(live.bank.var(live.ref("irr_input_qtyPerImp")), 1.0f);

    uint64_t t = 0;
    d.step(t += 1000);
    // 20 pulses a second at a litre each, for ten seconds.
    for (uint32_t i = 0; i < 10; i++) {
        HAL::Pulse::inject(pulse_line(live, "irr_inCount"), 20);
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

TEST_CASE_FIXTURE(StRig, "irrigation: what comes back out is judged against what went in") {
    // The meters FIRST: the cycle judges a fill against what they publish.
    summer(live);
    // Bare, not REQUIREd: a channel another case already attached answers
    // false, and this one only needs it counting.

    // 20 L went in; 5 L/min over the 300 s window is far more than the 30% the
    // margin allows to be missing.
    const float back = drain_at(live, d, 5.0f, true);
    CHECK(back > 20.0f);
    CHECK_FALSE(live.bank.var(live.ref("alarm_irr_drain")).as_bool());
    CHECK_FALSE(live.bank.var(live.ref("alarm_irr_drain_tray1")).as_bool());
}

TEST_CASE_FIXTURE(StRig, "irrigation: a tray that does not drain names itself") {
    // The meters FIRST: the cycle judges a fill against what they publish.
    summer(live);

    // A trickle: half a litre back out of twenty.
    const float back = drain_at(live, d, 0.1f, true);
    CHECK(back < 1.0f);
    CHECK(live.bank.var(live.ref("alarm_irr_drain")).as_bool());
    CHECK(live.bank.var(live.ref("alarm_irr_drain_tray1")).as_bool());
    INFO("the OTHER trays are not accused");
    CHECK_FALSE(live.bank.var(live.ref("alarm_irr_drain_tray2")).as_bool());
}

TEST_CASE_FIXTURE(StRig, "irrigation: a silent outlet meter gives no verdict, not a clog") {
    // The meters FIRST: the cycle judges a fill against what they publish.
    summer(live);

    Plc::Health worst = Plc::Health::HEALTHY;
    const float back = drain_at(live, d, 0.0f, false, &worst);
    CHECK(back == 0.0f);
    INFO("nothing was measured, so nothing is blamed - the meter has an alarm "
         "of its own");
    CHECK_FALSE(live.bank.var(live.ref("alarm_irr_drain")).as_bool());
    CHECK_FALSE(live.bank.var(live.ref("alarm_irr_drain_tray1")).as_bool());
    INFO("but it does not pass in silence");
}

TEST_CASE_FIXTURE(StRig, "irrigation: a fill that never arrives raises, and still opens the drain") {
    // The meters FIRST: the cycle judges a fill against what they publish.
    summer(live);

    slot(live);
    live.bank.param_set(live.bank.var(live.ref("irr_forceOn_tray1")), true);
    live.bank.param_set(live.bank.var(live.ref("irr_liters_tray1")), 5.0f);
    // The shortest the band allows; a rejected edit would leave the default and
    // the case would just be waiting out five minutes it never reaches.
    REQUIRE(live.bank.param_set(live.bank.var(live.ref("irr_maxFill_sec")), 100.0f) == SET_OK);

    uint64_t t = 0;
    d.step(t += 1000);
    REQUIRE(live.bank.var(live.ref("irr_state")).as_i64() == Site::IRR_FILLING);

    for (uint32_t i = 0; i < 102; i++) d.step(t += 1000);   // no pulses at all

    INFO("leaving the inlet open on a fault is how a tray overflows, and the "
         "soak is skipped with it: this tray was given nothing to steep in");
    CHECK(live.bank.var(live.ref("alarm_irr_fill")).as_bool());
    CHECK(live.bank.var(live.ref("irr_state")).as_i64() == Site::IRR_DRAINING);
    CHECK_FALSE(live.bank.var(live.ref("irr_input_ro1")).as_bool());
}

TEST_CASE_FIXTURE(StRig, "irrigation: a fill that never arrives is spent, not retried") {
    // The flood: with a stuck meter the fill times out, and if the attempt is
    // not spent the schedule re-picks the same tray on the next slot and
    // refills it until the window closes.
    summer(live);
    slot(live);
    live.bank.param_set(live.bank.var(live.ref("irr_forceOn_tray1")), true);
    live.bank.param_set(live.bank.var(live.ref("irr_liters_tray1")), 5.0f);
    REQUIRE(live.bank.param_set(live.bank.var(live.ref("irr_maxFill_sec")), 100.0f) == SET_OK);

    uint64_t t = 0;
    d.step(t += 1000);
    REQUIRE(live.bank.var(live.ref("irr_state")).as_i64() == Site::IRR_FILLING);
    REQUIRE(live.bank.var(live.ref("irr_servedToday")).as_i64() == 0);

    for (uint32_t i = 0; i < 102; i++) d.step(t += 1000);   // no pulses at all
    REQUIRE(live.bank.var(live.ref("alarm_irr_fill")).as_bool());

    INFO("the force asked for one watering, and it got its attempt");
    CHECK(live.bank.var(live.ref("irr_forceOn_tray1")).as_bool() == false);
    INFO("the day's allowance counts an attempt, which is what bounds the day");
    CHECK(live.bank.var(live.ref("irr_servedToday")).as_i64() == 1);
    INFO("...and the tray is stamped, so its own interval backs it off");
    CHECK(live.bank.var(live.ref("irr_last_tray1")).as_i64() > 0);
}

TEST_CASE_FIXTURE(StRig, "irrigation: the pulse line says whether the meter is ticking") {
    // LocalInputs skips an F_PULSE terminal deliberately, so nothing else can
    // publish this row - it read as never-written for the life of the run.
    // Its LEVEL is meaningless (a pulse is a moment of a train), so what it
    // carries is whether an edge arrived in the beat.
    const Vars::VarRef line = live.ref("irr_inPulse");

    uint64_t t = 0;
    d.step(t += 1000);
    INFO("written from the first activation, whatever the meter is doing");
    CHECK(live.bank.var(line).valid());
    CHECK(live.bank.var(line).as_bool() == false);

    HAL::Pulse::inject(pulse_line(live, "irr_inCount"), 5);
    d.step(t += 1000);
    CHECK(live.bank.var(line).as_bool() == true);

    // ...and it goes quiet again when the water stops.
    d.step(t += 1000);
    CHECK(live.bank.var(line).as_bool() == false);
    CHECK(live.bank.var(line).valid());
}

}  // namespace site_programs
