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

#include <cstdint>      // uint32_t/uint64_t
#include <cstdio>       // snprintf - the sensor names are patterned
#include <random>       // std::mt19937 + distributions

#include "libbotanik.h" // Vars::Bank


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

/** @brief One mock alarm cause: the row it drives, and whether it is on right
 *  now. Named rather than positional - the animator drives only the conditions
 *  it has a story for and must leave every other alarm row alone, including the
 *  platform-health rows the firmware raises itself. */
struct MockAlarm { const char* name; bool on; };

/** @brief Where a bussed reading goes: into the DEVICE, not into the row. Set by
 *  the harness once the simulated field is up (botanik_sim's FieldBus); null on
 *  a build with no bus, where put() writes the row instead.
 *
 *  A hook rather than a direct call because this file is SITE code and must
 *  cross-compile for the OPTA: the harness reaches into the site, never the
 *  other way. */
typedef void (*FieldSink)(void* ctx, const Vars::Variable& d, float v);
inline FieldSink& field_sink() { static FieldSink fn = nullptr; return fn; }
inline void*&     field_ctx()  { static void* ctx = nullptr; return ctx; }

/** @brief Publish one reading as a driver would: value, timestamp and quality in
 *  one step, through the handle rather than the value array. */
inline void put(Vars::Bank& io, const char* id, float v, uint32_t now_ms) {
    Vars::Var h = io.var(id);
    const Vars::Variable& d = *h.meta();

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

/**
 * @brief Drive the relay coils, so the climate and irrigation groups show
 * something moving.
 *
 * A caricature of a control law: the console reads these through the same
 * handles a real program writes them with.
 */
inline void drive_coils(Vars::Bank& io, uint32_t now_ms) {
    const float t = (float)now_ms / 1000.0f;

    // Windows track a slow thermal cycle; open and close are interlocked, as
    // the real drives are (energising both would fight the motor).
    static float thermal = 0.0f;
    thermal = drift(thermal, 0.3f, -1.0f, 1.0f, 0.05f) * 0.99f;
    const bool opening = thermal > 0.35f;
    const bool closing = thermal < -0.35f;

    const char* const opn[] = { "cli_window1_opn", "cli_window2_opn",
                             "cli_window3_opn", "cli_window4_opn" };
    const char* const clo[] = { "cli_window1_clo", "cli_window2_clo",
                             "cli_window3_clo", "cli_window4_clo" };
    for (uint8_t i = 0; i < 4; i++) {
        io.var(opn[i]).set(opening);
        io.var(clo[i]).set(closing);
    }

    // Destratification fans step up with the thermal load.
    const char* const fans[] = { "cli_destrat1_speed1", "cli_destrat1_speed2",
                              "cli_destrat1_speed3" };
    const int speed = (thermal > 0.6f) ? 3 : (thermal > 0.2f) ? 2 : (thermal > -0.2f) ? 1 : 0;
    for (uint8_t i = 0; i < 3; i++) {
        io.var(fans[i]).set((speed == (int)i + 1));
    }

    io.var("cli_screen").set(thermal < -0.5f);

    // Irrigation walks the trays, one at a time, ~40 s each.
    const char* const trays[] = { "irr_tray1", "irr_tray2", "irr_tray3", "irr_tray4",
                               "irr_tray5", "irr_tray6", "irr_tray7", "irr_tray8" };
    const int active = ((int)(t / 40.0f)) % 8;
    for (uint8_t i = 0; i < 8; i++) {
        io.var(trays[i]).set(((int)i == active));
    }
    io.var("irr_input_ro1").set((active < 4));
    io.var("irr_output_ro1").set((active >= 4));
}

/**
 * @brief Raise and clear alarm conditions.
 *
 * Triggers read the same variables the console shows, so an alarm on screen
 * always has a visible cause in the monitor table. The climate conditions have
 * no instrumentation mapped yet and rest on a slow drift instead.
 */
inline void publish_alarms(Vars::Bank& io, uint32_t now_ms) {
    // Real causes, read back through the variable tables.
    const float ln  = io.var("grid_LN_V").value();
    const float pf  = io.var("grid_total_PF").value();
    const float hz  = io.var("grid_Hz").value();

    // No sensor mapped for these yet: a slow random walk stands in, so the
    // alarms page and its dwell/ack machinery still have something to show.
    static float weather = 0.0f, plant = 0.0f;
    weather = drift(weather, 0.30f, -1.0f, 1.0f, 0.03f) * 0.99f;
    plant   = drift(plant,   0.30f, -1.0f, 1.0f, 0.03f) * 0.99f;

    MockAlarm mock[] = {
        { "alarm_ac_grid",   ln < 226.0f || ln > 234.0f },
        { "alarm_ac_local",  hz < 49.9f  || hz > 50.1f  },
        { "alarm_dc_ps1",    pf < 0.90f                 },
        { "alarm_weather_storm", weather >  0.80f },
        { "alarm_too_hot",       plant   >  0.85f },
        { "alarm_too_cold",      plant   < -0.85f },
        { "alarm_too_humid",     weather < -0.85f },
    };
    const uint8_t N = (uint8_t)(sizeof(mock) / sizeof(mock[0]));

    // Settle window: keep alarms clear for the first stretch after boot, as a
    // real plant masks process alarms until its sensors settle.
    const uint32_t SETTLE_MS = 120000;   // 2 min

    // Debounce: hold each cause ON for a minimum dwell so a signal hovering at
    // its threshold cannot strobe the ribbon.
    static uint32_t hold_ms[16] = {0};
    static uint32_t prev_ms = 0;
    const uint32_t dt = (prev_ms == 0) ? 0 : (now_ms - prev_ms);
    prev_ms = now_ms;
    const uint32_t MIN_HOLD_MS = 10000;   // ~10 s minimum visible dwell

    for (uint8_t i = 0; i < N; i++) {
        const bool on = mock[i].on && now_ms >= SETTLE_MS;
        if (on)                     hold_ms[i] = MIN_HOLD_MS;
        else if (hold_ms[i] > dt)   hold_ms[i] -= dt;
        else                        hold_ms[i] = 0;
        // By name, and only the rows above: an alarm this animator has no story
        // for stays where it is. Clearing it every beat would fight the health
        // monitor that owns the platform rows in the same table.
        io.var(mock[i].name).set(on || hold_ms[i] > 0);
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

/**
 * @brief Stand in for the field: publish meter readings, drive the coils, and
 * raise alarms. Called straight from the sim harness.
 */
inline void simulate(Vars::Bank& io, uint32_t now_ms) {
    publish_meters(io, now_ms);
    publish_climate(io, now_ms);
    drive_coils(io, now_ms);
    publish_alarms(io, now_ms);
}

}  // namespace Site

#endif // TARGET_SIMULATOR

#endif // SITE_SIM_H
