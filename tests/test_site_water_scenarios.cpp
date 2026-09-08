/**
 * @file test_site_water_scenarios.cpp
 * @brief The plant through a watering: the slot, the forces, and the pipes.
 *
 * Every program on one bank, driven beat by beat. The fixture is
 * plant_fixture.h.
 */

#include "plant_fixture.h"

namespace tests { namespace site_scenarios {
using namespace fixture;

TEST_CASE("scenario: a dawn watering serves the trays one at a time") {
    ScenarioTrace::scenario("A dawn watering",
        "The end of a mild night, every tray owed water: one cycle per tray, "
        "and the day's allowance closing the slot behind them.");
    Plant p;
    plumbing(p, SOUND_CIRCUIT);
    force_phase(p, Site::PHASE_DAWN_BREEZE);
    force_rise(p, -30);
    settle(p, MILD_DAWN, 300);
    trace_weather(MILD_DAWN, Site::PHASE_DAWN_BREEZE);

    SAY("the end of the night, and 13 degC outside clears the dawn gate: the "
        "cycle takes the first tray owed water and opens the inlet on it");
    REQUIRE(run_until(p, cycle_filling, 600) != 0);
    CHECK(p.num("irr_activeTray") == doctest::Approx(1.0f));
    CHECK(p.on("irr_input_ro1"));
    CHECK(p.on("irr_tray1"));
    SAY("...and only that tray: one cycle can never water two");
    CHECK_FALSE(p.on("irr_output_ro1"));
    CHECK_FALSE(p.on("irr_tray2"));

    SAY("the dose is the tray's SUMMER figure scaled down by the weather, and "
        "damp air at dawn is a long way from summer");
    p.weather_for(2);        // the dose is published against the tray in hand
    const float dose = p.num("irr_volume_L");
    CHECK(dose > 0.0f);
    CHECK(dose < 150.0f);

    settle_meters(p);
    SAY("...and one end of the circuit is open: water arriving at the inlet "
        "meter, and the drain shut behind the tray");
    CHECK(p.num("irr_inFlow_Lpm") > 0.0f);
    CHECK(p.num("irr_outFlow_Lpm") == doctest::Approx(0.0f));
    snapshot_water(p, "filling the first tray");

    SAY("the fill ends at the DOSE and not at a time: the meter says when, and "
        "nothing is blamed for a fill that arrived");
    REQUIRE(run_until(p, cycle_soaking, 4000) != 0);
    CHECK(p.num("irr_delivered_L") >= dose);
    CHECK_FALSE(p.on("alarm_irr_fill"));

    SAY("...and the tray keeps it: both ends of the manifold shut for the soak, "
        "the tray still connected, and neither meter reading anything");
    CHECK_FALSE(p.on("irr_input_ro1"));
    CHECK_FALSE(p.on("irr_output_ro1"));
    CHECK(p.on("irr_tray1"));
    settle_meters(p);
    CHECK(p.num("irr_inFlow_Lpm") == doctest::Approx(0.0f));
    CHECK(p.num("irr_outFlow_Lpm") == doctest::Approx(0.0f));
    snapshot_water(p, "the tray sitting full through its soak");

    SAY("then the drain opens on the same tray");
    REQUIRE(run_until(p, cycle_draining, 6000) != 0);
    SAY("draining: the inlet shut, the tray still open, the drain carrying "
        "what comes back past the outlet meter");
    CHECK_FALSE(p.on("irr_input_ro1"));
    CHECK(p.on("irr_tray1"));
    CHECK(p.on("irr_output_ro1"));
    SAY("...and the day is stamped at the END OF THE FILL, so a fault during "
        "the long drain cannot water the tray twice");
    CHECK(p.num("irr_servedToday") == doctest::Approx(1.0f));

    // Ten seconds in, so the inlet's own 5 s rate window lies wholly inside
    // the drain: at the transition beat it is still reporting the fill.
    const float in_at_drain = p.num("irr_inTotal_L");
    settle_meters(p);
    SAY("water is leaving and none is arriving: one meter reads and the other "
        "does not, which is what a drain looks like at the two ends");
    CHECK(p.num("irr_outFlow_Lpm") > 0.0f);
    CHECK(p.num("irr_inFlow_Lpm") == doctest::Approx(0.0f));
    CHECK(p.num("irr_inTotal_L") == doctest::Approx(in_at_drain));
    snapshot_water(p, "draining the first tray");

    SAY("enough came back for the tray to be clear of a clog");
    REQUIRE(run_until(p, cycle_flushing, 12000) != 0);
    CHECK_FALSE(p.on("alarm_irr_drain"));
    SAY("...and the manifold is rinsed with no tray between the two ends, so "
        "what sat in the line is not what the next tray gets first");
    CHECK(p.on("irr_input_ro1"));
    CHECK(p.on("irr_output_ro1"));
    CHECK(p.num("irr_activeTray") == doctest::Approx(0.0f));

    const float in_at_flush  = p.num("irr_inTotal_L");
    const float out_at_flush = p.num("irr_outTotal_L");
    settle_meters(p, 80);
    SAY("both meters read, and read the SAME thing: fresh water going in one "
        "end of the manifold and out of the other, with no tray to hold it");
    CHECK(p.num("irr_inFlow_Lpm") > 20.0f);
    CHECK(fabsf(p.num("irr_inFlow_Lpm") - p.num("irr_outFlow_Lpm")) < 15.0f);
    const float went_in   = p.num("irr_inTotal_L") - in_at_flush;
    const float came_back = p.num("irr_outTotal_L") - out_at_flush;
    CHECK(went_in > 0.0f);
    CHECK(fabsf(went_in - came_back) < 0.25f * went_in);
    snapshot_water(p, "rinsing the manifold between two trays");

    SAY("then the second tray, because tray 1 is no longer owed anything");
    REQUIRE(run_until(p, second_tray, 12000) != 0);
    CHECK(p.on("irr_tray2"));
    CHECK_FALSE(p.on("irr_tray1"));

    SAY("and there it stops: irr_traysPerDay is the day's allowance, and the "
        "trays still owed water wait for tomorrow");
    REQUIRE(run_until(p, day_spent, 40000) != 0);
    CHECK(p.num("irr_servedToday") == doctest::Approx(2.0f));
    p.weather_for(3000);
    CHECK(irr_state(p) == Site::IRR_IDLE);
    CHECK(p.num("irr_servedToday") == doctest::Approx(2.0f));
    snapshot_water(p, "the day's allowance spent");
}

TEST_CASE("scenario: the two forces an operator has, and what spends them") {
    ScenarioTrace::scenario("Forcing a tray, and holding one back",
        "Both forces are ONE-SHOT: they change the NEXT cycle and are spent "
        "doing it, or they would ask for the same thing every morning for "
        "ever and no other tray would be reached.");
    Plant p;
    plumbing(p, SOUND_CIRCUIT);
    force_phase(p, Site::PHASE_DAWN_BREEZE);
    force_rise(p, -30);
    // Before the plant is stepped: the slot is open, so the first beat of the
    // settle is already a cycle looking for a tray.
    SAY("tray 5 is forced on. Every tray is owed water and the schedule would "
        "have taken the first, so the tray that goes is the one asked for");
    REQUIRE(p.f.bank.var(p.ref("irr_forceOn_tray5")).param_set(true)
            == Vars::SET_OK);
    settle(p, MILD_DAWN, 300);
    trace_weather(MILD_DAWN, Site::PHASE_DAWN_BREEZE);
    REQUIRE(run_until(p, filling_five, 1200) != 0);
    CHECK(p.on("irr_tray5"));
    settle_meters(p);
    CHECK_FALSE(p.on("irr_tray1"));
    CHECK(p.on("irr_forceOn_tray5"));
    snapshot_water(p, "filling the tray that was asked for");

    SAY("the force is spent as the fill completes, and owed to flash with the "
        "rest of the schedule");
    REQUIRE(run_until(p, cycle_draining, 6000) != 0);
    CHECK_FALSE(p.on("irr_forceOn_tray5"));
    CHECK(p.f.bank.var(p.ref("irr_forceOn_tray5")).param_owed());

    SAY("...so the day's second cycle goes back to the schedule, which has "
        "been asking for tray 1 all along");
    REQUIRE(run_until(p, filling_one, 24000) != 0);
    CHECK(p.on("irr_tray1"));
    settle_meters(p);
    SAY("...and the drain is shut behind it: a tray being filled has one end "
        "open, and the meter at the other end reads nothing");
    CHECK(p.num("irr_outFlow_Lpm") == doctest::Approx(0.0f));
    snapshot_water(p, "back to the schedule for the second cycle");

    Plant q;
    plumbing(q, SOUND_CIRCUIT);
    force_phase(q, Site::PHASE_DAWN_BREEZE);
    force_rise(q, -30);
    SAY("the other force, on a fresh plant: tray 1 is due and held back by "
        "hand, so the cycle passes over it and takes the next one owed");
    REQUIRE(q.f.bank.var(q.ref("irr_forceOff_tray1")).param_set(true)
            == Vars::SET_OK);
    settle(q, MILD_DAWN, 300);
    REQUIRE(run_until(q, cycle_filling, 1200) != 0);
    CHECK(q.num("irr_activeTray") == doctest::Approx(2.0f));
    settle_meters(q);

    SAY("the skip is spent doing it, and the tray is STAMPED as though it had "
        "been watered - so it waits its period rather than arriving again "
        "tomorrow with the flag gone");
    CHECK_FALSE(q.on("irr_forceOff_tray1"));
    CHECK(q.num("irr_last_tray1") > 0.0f);
    snapshot_water(q, "the held-back tray passed over");
}

TEST_CASE("scenario: a cold dawn waits for the morning rather than watering") {
    ScenarioTrace::scenario("A cold dawn",
        "8 degC outside at the end of the night: watering cold roots is what "
        "the dawn gate exists to prevent, and the morning slot is where that "
        "watering goes instead.");
    Plant p;
    plumbing(p, SOUND_CIRCUIT);
    force_phase(p, Site::PHASE_DAWN_BREEZE);
    force_rise(p, -30);
    settle(p, COLD_DAWN, 300);
    trace_weather(COLD_DAWN, Site::PHASE_DAWN_BREEZE);

    SAY("the end of the night, and too cold: the trays are owed water and the "
        "cycle does not start anyway");
    p.weather_for(1200);
    CHECK(irr_state(p) == Site::IRR_IDLE);
    CHECK(p.num("irr_servedToday") == doctest::Approx(0.0f));
    CHECK_FALSE(p.on("irr_input_ro1"));
    snapshot_water(p, "cold, at the end of the night");

    SAY("the sun comes up and the day's own slot opens: the same tray, the "
        "same dose, an hour and a half later");
    force_phase(p, Site::PHASE_DAY);
    force_rise(p, 90);
    REQUIRE(run_until(p, cycle_filling, 1200) != 0);
    CHECK(p.num("irr_activeTray") == doctest::Approx(1.0f));
    settle_meters(p);
    snapshot_water(p, "watering in the morning slot instead");

    SAY("...and the afternoon is not a slot at all: the cycle in hand finishes "
        "and no second one starts, because a tray filled then is still wet "
        "when the stomata open at dusk");
    force_rise(p, 400);
    REQUIRE(run_until(p, cycle_idle, 24000) != 0);
    CHECK(p.num("irr_servedToday") == doctest::Approx(1.0f));
    p.weather_for(3000);
    CHECK(irr_state(p) == Site::IRR_IDLE);
    CHECK(p.num("irr_servedToday") == doctest::Approx(1.0f));
    snapshot_water(p, "the afternoon, with trays still owed water");
}

TEST_CASE("scenario: damp air waters less than dry air, and less often") {
    ScenarioTrace::scenario("Damp air against dry",
        "The same morning twice, at 92 % and at 25 %: the modulation is the "
        "whole of this greenhouse's seasons, and it moves both the dose and "
        "the interval.");
    Plant damp;
    plumbing(damp, SOUND_CIRCUIT);
    force_phase(damp, Site::PHASE_DAY);
    force_rise(damp, 90);
    settle(damp, DAMP_MORNING, 300);
    trace_weather(DAMP_MORNING, Site::PHASE_DAY);
    REQUIRE(run_until(damp, cycle_filling, 1200) != 0);
    damp.weather_for(2);     // the dose is published against the tray in hand
    snapshot_water(damp, "the damp morning");

    Plant dry;
    plumbing(dry, SOUND_CIRCUIT);
    force_phase(dry, Site::PHASE_DAY);
    force_rise(dry, 90);
    settle(dry, DRY_MORNING, 300);
    REQUIRE(run_until(dry, cycle_filling, 1200) != 0);
    dry.weather_for(2);
    snapshot_water(dry, "the dry morning");

    SAY("the air the plants have been breathing is the input, and the damp "
        "house is nowhere near its summer figure");
    CHECK(damp.num("irr_vpdAvg_kPa") < dry.num("irr_vpdAvg_kPa"));
    CHECK(damp.num("irr_mod") < dry.num("irr_mod"));

    SAY("the split spends that on BOTH halves: a smaller dose, and a longer "
        "wait for the next one");
    CHECK(damp.num("irr_volume_L") < dry.num("irr_volume_L"));
    CHECK(damp.num("irr_period_days") > dry.num("irr_period_days"));

    SAY("...and the floor is a floor: even at 92 % the tray still gets water");
    CHECK(damp.num("irr_volume_L") > 0.0f);
    CHECK(damp.num("irr_mod") >= damp.num("irr_modMin"));
}

TEST_CASE("scenario: a tray that will not drain names itself") {
    ScenarioTrace::scenario("A tray that will not drain",
        "The fill arrives and only a tenth of it comes back: a clog is a "
        "MEASUREMENT, made against the dose that tray actually got.");
    Plant p;
    plumbing(p, CLOGGED_TRAY);
    force_phase(p, Site::PHASE_DAWN_BREEZE);
    force_rise(p, -30);
    settle(p, MILD_DAWN, 300);
    trace_weather(MILD_DAWN, Site::PHASE_DAWN_BREEZE);

    REQUIRE(run_until(p, cycle_draining, 12000) != 0);
    settle_meters(p);
    SAY("the fill itself was fine: the water went in, and the alarm below is "
        "about where it went afterwards");
    CHECK_FALSE(p.on("alarm_irr_fill"));
    snapshot_water(p, "the tray filled, and now draining");

    SAY("the drain window closes with a tenth of the dose recovered, which is "
        "under what the tray owes even with the margin allowed");
    REQUIRE(run_until(p, drain_blamed, 12000) != 0);
    CHECK(p.num("irr_drained_L") < p.num("irr_volume_L"));
    settle_meters(p, 80);
    SAY("...and the per-tray row says WHICH one, so somebody goes to the right "
        "end of the plant");
    CHECK(p.on("alarm_irr_drain_tray1"));
    CHECK_FALSE(p.on("alarm_irr_drain_tray2"));
    snapshot_water(p, "the drain window closed on a clog");

    SAY("the cycle carries on regardless: a blocked tray is not a reason to "
        "leave the other seven dry");
    REQUIRE(run_until(p, second_tray, 12000) != 0);
    CHECK(p.on("irr_tray2"));
    settle_meters(p);
    snapshot_water(p, "moving on to the next tray anyway");
}

TEST_CASE("scenario: the two things the inlet meter can see on its own") {
    ScenarioTrace::scenario("A leak, and a main with no water in it",
        "Both are read off the inlet meter alone: water moving with every "
        "valve shut, and water not arriving with the inlet open.");
    Plant p;
    plumbing(p, LEAKING_MANIFOLD);
    force_phase(p, Site::PHASE_NIGHT);
    force_rise(p, -240);
    settle(p, MILD_DAWN, 300);
    trace_weather(MILD_DAWN, Site::PHASE_NIGHT);

    SAY("the middle of the night: no cycle is running, so every valve in the "
        "circuit is shut and anything the meter counts is going somewhere "
        "nobody opened");
    REQUIRE(irr_state(p) == Site::IRR_IDLE);
    REQUIRE(run_until(p, leaking, 1200) != 0);
    CHECK(p.num("irr_inFlow_Lpm") > 0.0f);
    snapshot_water(p, "a litre a minute with everything shut");

    SAY("the main is repaired, and gives no water at all: the mirror image, "
        "raised while the inlet is open and nothing is arriving");
    plumbing(p, DEAD_MAIN);
    force_phase(p, Site::PHASE_DAWN_BREEZE);
    REQUIRE(run_until(p, cycle_filling, 1200) != 0);
    p.weather_for(400);
    SAY("...and the leak is not one any more: the meter is counting into a "
        "tray somebody opened on purpose");
    CHECK_FALSE(p.on("alarm_water_leak"));
    CHECK(p.on("alarm_water_pressure"));
    snapshot_water(p, "filling, with nothing coming out of the main");

    SAY("and the fill gives up at its timeout rather than holding the inlet "
        "open on a fault, which is how a tray overflows");
    REQUIRE(run_until(p, fill_blamed, 12000) != 0);
    CHECK(p.on("alarm_irr_fill_tray1"));
    CHECK_FALSE(p.on("irr_input_ro1"));
    SAY("...and it goes STRAIGHT to the drain, past the soak: a soak steeps a "
        "tray in the dose it was given, and this one got nothing");
    CHECK(irr_state(p) == Site::IRR_DRAINING);
    SAY("...and a fill that delivered nothing gets no drain verdict either: a "
        "clog is a measurement, and there is nothing to measure");
    REQUIRE(run_until(p, cycle_flushing, 12000) != 0);
    CHECK_FALSE(p.on("alarm_irr_drain"));
    settle_meters(p, 80);
    snapshot_water(p, "the fill given up on, and no clog claimed");
}

}}  // namespace tests::site_scenarios
