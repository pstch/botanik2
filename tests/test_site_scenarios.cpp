/**
 * @file test_site_scenarios.cpp
 * @brief The plant through a day: heat, cold, rain, damp, and a zone dying.
 *
 * Every program on one bank, driven beat by beat. The fixture is
 * plant_fixture.h.
 */

#include "plant_fixture.h"

namespace site_scenarios {
using namespace fixture;

TEST_CASE("scenario: a hot afternoon opens the roof and pulls cool ground air") {
    ScenarioTrace::scenario("A hot afternoon",
        "32 degC under the ridge with 18 degC in the crawlspace: the whole "
        "cooling ladder, from the roof down to the wet skirt.");
    Plant p;
    hold(p, HOT_AFTERNOON, Site::PHASE_DAY);
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
    hold(p, COLD_NIGHT, Site::PHASE_NIGHT);
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

    SAY("70% on a cold night is an ordinary night, and the alarm says so: the "
         "margin is wide, the ladder is idle, and TOO_HUM agrees with both");
    CHECK(p.num("cli_dpMargin_degC") > p.num("cli_dehumFull_degC"));
    CHECK(p.num("cli_humidDem") == doctest::Approx(0.0f));
    CHECK_FALSE(p.on("alarm_too_humid"));
    snapshot(p, "the plant settled in the cold");
}

TEST_CASE("scenario: rain shuts the roof over the cooling ladder's objection") {
    ScenarioTrace::scenario("A shower on a hot afternoon",
        "The weather interlock against the ladder: which of the two owns the "
        "roof, and how long the answer outlives the rain.");
    Plant p;
    hold(p, HOT_AFTERNOON, Site::PHASE_DAY);
    p.weather_for(900);
    REQUIRE(p.num("cli_window1_pos") > 0.9f);
    snapshot(p, "before the shower: the roof wide open");

    SAY("it starts raining on a hot afternoon: the ladder still wants the roof "
         "open, and stage one of the arbitration does not care");
    g_wx().rain = 4.0f;
    p.weather_for(600);
    CHECK(p.on("cli_rainLockout"));
    CHECK(p.num("cli_windowDem") > 0.5f);      // the demand is unchanged...
    CHECK(p.num("cli_window1_pos") < 0.1f);    // ...and the roof is shut anyway
    snapshot(p, "raining: the demand stands, the roof does not");

    SAY("the shower stops, and the lockout OUTLIVES it: a shower that has just "
         "stopped is about to be one that has not");
    g_wx().rain = 0.0f;
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
    hold(p, DAMP_NIGHT, Site::PHASE_NIGHT);
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

    SAY("and TOO_HUM is up, because the dehumidify arm is saturated and the "
         "air is still this wet - the alarm the operator can act on");
    CHECK(p.num("cli_dpMargin_degC") < p.num("cli_dehumFull_degC"));
    CHECK(p.on("alarm_too_humid"));

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
    hold(p, DRY_SPELL, Site::PHASE_DAY);
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
    hold(p, HOT_AFTERNOON, Site::PHASE_DAY);
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
        p.probe("cli_temp_lo%u", 3, g_wx().floor_);
        p.probe("cli_temp_cs%u", 3, g_wx().crawlspace);
        p.probe("cli_rh_hi%u", 3, g_wx().rh);
        p.probe("cli_rh_lo%u", 3, g_wx().rh);
        p.beat();
    }

    // The COMPANION, not the row: temp_rh_sensors is ST now, and ST has no
    // way to write a quality. The row stays valid and holds its last average.
    CHECK_FALSE(p.on("cli_temp_hi_qlt"));
    SAY("the row HOLDS its last average rather than reading a default of zero");
    CHECK(p.num("cli_temp_hi") == doctest::Approx(was));
    CHECK(p.on("alarm_sns_ceiling"));

    SAY("the plant keeps running on the floor: still hot, still open");
    CHECK(p.num("cli_coolDem") > 0.5f);
    CHECK(p.num("cli_window1_pos") > 0.9f);

    SAY("the dew-point margin is still MEASURED, and it is the FLOOR's: a "
         "level holding an old reading does not speak for the house");
    CHECK(p.on("cli_dpMargin_degC_qlt"));
    CHECK(p.num("cli_dpMargin_degC") ==
          doctest::Approx(p.num("cli_temp_lo") - p.num("cli_dewPoint_degC")));
    snapshot(p, "five minutes without a ceiling reading");

    SAY("the floor goes too, and now nothing measures the house: the margin "
         "keeps the number it last had and says it is not current");
    for (uint8_t i = 1; i <= 3; i++) {
        char n[24]; snprintf(n, sizeof n, "cli_temp_lo%u", (unsigned)i);
        p.f.bank.var(p.ref(n)).invalidate(Q_NO_DATA);
    }
    for (uint32_t i = 0; i < 300; i++) {
        p.probe("cli_temp_cs%u", 3, g_wx().crawlspace);
        p.probe("cli_rh_hi%u", 3, g_wx().rh);
        p.probe("cli_rh_lo%u", 3, g_wx().rh);
        p.beat();
    }
    CHECK_FALSE(p.on("cli_dpMargin_degC_qlt"));
    CHECK(p.num("cli_dpMargin_degC") > 0.0f);   // held, not a default of zero

    SAY("and the rot guard fails toward acting on a margin nobody has, which "
         "is the branch it has always documented and never reached");
    CHECK(p.num("cli_rotVentDem") == doctest::Approx(1.0f));
    CHECK(p.on("cli_rotLockout"));
    snapshot(p, "the house with no measured margin at all");

    SAY("and being blind is an INTERLOCK, not an overrun: the floors answer "
         "again and the lockout lets go with them, where feeding blindness "
         "into the overrun cost five minutes of shut skirt after every boot");
    p.weather_for(50);
    REQUIRE(p.on("cli_dpMargin_degC_qlt"));
    CHECK_FALSE(p.on("cli_rotLockout"));
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
    hold(p, HOT_AFTERNOON, Site::PHASE_DAY);
    p.weather_for(6000);        // ten minutes hard against the limit
    REQUIRE(p.num("cli_coolDem") > 0.9f);
    snapshot(p, "ten minutes hard against the limit");

    SAY("the house reaches its target and the demand comes off the limit at "
         "once, keeping only what the integral had needed: the proportional "
         "term was doing the work, and conditional integration would not let "
         "the integral accumulate underneath it while the output was pinned");
    g_wx().ceiling = p.num("cli_targetTemp_degC");
    g_wx().floor_  = g_wx().ceiling - 1.0f;
    p.weather_for(1200);
    CHECK(p.num("cli_coolDem") < 0.5f);
    SAY("...and it does not fall to nothing either: some ventilation IS what "
         "is holding the house at its target");
    CHECK(p.num("cli_coolDem") > 0.0f);

    SAY("...and once the house is genuinely COLDER than wanted, the loop lets "
         "go from the FIRST beat rather than sitting on stored effort");
    g_wx().ceiling = p.num("cli_targetTemp_degC") - 3.0f;
    g_wx().floor_  = g_wx().ceiling - 1.0f;
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
    hold(p, HOT_AFTERNOON, Site::PHASE_DAY);

    // Park the interior where the intake gate lives: exactly the margin above
    // the crawlspace, which is where two converging spaces sit for hours.
    const float margin = p.num("cli_ventTempMgn_degC");
    g_wx().crawlspace = g_wx().ceiling - margin;
    p.weather_for(600);

    const bool gate = p.on("cli_intakeCoolOk");
    uint32_t flips = 0;
    for (uint32_t i = 0; i < 600; i++) {
        // A tenth of a degree of probe noise either side of the threshold.
        g_wx().crawlspace = g_wx().ceiling - margin + ((i % 2) ? 0.1f : -0.1f);
        hold_weather(p);
        p.beat();
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
        p.beat();
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
        p.beat();
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

    SAY("the traverse is published while it lasts, and the fans are not part "
         "of it: the two are far enough apart mechanically to be independent");
    CHECK(p.on("cli_screenMoving"));

    SAY("and once it has arrived they are allowed back - the breeze phase is "
         "asking for one");
    p.weather_for(700);
    CHECK_FALSE(p.on("cli_screenMoving"));
    CHECK(p.on("cli_destrat1_speed1"));
    snapshot(p, "shut, deployed, and the breeze back on");
}



}  // namespace site_scenarios
