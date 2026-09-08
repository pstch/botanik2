#ifndef GH_MLB41_SIM_H
#define GH_MLB41_SIM_H

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

/** @brief True on average once every @p every_ms of SIMULATED time.
 *
 *  Per unit of TIME, never per beat: a probability per scan is a different rate
 *  at every scan period, and at a 10 ms beat a per-beat shower rains around the
 *  clock.
 */
inline bool chance_per(uint32_t dt_ms, float every_ms) {
    return every_ms > 0.0f && frand() < (float)dt_ms / every_ms;
}

/** @brief Random walk: nudge v by up to +/-step about once every @p every_ms,
 *  clamped to [lo, hi]. */
inline float drift(float v, float step, float lo, float hi,
                   uint32_t dt_ms, float every_ms) {
    if (chance_per(dt_ms, every_ms)) {
        v += (2.0f * frand() - 1.0f) * step;
        if (v < lo) v = lo;
        if (v > hi) v = hi;
    }
    return v;
}

/** @brief Where a bussed reading goes: into the DEVICE, not into the row. Set by
 *  the harness once the simulated field is up (botanik_sim's FieldBus); null on
 *  a build with no bus, where put() writes the row instead.
 *
 *  A hook rather than a direct call because this file is SITE code and must
 *  cross-compile for the OPTA: the harness reaches into the site, never the
 *  other way. */
typedef void (*FieldSink)(void* ctx, Vars::Var h, float v);
inline FieldSink& field_sink() { static FieldSink fn = nullptr; return fn; }
inline void*&     field_ctx()  { static void* ctx = nullptr; return ctx; }

/** @brief Take a bus device off the wire, or put it back.
 *
 *  A probe on the field bus cannot be silenced by not writing it: the device
 *  keeps answering with the register it already holds, so the row stays fresh
 *  and perfectly valid. The DEVICE has to stop answering. */
/** @brief The row @p id names. The simulator is the one place a NAME is still
 *  the handle: the mock is written in the site's vocabulary, several of its
 *  names are built per call, and none of it is on a beat that matters. */
inline Vars::Var at(const Vars::Bank& io, const char* id) {
    return io.var(io.find(id));
}

typedef void (*FieldFault)(void* ctx, uint8_t unit, bool absent);
inline FieldFault& field_fault() { static FieldFault fn = nullptr; return fn; }

/** @brief Silence the device a named row lives on (or restore it). Nothing to
 *  do where the row is not on a bus - there, not publishing IS the dropout. */
inline void silence(const Vars::Bank& io, const char* id, bool absent) {
    const Vars::Descriptor& d = at(io, id).meta();
    if (d.bind.device != 0 && field_fault() != nullptr)
        field_fault()(field_ctx(), d.bind.device, absent);
}

/** @brief Publish one reading as a driver would: value, timestamp and quality in
 *  one step, through the handle rather than the value array. */
inline void put(Vars::Bank& io, const char* id, float v, uint32_t now_ms) {
    Vars::Var h = at(io, id);
    const Vars::Descriptor& d = h.meta();

    // A row on the FIELD BUS is not ours to write: we are the physical quantity,
    // so it goes into the device's register image and the MASTER fills the row
    // when it polls, with its own timestamp and quality.
    if (d.bind.device != 0) {
        if (field_sink() != nullptr) field_sink()(field_ctx(), h, v);
        else h.publish(Scalar::F(v), now_ms);   // no bus: nothing else would
        return;
    }
    h.publish(Scalar::F(v), now_ms);
}

/**
 * @brief What kind of day the field is putting the plant through.
 *
 * The same weather the control-system scenarios are written against, but as a
 * whole DAY rather than a held step: BOTANIK_SIM_SCENARIO picks one and it
 * repeats every simulated day, which is what makes it smoke-testable.
 * `cycle`, the default, gives each a turn so an unattended run still sweeps
 * the state space.
 */
enum SimScenario {
    SCN_CYCLE = 0,   ///< a different one each simulated day
    SCN_FAIR,        ///< an ordinary day for the season
    SCN_HOT,         ///< a cloudless heat anomaly: the whole cooling ladder
    SCN_COLD,        ///< a hard clear night: the screen, and the negative half
    SCN_STORM,       ///< wind over cli_windShutEnd_mps and rain: the interlock
    SCN_DAMP,        ///< saturated air overnight: the rot guard
    SCN_DRY,         ///< hot and parched: the humidifier arm
    SCN_PROBES,      ///< the ridge probes dying, one at a time
    SCN_SUPPLY,      ///< the grid failing, the generator, then the UPS
    SCN_N
};

/** @brief The named scenarios, in enum order from SCN_CYCLE. */
inline const char* const* scenario_names() {
    static const char* const names[] = {
        "cycle", "fair", "hot", "cold", "storm", "damp", "dry", "probes",
        "supply"
    };
    return names;
}

/** @brief BOTANIK_SIM_SCENARIO, read once. An unknown name is `cycle`. */
inline SimScenario scenario() {
    static const SimScenario chosen = []() -> SimScenario {
        const char* want = HAL::Env::str("BOTANIK_SIM_SCENARIO", "cycle");
        for (uint8_t i = 0; i < (uint8_t)SCN_N; i++)
            if (Utils::compare_str(want, scenario_names()[i])) return (SimScenario)i;
        return SCN_CYCLE;
    }();
    return chosen;
}

/** @brief How long a new day's character takes to arrive, hours. */
static const float CHANGE_H = 3.0f;

/** @brief The one in force today: the chosen one, or the day's turn. */
inline SimScenario acting(uint32_t day) {
    const SimScenario s = scenario();
    if (s != SCN_CYCLE) return s;
    return (SimScenario)(SCN_FAIR + (day % (uint32_t)(SCN_N - SCN_FAIR)));
}

/**
 * @brief The sky, from one reading of the wall clock: the season, where the sun
 * is, and what kind of day it is.
 *
 * The sun tracker works from that same clock, so a temperature minimum at dawn
 * and PHASE_DAWN_BREEZE are the same morning by construction.
 * BOTANIK_SIM_EPOCH moves the date, which is how a season is reached.
 */
struct Sky {
    uint32_t dt_ms;    ///< simulated time since the last beat
    uint32_t day;      ///< days since the UNIX epoch, for the weather regime
    float    doy;      ///< day of the year, 0..365
    float    hour;     ///< UTC hour, 0..24
    float    noon;     ///< solar elevation at noon today, degrees
    float    elev;     ///< solar elevation now, degrees (negative at night)
    float    sun;      ///< 0..1 of a clear noon, this instant
    float    irrad;    ///< W/m2 on the glass
    float    cloud;    ///< 0 clear, 1 overcast
    float    t_mean;   ///< today's mean exterior temperature, C
    float    t_amp;    ///< half today's exterior swing, C
    float    dew_ofs;  ///< how far today's air sits from an ordinary humidity
    SimScenario act;   ///< the day's character
};

/** @brief What a scenario does to the weather: the cloud it fixes, and its
 *  offsets to the day's mean, its swing, and how moist the air is. */
inline void _character(SimScenario a, float drifting_cloud,
                       float* cloud, float* t_ofs, float* amp_ofs, float* dew) {
    *cloud = drifting_cloud; *t_ofs = 0.0f; *amp_ofs = 0.0f; *dew = 0.0f;
    switch (a) {
        case SCN_HOT:   *cloud = 0.05f; *t_ofs =  7.0f; *amp_ofs = 2.0f;
                        *dew   =  -3.0f;                           break;
        case SCN_COLD:  *cloud = 0.00f; *t_ofs = -6.0f; *dew =  -4.0f; break;
        case SCN_STORM: *cloud = 0.85f; *t_ofs = -2.0f; *dew =   2.0f; break;
        case SCN_DAMP:  *cloud = 0.90f; *t_ofs =  1.0f; *dew =   4.5f; break;
        case SCN_DRY:   *cloud = 0.05f; *t_ofs =  4.0f; *dew = -12.0f; break;
        default: break;              // FAIR, PROBES, SUPPLY: ordinary weather
    }
}

/// Site latitude, for the solar geometry. Matches geo_lat_deg's default.
static const float SIM_LAT_DEG = 47.96f;

inline float _rad(float d) { return d * 0.0174532925f; }

/** @brief Everything downstream, from the wall clock and nothing else.
 *
 *  Called ONCE a beat and handed to each publisher: recomputed per publisher,
 *  the cloud walk would step four times and no two rows would agree about the
 *  weather they were measuring. */
inline Sky sky_now(uint32_t now_ms) {
    Sky s;
    static uint32_t last_ms = 0;
    s.dt_ms  = (now_ms > last_ms) ? (now_ms - last_ms) : 0;
    last_ms  = now_ms;
    const uint64_t sec = HAL::Clock::wall_ms() / 1000ull;
    s.day  = (uint32_t)(sec / 86400ull);
    s.hour = (float)(sec % 86400ull) / 3600.0f;
    // Day of year off the day count: 1970-01-01 was a Jan 1, so the 365.2425
    // remainder is within a day or two of the calendar all century.
    s.doy  = fmodf((float)s.day + 0.5f, 365.2425f);
    s.act  = acting(s.day);

    // Its OWN solar geometry, deliberately, rather than reading sol_elev_deg:
    // a simulated field that took its weather from the firmware's sun tracker
    // could not expose a fault in that tracker. Good to about ten minutes of
    // the real thing, which is what a temperature profile needs.
    //
    // Declination, and the elevation the sun reaches at noon today: the season
    // in one number.
    const float decl = 23.44f * sinf(6.2831853f * (s.doy - 81.0f) / 365.2425f);
    s.noon = 90.0f - SIM_LAT_DEG + decl;
    // Hour angle from solar noon (UTC, so ~12:00 at this longitude).
    const float ha = _rad(15.0f * (s.hour - 12.0f));
    s.elev = 57.2957795f * asinf(
        sinf(_rad(decl)) * sinf(_rad(SIM_LAT_DEG)) +
        cosf(_rad(decl)) * cosf(_rad(SIM_LAT_DEG)) * cosf(ha));

    const float peak = sinf(_rad(s.noon > 1.0f ? s.noon : 1.0f));
    s.sun = (s.elev > 0.0f) ? sinf(_rad(s.elev)) / peak : 0.0f;
    if (s.sun > 1.0f) s.sun = 1.0f;

    // What today is like. Cloud drives both the light and how far the air
    // swings, so it is the one knob most of these turn.
    static float cloud = 0.3f;
    cloud = drift(cloud, 0.05f, 0.0f, 1.0f, s.dt_ms, 1200000.0f);  // ~20 min

    // Yesterday's character CROSS-FADED into today's over the small hours: a
    // cycle that stepped the weather at the stroke of midnight would be exactly
    // the implausibility this model exists to remove. A fixed scenario is the
    // same both sides, so this costs it nothing.
    float c0, t0, a0, d0, c1, t1, a1, d1;
    _character(acting(s.day - 1u), cloud, &c0, &t0, &a0, &d0);
    _character(s.act,              cloud, &c1, &t1, &a1, &d1);
    const float w = (s.hour < CHANGE_H) ? s.hour / CHANGE_H : 1.0f;
    s.cloud   = c0 + (c1 - c0) * w;
    s.dew_ofs = d0 + (d1 - d0) * w;

    const float summer = 0.5f * (1.0f - cosf(6.2831853f * (s.doy - 15.0f) / 365.2425f));
    s.t_mean = 3.5f + 17.0f * summer + (t0 + (t1 - t0) * w);

    s.irrad = 1050.0f * s.sun * (1.0f - 0.75f * s.cloud);

    // A clear sky swings wider than an overcast one, and summer wider than
    // winter: about 4 C in January and 20 C in July, either side of the mean.
    s.t_amp = (3.5f + 4.0f * summer) * (1.0f - 0.45f * s.cloud)
            + (a0 + (a1 - a0) * w);

    return s;
}

/**
 * @brief The house: what the weather does to it, and what its own actuators do
 * back. A CLOSED loop, because opening a window has to cool the house or the
 * PID never settles and the ladders never stop climbing.
 */
struct Plant {
    float t_in    = 8.0f;    ///< air temperature, C
    float gap     = 1.0f;    ///< ridge minus canopy, C
    float dew     = 4.0f;    ///< interior dew point, C
    float t_cs    = 10.0f;   ///< crawlspace, heavily damped
    float t_lag   = 0.0f;    ///< lagged solar forcing, 0..1
    bool  seeded  = false;
};

inline Plant& plant() { static Plant p; return p; }

/** @brief Mean of the four window positions, 0..1. */
inline float _windows_open(const Vars::Bank& io) {
    float sum = 0.0f;
    for (uint8_t i = 1; i <= 4; i++) {
        char n[20];
        snprintf(n, sizeof n, "cli_window_pos%u", (unsigned)i);
        sum += at(io, n).as_f32();
    }
    return sum / 4.0f;
}

/** @brief Exterior air temperature, LAGGED behind the sun.
 *
 *  Air warms after the sun does, which is why the minimum lands at dawn and the
 *  maximum in mid-afternoon in every season, without either being a constant.
 */
inline float _exterior(const Sky& s, uint32_t dt_ms) {
    Plant& p = plant();
    // ~2.5 h, the lag between solar noon and the day's warmest hour.
    const float dt = (float)dt_ms / 1000.0f;
    const float a  = dt / (9000.0f + dt);
    if (!p.seeded) {
        // Started where the weather is, so a run does not open on an hour of
        // transient that is nothing to do with the season it asked for.
        p.t_lag  = s.sun;
        p.seeded = true;
        const float t0 = s.t_mean - s.t_amp + 2.0f * s.t_amp * s.sun;
        p.t_in = t0 + 2.0f;
        p.dew  = t0 - 3.0f;
        p.t_cs = t0;
    }
    p.t_lag += a * (s.sun - p.t_lag);
    return s.t_mean - s.t_amp + 2.0f * s.t_amp * p.t_lag;
}

/** @brief Step the house: solar gain in, ventilation and conduction out. The
 *  coefficients are a greenhouse's SHAPE, not any particular one. */
inline void step_plant(const Vars::Bank& io, const Sky& s, float t_ext,
                       uint32_t dt_ms) {
    Plant& p = plant();
    const float dt = (float)dt_ms / 1000.0f;
    if (dt <= 0.0f) return;

    const bool  screen = at(io, "cli_screen").as_bool();
    const float vent   = _windows_open(io);
    // The INTAKE fans exchange air with outside; the destratification fans only
    // stir what is already in here, so they move the gap and not the heat.
    const float intake = 0.5f * (at(io, "cli_intr1").as_f32() +
                                 at(io, "cli_intr2").as_f32());
    const float mix    = (float)at(io, "cli_fanSpeed").as_i64() / 3.0f;
    const float skirt  = at(io, "cli_skirtValve").as_bool() ? 1.0f : 0.0f;

    // Solar gain, shaded by the screen. Conduction through the fabric, which
    // the screen is there to slow at night. Ventilation, the only thing that
    // beats a sunny afternoon: shut, the house runs ~19 C over ambient.
    const float gain = s.irrad * (screen ? 0.30f : 1.0f) * 1.0e-5f;
    const float ua   = screen ? 0.00022f : 0.00055f;
    const float ach  = 0.0045f * vent + 0.0030f * intake;
    p.t_in += (gain - (p.t_in - t_ext) * (ua + ach)) * dt;

    // Stratification: the sun builds it, the destratification fans kill it.
    const float want = (0.8f + 6.0f * s.sun * (screen ? 0.4f : 1.0f))
                     * (1.0f - 0.9f * mix);
    p.gap += (want - p.gap) * (dt / (600.0f + dt));

    // Moisture: the plants transpire with the light, the skirt adds water, and
    // only the air that LEAVES takes any with it.
    const float dew_ext = t_ext - 5.0f + 4.0f * s.cloud + s.dew_ofs;
    const float transp  = 0.0009f * s.sun + 0.0016f * skirt;
    p.dew += (transp - (p.dew - dew_ext) *
                       (0.00012f + 0.0022f * vent + 0.0016f * intake)) * dt;
    if (p.dew > p.t_in) p.dew = p.t_in;   // saturation: nothing over 100% RH

    // The crawlspace is the ground: a season behind, not a day.
    p.t_cs += (t_ext - p.t_cs) * (dt / (43200.0f + dt));
}

/** @brief Relative humidity, 0..100, from a temperature and a dew point.
 *  The Magnus relation the site's own dew_point() inverts, so the two agree. */
inline float _rh_from_dew(float t, float td) {
    const float a = 17.62f, b = 243.12f;
    float rh = 100.0f * expf((a * td) / (b + td) - (a * t) / (b + t));
    if (rh < 3.0f)  rh = 3.0f;
    if (rh > 99.5f) rh = 99.5f;
    return rh;
}

/**
 * @brief The grid meter, the transfer switch, and the failure the `supply`
 * scenario walks through: a sag, then a loss the generator picks up, then the
 * UPS calling its battery low - the one that raises the plant-wide lockout.
 */
inline void publish_meters(Vars::Bank& io, const Sky& s, uint32_t now_ms) {
    const float t = (float)now_ms / 1000.0f;

    static float load = 0.0f;
    load = drift(load, 0.5f, 0.0f, 1.0f, s.dt_ms, 60000.0f);       // ~1 min
    // The plant draws what it is running: fans and windows are the load.
    const float duty = 0.25f + 0.55f * ((float)at(io, "cli_fanSpeed").as_i64() / 3.0f);

    // The power day, in four acts, by the hour.
    float sag = 1.0f;
    bool  grid_dead = false, gen_running = false, ups_low = false;
    if (s.act == SCN_SUPPLY) {
        if (s.hour >= 9.0f && s.hour < 10.0f)  sag = 0.82f;          // brownout
        if (s.hour >= 13.0f && s.hour < 15.0f) { grid_dead = true; gen_running = true; }
        if (s.hour >= 14.0f && s.hour < 15.0f) ups_low = true;       // ...and the UPS gives up
    }

    const float ln = grid_dead ? 0.0f : (230.0f * sag + 3.0f * (2.0f * load - 1.0f));
    const float amps = grid_dead ? 0.0f : (4.0f + 28.0f * duty);
    const float pf = 0.88f + 0.10f * duty;
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
    // Mains frequency wanders in the last decimal; a generator wanders wider.
    const float hz = grid_dead ? 0.0f : 50.0f + 0.03f * sinf(t / 37.0f);
    put(io, "grid_Hz", hz, now_ms);

    // Downstream of the transfer switch: the generator carries it when the grid
    // is gone, at a frequency only the LOCAL band tolerates.
    const float lv = gen_running ? 228.0f + 4.0f * sinf(t / 11.0f) : ln - 0.5f;
    const float lhz = gen_running ? 49.4f + 0.5f * sinf(t / 7.0f) : hz;
    put(io, "ats_V",  lv, now_ms);
    put(io, "ats_A",  amps * 0.15f + (gen_running ? 6.0f : 0.0f), now_ms);
    put(io, "ats_W",  lv * (amps * 0.15f + (gen_running ? 6.0f : 0.0f)) * pf, now_ms);
    put(io, "ats_Hz", lhz, now_ms);

    // The UPS low-battery contact, through the TERMINAL: the local input scan
    // is what publishes it on hardware, and driving the row here would step
    // over the thing under test.
    HAL::DigitalIn::inject(HAL::Pin::IN1, ups_low);
}

/** @brief The calibrated channels on TDC/AI-1: the four dividers and the flow
 *  loop. The only rows that SCALE, so this is the calibration round trip. */
inline void publish_analog(Vars::Bank& io, const Sky& s, uint32_t now_ms) {
    const float t = (float)now_ms / 1000.0f;
    static float ripple = 0.0f;
    ripple = drift(ripple, 0.3f, -1.0f, 1.0f, s.dt_ms, 30000.0f);  // ~30 s

    // The rails sag with the mains, and the batteries carry the outage: the
    // generator battery is the one that gets low enough to be worth an alarm.
    const bool outage = (s.act == SCN_SUPPLY && s.hour >= 13.0f && s.hour < 15.0f);
    const float droop = outage ? 3.5f : 0.0f;

    put(io, "pwr_dc_V1",     24.0f + 0.8f * ripple - droop, now_ms);
    put(io, "pwr_dc_V2",     24.1f - 0.6f * ripple - droop, now_ms);
    put(io, "pwr_upsBat_V",  26.8f + 0.3f * ripple - 2.0f * droop, now_ms);
    put(io, "pwr_genBat_V",  25.4f + 0.5f * ripple - droop, now_ms);
    // Output flow: what the irrigation cycle is actually drawing, plus a swell.
    // Sized against irr_expDrainFrac: a tray gives back about 0.6 of its dose
    // inside irr_drainWindow_sec, so the verdict has something to judge.
    const bool draining = at(io, "irr_output_ro1").as_bool();
    put(io, "irr_out_Lpm", draining ? 20.0f + 3.0f * sinf(t / 5.0f) : 0.0f, now_ms);
}

/** @brief The two weather stations: wind with gusts, showers, and the
 *  irradiance the sky already computed. Without them the sky reads as
 *  unmeasured and the roof is locked shut for ever, the interlock doing its
 *  job on a field that was not there. */
inline void publish_weather(Vars::Bank& io, const Sky& s, uint32_t now_ms) {
    static float    wind    = 1.5f;
    static uint32_t rain_ms = 0;

    // A storm day sits over cli_windShutEnd_mps - 20.8 mps, which is 75 kmph -
    // for hours, so the roof shuts and alarm_weather_storm means it. A fair day
    // keeps its gusts under cli_windShutBegin_mps, so the ramp is a storm-day thing
    // and not a permanent ceiling on the roof.
    const bool storm = (s.act == SCN_STORM && s.hour >= 6.0f && s.hour < 20.0f);
    wind = drift(wind, 1.2f, 0.2f, storm ? 30.0f : 6.0f, s.dt_ms, 120000.0f);
    if (storm && wind < 22.0f) wind = 22.0f;
    // BOTH stations, and not identically: they stand in different places, so a
    // vote between two readings that never differ would prove nothing about
    // the voting. The second reads a little low, which is the direction that
    // matters - the worse of the two is what the interlock takes.
    put(io, "cli_wind_mps1", wind, now_ms);
    put(io, "cli_wind_mps2", wind * 0.94f, now_ms);
    put(io, "cli_windGust_mps1", wind * 1.5f, now_ms);
    put(io, "cli_windGust_mps2", wind * 1.42f, now_ms);

    // Showers rather than a rain rate that wanders: one starts rarely and then
    // lasts, which is what the lockout's overrun is written against. About one
    // a day fair, one an hour under a storm.
    if (rain_ms > s.dt_ms) rain_ms -= s.dt_ms;
    else if (rain_ms > 0)  rain_ms = 0;
    else if (chance_per(s.dt_ms, storm ? 3600000.0f : 86400000.0f))
        rain_ms = storm ? 1800000u : 600000u;
    const float rain = (rain_ms > 0) ? (storm ? 12.0f : 3.0f) : 0.0f;
    put(io, "cli_rain_mmph1", rain, now_ms);
    put(io, "cli_rain_mmph2", rain * 0.90f, now_ms);

    put(io, "cli_irrad_Wpm21", s.irrad, now_ms);
    put(io, "cli_irrad_Wpm22", s.irrad * 0.97f, now_ms);
}

/** @brief The inlet meter: a pulse train, only while the inlet valve is open,
 *  and through the CHANNEL - the counter is what the irrigation cycle reads.
 *
 *  60 L/min at one pulse per litre, which is what the TABLE asks for: tray 1
 *  doses 150 L and irr_maxFill_sec allows 300, so a supply slower than 30 L/min
 *  cannot finish a fill and every cycle ends in irr_fillFault. */
inline void pulse_inlet(const Vars::Bank& io, uint32_t now_ms) {
    static uint32_t owed_ms = 0;
    static uint32_t last_ms = 0;

    const uint32_t dt = (now_ms > last_ms) ? (now_ms - last_ms) : 0;
    last_ms = now_ms;
    if (!at(io, "irr_input_ro1").as_bool()) { owed_ms = 0; return; }

    owed_ms += dt;
    const uint32_t pulses = owed_ms / 1000u;
    owed_ms -= pulses * 1000u;
    // The channel the ROW declares: a PI row IS the counter, so there is no
    // pulse table beside the table to read it from.
    if (pulses > 0)
        HAL::Pulse::inject(
            Vars::pulse_channel(at(io, "irr_inCount").meta()), pulses);
}

/**
 * @brief The twelve Temp/RH sensors, off the one plant model. Animated because
 * the rows exist: unfed, the bus answers zeroed registers and every sensor
 * reads a perfectly VALID 0.0 C, which is not a measurement. The `probes`
 * scenario takes them out one at a time, so the quorum holds, then does not.
 */
inline void publish_climate(Vars::Bank& io, const Sky& s, uint32_t now_ms) {
    const float t_ext = _exterior(s, s.dt_ms);
    step_plant(io, s, t_ext, s.dt_ms);
    const Plant& p = plant();

    // How many ridge probes are out: one keeps the quorum, two break it. Each
    // is a whole DEVICE going quiet, which is what a dead probe is.
    uint8_t mute = 0;
    if (s.act == SCN_PROBES) {
        if (s.hour >= 4.0f && s.hour < 5.0f) mute = 1;
        if (s.hour >= 5.0f && s.hour < 6.0f) mute = 2;
    }
    static uint8_t muted = 0;
    if (mute != muted) {
        for (uint8_t n = 1; n <= 3; n++) {
            char name[24];
            snprintf(name, sizeof name, "cli_temp_hi%u", (unsigned)n);
            silence(io, name, n <= mute);
        }
        muted = mute;
    }

    struct Group {
        const char* temp;   ///< printf pattern, one %u for the sensor number
        const char* rh;
        float       t;      ///< what this group is measuring
        float       dew;
    };
    const Group groups[] = {
        { "cli_temp_ext%u", "cli_rh_ext%u", t_ext,               t_ext - 3.5f },
        { "cli_temp_cs%u",  "cli_rh_cs%u",  p.t_cs,              p.t_cs - 1.0f },
        { "cli_temp_lo%u",  "cli_rh_lo%u",  p.t_in - p.gap / 2,  p.dew },
        { "cli_temp_hi%u",  "cli_rh_hi%u",  p.t_in + p.gap / 2,  p.dew },
    };

    for (uint8_t g = 0; g < 4; g++) {
        for (uint8_t n = 1; n <= 3; n++) {
            if (g == 3 && n <= mute) continue;      // a probe that stopped answering
            // Three instruments in one space never agree exactly.
            const float spread = ((float)n - 2.0f) * 0.25f;
            const float temp = groups[g].t + spread;
            char name[24];
            snprintf(name, sizeof name, groups[g].temp, (unsigned)n);
            put(io, name, temp, now_ms);
            snprintf(name, sizeof name, groups[g].rh, (unsigned)n);
            put(io, name, _rh_from_dew(temp, groups[g].dew), now_ms);
        }
    }
}


// Entry point

/**
 * @brief Stand in for the field: publish meter readings, drive the coils, and
 * raise alarms. Called straight from the sim harness.
 */
inline void simulate(Vars::Bank& io, uint32_t now_ms) {
    const Sky s = sky_now(now_ms);    // one sky, which every row then agrees on
    publish_meters(io, s, now_ms);
    publish_climate(io, s, now_ms);
    publish_analog(io, s, now_ms);
    publish_weather(io, s, now_ms);
    pulse_inlet(io, now_ms);
}

}  // namespace Site

#endif // TARGET_SIMULATOR

#endif // GH_MLB41_SIM_H