#ifndef SITE_SIM_H
#define SITE_SIM_H

/**
 * @file sim.h
 * @brief HAND-WRITTEN - the gh_mlb41 simulated field: the data animator the
 *        simulator runs, and the timeout dilation.
 *
 * Not generated: how a greenhouse *behaves* is judgement, not schema, so this
 * file is yours. What matters for the interface is that it animates THIS site's
 * generated payload, so the simulator exercises the same layout, tables and
 * offsets the hardware will.
 *
 * Header-only; the whole body is guarded by TARGET_SIMULATOR, so it is a
 * near-empty include on the OPTA / Arduino build, where the real field plays
 * this role. The floats here only shape the mock signal.
 */

#ifdef TARGET_SIMULATOR

#include <cstdint>
#include <cstring>  // strncpy (config host strings)
#include <cmath>    // pow
#include <random>


#include "botanik.h"

#include "alarms.h"                     // g_alarms - the one alarm definition

/**
 * @brief Grow a base timeout (ms) by time-dilation factor S, so jitter
 * (amplified by a high BOTANIK_TIME_SCALE) can't false-trip the link. Clamped
 * below 2^31 to keep the 32-bit elapsed compares rollover-safe. No-op when
 * S == 1.
 */
inline uint32_t dilated_timeout(uint32_t base_ms) {
    uint32_t s = Botanik::HAL::_time_scale();
    if (s <= 1) return base_ms;
    double scaled = (double)base_ms * std::pow((double)s, 3);
    const double cap = 2147483647.0;  // < 2^31
    return scaled >= cap ? (uint32_t)cap : (uint32_t)scaled;
}

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

/** @brief Simulated alarm bit for a table code (0 if undefined).
 *  Resolves through g_alarms so the mock bitset uses the firmware's bit
 *  layout. */
inline uint64_t abit(const char* name) {
    int i = Botanik::Alarms::alarm_index(g_alarms, g_alarm_count, name);
    return i >= 0 ? Botanik::Alarms::alarm_bit((uint8_t)i) : 0;
}

/** @brief Publish one reading as a driver would: value, timestamp, valid in
 *  one step. Through the handle, not the value array - writing the array
 *  directly is how the simulated field silently drifted out of step with the
 *  model, since it would not fail to compile when the model changed. */
inline void put(Vars::Bank& io, uint16_t id, float v, uint32_t now_ms) {
    io.var(id).publish(Scalar::F(v), now_ms);
}

/**
 * @brief Stand in for the Modbus RTU master: publish plausible readings for the
 * two meters.
 *
 * Only the signals worth watching are animated; the rest of the table stays
 * invalid, which is honest - it reads as "no data" on the monitor page rather
 * than as a fabricated measurement.
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

    put(io, V_PWR_L1_V, ln + 1.2f, now_ms);
    put(io, V_PWR_L2_V, ln - 0.8f, now_ms);
    put(io, V_PWR_L3_V, ln + 0.3f, now_ms);
    put(io, V_PWR_LN_V, ln, now_ms);
    put(io, V_PWR_LL_V, ln * 1.732f, now_ms);
    put(io, V_PWR_L1_A, amps / 3.0f, now_ms);
    put(io, V_PWR_L2_A, amps / 3.0f, now_ms);
    put(io, V_PWR_L3_A, amps / 3.0f, now_ms);
    put(io, V_PWR_AVG_A, amps / 3.0f, now_ms);
    put(io, V_PWR_TOTAL_A, amps, now_ms);
    put(io, V_PWR_TOTAL_W, watts, now_ms);
    put(io, V_PWR_TOTAL_VA, watts / pf, now_ms);
    put(io, V_PWR_TOTAL_PF, pf, now_ms);
    // Mains frequency wanders in the last decimal, as a real one does.
    put(io, V_PWR_HZ, 50.0f + 0.03f * sinf(t / 37.0f), now_ms);

    // The transfer switch sees the same supply, lightly loaded.
    put(io, V_ATS_V,  ln - 0.5f, now_ms);
    put(io, V_ATS_A,  amps * 0.15f, now_ms);
    put(io, V_ATS_HZ, 50.0f + 0.03f * sinf(t / 37.0f), now_ms);
}

/**
 * @brief Drive the relay coils, so the climate and irrigation groups show
 * something moving.
 *
 * A caricature of a control law, not one: the point is that the console reads
 * these through the same handles a real program writes them with.
 */
inline void drive_coils(Vars::Bank& io, uint32_t now_ms) {
    const float t = (float)now_ms / 1000.0f;

    // Windows track a slow thermal cycle; open and close are interlocked, as
    // the real drives are (energising both would fight the motor).
    static float thermal = 0.0f;
    thermal = drift(thermal, 0.3f, -1.0f, 1.0f, 0.05f) * 0.99f;
    const bool opening = thermal > 0.35f;
    const bool closing = thermal < -0.35f;

    const uint16_t opn[] = { V_CLI_WINDOW1_OPN, V_CLI_WINDOW2_OPN,
                             V_CLI_WINDOW3_OPN, V_CLI_WINDOW4_OPN };
    const uint16_t clo[] = { V_CLI_WINDOW1_CLO, V_CLI_WINDOW2_CLO,
                             V_CLI_WINDOW3_CLO, V_CLI_WINDOW4_CLO };
    for (uint8_t i = 0; i < 4; i++) {
        io.var(opn[i]).set(opening);
        io.var(clo[i]).set(closing);
    }

    // Destratification fans step up with the thermal load.
    const uint16_t fans[] = { V_CLI_DESTRAT1_SPEED1, V_CLI_DESTRAT1_SPEED2,
                              V_CLI_DESTRAT1_SPEED3 };
    const int speed = (thermal > 0.6f) ? 3 : (thermal > 0.2f) ? 2 : (thermal > -0.2f) ? 1 : 0;
    for (uint8_t i = 0; i < 3; i++) {
        io.var(fans[i]).set((speed == (int)i + 1));
    }

    io.var(V_CLI_SCREEN).set(thermal < -0.5f);

    // Irrigation walks the trays, one at a time, ~40 s each.
    const uint16_t trays[] = { V_IRR_TRAY1, V_IRR_TRAY2, V_IRR_TRAY3, V_IRR_TRAY4,
                               V_IRR_TRAY5, V_IRR_TRAY6, V_IRR_TRAY7, V_IRR_TRAY8 };
    const int active = ((int)(t / 40.0f)) % 8;
    for (uint8_t i = 0; i < 8; i++) {
        io.var(trays[i]).set(((int)i == active));
    }
    io.var(V_IRR_INPUT_RO1).set((active < 4));
    io.var(V_IRR_OUTPUT_RO1).set((active >= 4));
}

/**
 * @brief Raise and clear alarm conditions.
 *
 * Triggers read the same variables the console shows, so an alarm on screen
 * always has a visible cause in the monitor table. What used to drive this -
 * a mock payload of climate fields no sensor produced - is gone; the site has
 * no climate instrumentation mapped yet, so those conditions rest on their own
 * slow drift until it is.
 */
inline void publish_alarms(Vars::Bank& io, uint32_t now_ms) {
    uint64_t raw = 0;

    // Real causes, read back through the variable tables.
    const float ln  = io.var(V_PWR_LN_V).value();
    const float pf  = io.var(V_PWR_TOTAL_PF).value();
    const float hz  = io.var(V_PWR_HZ).value();
    if (ln < 226.0f || ln > 234.0f)  raw |= abit("AC_GRID");
    if (hz < 49.9f  || hz > 50.1f)   raw |= abit("AC_LOCAL");
    if (pf < 0.90f)                  raw |= abit("DC_PS1");

    // No sensor mapped for these yet: a slow random walk stands in, so the
    // alarms page and its dwell/ack machinery still have something to show.
    static float weather = 0.0f, plant = 0.0f;
    weather = drift(weather, 0.30f, -1.0f, 1.0f, 0.03f) * 0.99f;
    plant   = drift(plant,   0.30f, -1.0f, 1.0f, 0.03f) * 0.99f;
    if (weather >  0.80f) raw |= abit("WH_STORM");
    if (plant   >  0.85f) raw |= abit("TOO_HOT");
    if (plant   < -0.85f) raw |= abit("TOO_COLD");
    if (weather < -0.85f) raw |= abit("TOO_HUM");

    // Settle window: keep alarms clear for the first stretch after boot (real
    // plant likewise masks process alarms until its sensors settle). Kept
    // generous so the first alarm lands well after boot, not seconds in.
    const uint32_t SETTLE_MS = 120000;   // 2 min
    if (now_ms < SETTLE_MS) raw = 0;

    // Debounce: hold each raw bit ON for a minimum dwell so a signal hovering
    // at its threshold cannot strobe the ribbon. The firmware's per-alarm
    // min_on_s is the further local net.
    static uint32_t hold_ms[Botanik::Alarms::ALARM_BITS] = {0};
    static uint32_t prev_ms = 0;
    uint32_t dt = (prev_ms == 0) ? 0 : (now_ms - prev_ms);
    prev_ms = now_ms;
    const uint32_t MIN_HOLD_MS = 10000;   // ~10 s minimum visible dwell
    uint64_t debounced = 0;
    for (uint8_t i = 0; i < Botanik::Alarms::ALARM_BITS; i++) {
        bool on = ((raw >> i) & 1u) != 0;
        if (on)                     hold_ms[i] = MIN_HOLD_MS;
        else if (hold_ms[i] > dt)   hold_ms[i] -= dt;
        else                        hold_ms[i] = 0;
        if (on || hold_ms[i] > 0)   debounced |= Botanik::Alarms::alarm_bit(i);
    }

    // Scatter the debounced bitset into the variables the alarm table names
    // (row i -> g_alarms[i].ref), which is where the model reads them. On
    // hardware a control program drives the same internals.
    for (uint8_t i = 0; i < g_alarm_count; i++)
        io.var(g_alarms[i].ref).set(((debounced >> i) & 1u) != 0);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

/**
 * @brief Stand in for the field: publish meter readings, drive the coils, and
 * raise alarms.
 *
 * Called straight from the sim harness. It writes the variable tables - the
 * same ones a Modbus driver will write - so nothing downstream can tell the
 * difference.
 */
inline void simulate(Vars::Bank& io, uint32_t now_ms) {
    publish_meters(io, now_ms);
    drive_coils(io, now_ms);
    publish_alarms(io, now_ms);
}

}  // namespace Site

#endif // TARGET_SIMULATOR

#endif // SITE_SIM_H
