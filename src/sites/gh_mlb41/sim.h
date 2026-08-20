#ifndef SITE_SIM_H
#define SITE_SIM_H

/**
 * @file sim.h
 * @brief HAND-WRITTEN - the gh_mlb41 simulated field: the data animator the
 *        simulator runs.
 *
 * Not generated: how a greenhouse *behaves* is judgement, not schema. It
 * animates THIS site's tables, so the simulator exercises the same layout the
 * hardware will.
 *
 * Header-only; the whole body is guarded by TARGET_SIMULATOR, so it is a
 * near-empty include on the OPTA / Arduino build, where the real field plays
 * this role. Site code must cross-compile, so keep the guard.
 */

#ifdef TARGET_SIMULATOR

#include <cstdint>
#include <cstdio>
#include <random>

#include "libbotanik.h"


namespace Site {

/// PRNG draw in [0, 1). Fixed seed keeps runs reproducible.
inline float frand() {
    static std::mt19937 rng(0xB0A11Cu);
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
}

/// Random walk: with prob p, nudge v by up to +/-step, clamped to [lo, hi].
inline float drift(float v, float step, float lo, float hi, float p) {
    if (frand() < p) {
        v += (2.0f * frand() - 1.0f) * step;
        if (v < lo) v = lo;
        if (v > hi) v = hi;
    }
    return v;
}

/// True with probability p - for rare discrete events (rain, bus dropouts).
inline bool chance(float p) { return frand() < p; }

/** @brief Where a bussed reading goes: into the DEVICE, not into the row. Set by
 *  the harness once the simulated field is up (botanik_sim's FieldBus); null on
 *  a build with no bus, where put() writes the row instead.
 *
 *  A hook rather than a direct call because this file is SITE code and must
 *  cross-compile for the OPTA: the harness reaches into the site, never the
 *  other way. */
typedef void (*FieldSink)(void* ctx, const Vars::Descriptor& d, float v);
inline FieldSink& field_sink() { static FieldSink fn = nullptr; return fn; }
inline void*&     field_ctx()  { static void* ctx = nullptr; return ctx; }

/** @brief Publish one reading as a driver would: value, timestamp and quality in
 *  one step, through the handle rather than the value array. */
inline void put(Vars::Bank& io, const char* id, float v, uint32_t now_ms) {
    Vars::Var h = io.var(id);
    const Vars::Descriptor& d = *h.meta();

    // A row on the FIELD BUS is not ours to write: we are the physical quantity,
    // so it goes into the device's register image and the MASTER fills the row
    // when it polls, with its own timestamp and quality.
    if (d.bind.transport == Vars::XP_MODBUS) {
        if (field_sink() != nullptr) field_sink()(field_ctx(), d, v);
        else h.publish(Scalar::F(v), now_ms);   // no bus: nothing else would
        return;
    }
    h.publish(Scalar::F(v), now_ms);
}

/**
 * @brief Stand in for the Modbus RTU master: publish plausible readings for the
 * two meters.
 *
 * Only the signals worth watching are animated; the rest of the table stays
 * invalid, so it reads as "no data" rather than as a fabricated measurement.
 */
inline void publish_meters(Vars::Bank& io, uint32_t now_ms) {
    const float t = (float)now_ms / 1000.0f;

    static float v_drift = 0.0f, load = 0.0f;
    v_drift = drift(v_drift, 0.4f, -1.0f, 1.0f, 0.05f) * 0.99f;
    load    = drift(load,    0.5f,  0.0f, 1.0f, 0.04f);

    const float ln = 230.0f + 6.0f * v_drift;          // ~224..236 V
    const float amps = 4.0f + 28.0f * load;
    const float pf = 0.88f + 0.10f * load;
    const float watts = ln * amps * pf * 3.0f;

    put(io, "grid_L1_V", ln + 1.2f, now_ms);
    put(io, "grid_L2_V", ln - 0.8f, now_ms);
    put(io, "grid_L3_V", ln + 0.3f, now_ms);
    put(io, "grid_LN_V", ln, now_ms);
    put(io, "grid_LL_V", ln * 1.732f, now_ms);
    put(io, "grid_L1_A", amps / 3.0f, now_ms);
    put(io, "grid_L2_A", amps / 3.0f, now_ms);
    put(io, "grid_L3_A", amps / 3.0f, now_ms);
    put(io, "grid_avg_A", amps / 3.0f, now_ms);
    put(io, "grid_total_A", amps, now_ms);
    put(io, "grid_total_W", watts, now_ms);
    put(io, "grid_total_VA", watts / pf, now_ms);
    put(io, "grid_total_PF", pf, now_ms);
    // Mains frequency wanders in the last decimal, as a real one does.
    put(io, "grid_Hz", 50.0f + 0.03f * sinf(t / 37.0f), now_ms);

    // The transfer switch sees the same supply, lightly loaded.
    put(io, "ats_V",  ln - 0.5f, now_ms);
    put(io, "ats_A",  amps * 0.15f, now_ms);
    // Active power from the same three numbers the meter derives it from, so
    // the reading stays consistent with the volts and amps beside it.
    put(io, "ats_W",  (ln - 0.5f) * amps * 0.15f * pf, now_ms);
    put(io, "ats_Hz", 50.0f + 0.03f * sinf(t / 37.0f), now_ms);
}

/**
 * @brief The calibrated channels on TDC/AI-1: the four voltage dividers and the
 * output flow loop.
 *
 * These are the only rows in the table that SCALE, so animating them is what
 * puts the calibration under test every run: the quantity goes in here in
 * engineering units, the simulated transducer reports it as a count, and the
 * master has to scale it back. A row reading its own animated value is the
 * whole round trip.
 */
inline void publish_analog(Vars::Bank& io, uint32_t now_ms) {
    const float t = (float)now_ms / 1000.0f;
    static float ripple = 0.0f;
    ripple = drift(ripple, 0.3f, -1.0f, 1.0f, 0.03f);

    // Both supplies sit near 24 V, the bands being 20..30.
    put(io, "pwr_dc1_V",     24.0f + 0.8f * ripple, now_ms);
    put(io, "pwr_dc2_V",     24.1f - 0.6f * ripple, now_ms);
    // The UPS battery floats a little higher; the generator start battery rests
    // lower and is the one worth watching.
    put(io, "pwr_upsBat_V",  26.8f + 0.3f * ripple, now_ms);
    put(io, "pwr_genBat_V",  25.4f + 0.5f * ripple, now_ms);
    // Output flow: quiet most of the time, with a slow swell (band 0..50 L/min).
    put(io, "irr_out_Lpm",   12.0f + 8.0f * sinf(t / 53.0f), now_ms);

    // The UPS low-battery contact, through the TERMINAL rather than the row:
    // the local input scan is what publishes it on hardware, and driving the
    // row directly here would step over the thing under test. Open, because
    // the simulated plant is on mains - force the row to see the lockout.
    HAL::DigitalIn::inject(0, false);
}

/** @brief The weather station: wind with gusts, showers, and a diurnal
 *  irradiance taken off the same wall clock the sun tracker reads.
 *
 *  Without it the four rows never answer, the sky reads as unmeasured, and the
 *  roof is locked shut for ever - which is the interlock doing its job on a
 *  field that was not there.
 */
inline void publish_weather(Vars::Bank& io, uint32_t now_ms) {
    static float    wind    = 1.5f;
    static uint32_t rain_ms = 0;
    static uint32_t last_ms = 0;

    const uint32_t dt = (now_ms > last_ms) ? (now_ms - last_ms) : 0;
    last_ms = now_ms;

    // Mostly under cli_windShut_mps, with gusts that occasionally are not: the
    // lockout is meant to be exercised, not avoided.
    wind = drift(wind, 0.4f, 0.2f, 4.0f, 0.05f);
    put(io, "cli_wind_mps", wind, now_ms);
    put(io, "cli_windGust_mps", wind * 1.5f, now_ms);

    // Showers rather than a rain rate that wanders: a shower starts rarely and
    // then lasts, which is what the lockout's overrun is written against.
    if (rain_ms > dt)      rain_ms -= dt;
    else if (rain_ms > 0)  rain_ms = 0;
    else if (chance(0.0004f)) rain_ms = 180000;
    put(io, "cli_rain_mmph", (rain_ms > 0) ? 4.0f : 0.0f, now_ms);

    // A day is a day: the same UTC clock the sun tracker works from, so dawn
    // on this row and PHASE_DAWN_BREEZE are the same morning.
    const uint32_t sec = (uint32_t)((HAL::Clock::wall_ms() / 1000ull) % 86400ull);
    const float noon = sinf(((float)sec / 86400.0f - 0.25f) * 6.2831853f);
    put(io, "cli_irrad_Wpm2", (noon > 0.0f) ? 950.0f * noon : 0.0f, now_ms);
}

/** @brief The inlet water meter: a pulse train, and only while the inlet valve
 *  is open. Through the CHANNEL, like the contact above - the counter is what
 *  the irrigation cycle reads, and publishing litres here would step over it.
 *
 *  ~20 L/min at one pulse per litre, so a tray's dose arrives in the tens of
 *  seconds a real fill takes.
 */
inline void pulse_inlet(const Vars::Bank& io, uint32_t now_ms) {
    static uint32_t owed_ms = 0;
    static uint32_t last_ms = 0;

    const uint32_t dt = (now_ms > last_ms) ? (now_ms - last_ms) : 0;
    last_ms = now_ms;
    if (!io.var("irr_input_ro1").b()) { owed_ms = 0; return; }

    // 3000 ms per litre is 20 L/min. Accumulated, so a slow beat still meters
    // the water it was open for.
    owed_ms += dt;
    const uint32_t pulses = owed_ms / 3000u;
    owed_ms -= pulses * 3000u;
    if (pulses > 0) HAL::Pulse::inject(Site::pulse_inputs[0].channel, pulses);
}

/**
 * @brief The twelve Temp/RH sensors: exterior, crawlspace, low and high.
 *
 * Animated because the rows exist: without this the field bus answers zeroed
 * registers and every sensor reads a perfectly VALID 0.0 C, which looks like a
 * working instrument and is not a measurement.
 *
 * Shaped so the four groups say something a greenhouse would: the exterior
 * swings hardest, the crawlspace barely moves, high sits above low. RH runs
 * opposite to temperature.
 */
inline void publish_climate(Vars::Bank& io, uint32_t now_ms) {
    const float t = (float)now_ms / 1000.0f;
    // One slow "day", fast enough to watch: a full cycle every four minutes.
    const float day = sinf(t / 38.0f);

    struct Group {
        const char* temp;   ///< printf pattern, one %u for the sensor number
        const char* rh;
        float       base;   ///< mid-point temperature, C
        float       swing;  ///< how far it moves over the day
    };
    const Group groups[] = {
        { "cli_temp_ext%u", "cli_rh_ext%u",  12.0f, 9.0f },   // outside, wide swing
        { "cli_temp_cs%u",  "cli_rh_cs%u",   14.0f, 1.5f },   // crawlspace, damped
        { "cli_temp_lo%u",  "cli_rh_lo%u",   19.0f, 4.0f },   // canopy level
        { "cli_temp_hi%u",  "cli_rh_hi%u",   22.0f, 5.0f },   // ridge, warmer
    };

    for (uint8_t g = 0; g < 4; g++) {
        for (uint8_t n = 1; n <= 3; n++) {
            // Each sensor of a group sees the same weather, offset a little:
            // three instruments in one space never agree exactly.
            const float spread = ((float)n - 2.0f) * 0.4f;
            const float temp = groups[g].base + groups[g].swing * day + spread;
            // Warmer air, drier reading; kept inside the sensor's 0-100 band.
            float rh = 68.0f - 1.8f * (temp - groups[g].base) + spread;
            if (rh < 5.0f)  rh = 5.0f;
            if (rh > 99.0f) rh = 99.0f;

            char name[24];
            snprintf(name, sizeof name, groups[g].temp, (unsigned)n);
            put(io, name, temp, now_ms);
            snprintf(name, sizeof name, groups[g].rh, (unsigned)n);
            put(io, name, rh, now_ms);
        }
    }
}


// Entry point

/**
 * @brief Stand in for the field: publish meter readings, drive the coils, and
 * raise alarms. Called straight from the sim harness.
 */
inline void simulate(Vars::Bank& io, uint32_t now_ms) {
    publish_meters(io, now_ms);
    publish_climate(io, now_ms);
    publish_analog(io, now_ms);
    publish_weather(io, now_ms);
    pulse_inlet(io, now_ms);
}

}  // namespace Site

#endif // TARGET_SIMULATOR

#endif // SITE_SIM_H
