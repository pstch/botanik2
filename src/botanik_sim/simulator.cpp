/**
 * @file simulator.cpp
 * @brief Mock greenhouse data source + emulator wiring for the simulator (see simulator.h).
 *
 * The floats here only shape the mock signal; the payload itself stays in the
 * PLC integer conventions documented in simulator.h and never carries a float on
 * the wire.
 */

#include "botanik_sim/simulator.h"
#include "libbotanik.h"   // PlcIO::start_emulator_thread, PlcInput/PlcOutput, Logs::Logger

#include <random>   // drives the rare perturbations that animate the mock data

namespace {

/// Process-wide PRNG draw in [0, 1). Fixed seed keeps simulator runs reproducible.
float frand() {
    static std::mt19937 rng(0xB0A11Cu);
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
}

/**
 * @brief Random walk for a quantity that should mostly sit still.
 *
 * On most ticks @p v is returned untouched; with probability @p p it is nudged by
 * up to +/-@p step and clamped to [@p lo, @p hi]. Called every tick, this yields a
 * signal that holds a level for long stretches and only drifts on the odd tick.
 */
float drift(float v, float step, float lo, float hi, float p) {
    if (frand() < p) {
        v += (2.0f * frand() - 1.0f) * step;
        if (v < lo) v = lo;
        if (v > hi) v = hi;
    }
    return v;
}

/// True with probability @p p - for rare discrete events (rain, bus dropouts).
bool chance(float p) { return frand() < p; }

}  // namespace

/**
 * @brief Animate the greenhouse payload with plausible, mostly-steady values.
 *
 * Called from the emulator thread (~1 Hz). The signals are held in static state
 * that stays put from tick to tick and only takes the occasional random step, so
 * the pages show values that look settled and shift now and then - closer to a
 * real site than a constant oscillation. @p now_ms is the uptime in milliseconds.
 */
static void fill_greenhouse_output(PlcOutputData_t* o, uint32_t now_ms) {
    const float t = (float)now_ms / 1000.0f;   // seconds - drives the irrigation walk

    // Slow climate driver in [-1, 1]. Holds a level for long stretches and only
    // drifts on the rare tick, so the sensors and actuators below read steady and
    // occasionally shift together.
    static float climate = 0.0f;
    climate = drift(climate, 0.25f, -1.0f, 1.0f, 0.03f);

    // Climate sensors - three height zones share the level with a static offset
    // (ceiling warmer/drier than the floor), so each reads a little differently.
    int temp_centi = 2200 + (int)(300.0f * climate);                 // ~19..25 degC
    o->ext_temp_c_centi   = (int16_t)(temp_centi - 150);
    o->floor_temp_c_centi = (int16_t)(temp_centi);
    o->ceil_temp_c_centi  = (int16_t)(temp_centi + 200);
    int rh_centi = 6000 - (int)(1000.0f * climate);                  // ~50..70 %
    o->ext_rh_centi_pct   = (uint16_t)(rh_centi + 200);
    o->floor_rh_centi_pct = (uint16_t)(rh_centi);
    o->ceil_rh_centi_pct  = (uint16_t)(rh_centi - 400);

    // Weather - rain rarely sets in and later clears; wind sits around a light
    // breeze and gusts now and then.
    static bool     raining       = false;
    static uint32_t rain_since_ms = 0;
    raining = raining ? !chance(0.01f) : chance(0.002f);
    if (raining) rain_since_ms = now_ms;
    o->rain_detected = raining ? 1 : 0;
    o->rain_age_s    = raining ? 0 : (uint16_t)((now_ms - rain_since_ms) / 1000);
    static float wind = 12.0f;
    wind = drift(wind, 5.0f, 5.0f, 40.0f, 0.05f);
    o->wind_peak_kmh = (uint16_t)wind;

    // Power - all nominal, drifting within tight tolerances; the B bus very
    // occasionally drops out and recovers.
    static float grid_mv   = 230000.0f;
    static float grid_freq = 5000.0f;
    static float dc_mv     = 24000.0f;
    static float batt_mv   = 12600.0f;
    grid_mv   = drift(grid_mv,   1500.0f, 225000.0f, 235000.0f, 0.05f);
    grid_freq = drift(grid_freq, 8.0f,    4990.0f,   5010.0f,   0.05f);  // ~49.9..50.1
    dc_mv     = drift(dc_mv,     150.0f,  23500.0f,  24500.0f,  0.05f);
    batt_mv   = drift(batt_mv,   300.0f,  11800.0f,  13000.0f,  0.03f);
    o->grid_ac_mv         = (uint32_t)grid_mv;
    o->grid_freq_centi_hz = (uint16_t)grid_freq;
    o->dc_bus_mv          = (uint32_t)dc_mv;
    o->dc_bus_a_ok        = 1;
    static bool dc_b_ok = true;
    dc_b_ok = dc_b_ok ? !chance(0.005f) : chance(0.05f);  // mostly OK, the odd dropout
    o->dc_bus_b_ok        = dc_b_ok ? 1 : 0;
    o->battery_mv         = (uint32_t)batt_mv;
    o->ups_status         = UPS_ONLINE;

    // Ventilation tracks temperature (warmer -> more ventilation/cooling). Windows,
    // blowers and extractors stagger so each channel shows a distinct level.
    int vent = 50 + (int)(50.0f * climate);
    if (vent < 0)   vent = 0;
    if (vent > 100) vent = 100;
    o->window_a = (uint8_t)vent;
    o->window_b = (uint8_t)(vent > 10 ? vent - 10 : 0);
    o->window_c = (uint8_t)(vent / 2);
    o->window_d = (uint8_t)(vent / 4);
    o->blower_a = (uint8_t)vent;
    o->blower_b = (uint8_t)(vent / 2);
    o->blower_c = (uint8_t)(vent / 4);
    o->blower_d = 0;
    o->extractor_a = (uint8_t)(vent * 3 / 4);
    o->extractor_b = (uint8_t)(vent > 20 ? vent - 20 : 0);
    o->thermal_screen = (uint8_t)((climate < -0.3f) ? 80 : 0);
    o->cooling_on  = (climate > 0.5f) ? 1 : 0;

    // Irrigation: walk trays 1..6 (~30 s each), cycling fill -> soak -> drain.
    uint32_t cycle  = (uint32_t)t / 30;
    o->current_tray = (uint8_t)(1 + cycle % 6);
    o->tray_status  = (uint8_t)(TRAY_FILLING + cycle % 3);
    o->water_in_l   = (uint16_t)(100 + cycle % 50);
    static float water_out = 3.0f;
    water_out = drift(water_out, 1.0f, 1.0f, 6.0f, 0.05f);
    o->water_out_lpm = (uint16_t)water_out;

    // Monitored autonomous heating (active when it gets cold)
    o->generator_running = (climate < -0.7f) ? 1 : 0;
    o->generator_mv      = o->generator_running ? 13800 : 0;
    o->heat_blower_on    = (climate < -0.7f) ? 1 : 0;
    o->heatpump_on       = (climate < -0.4f) ? 1 : 0;
}

// Spawn the emulator thread bound to the shared PLC memory, with the greenhouse
// fill hook above as its site payload animator. Keeps the fill hook and the
// PlcIO plumbing private to this translation unit (see simulator.h).
void start_greenhouse_emulator(Botanik::PlcIO::PlcInput<PlcInputData_t>*   shared_in,
                               Botanik::PlcIO::PlcOutput<PlcOutputData_t>* shared_out,
                               Botanik::Logs::Logger*                      logger) {
    Botanik::Scheduler::make_process_realtime(0, logger);
    Botanik::PlcIO::start_emulator_thread(shared_in, shared_out,
                                          &fill_greenhouse_output, logger);
}

// Graceful-shutdown plumbing for the simulator process (see simulator.h).
volatile sig_atomic_t botanik_running = 1;
extern "C" void botanik_sim_shutdown(int) { botanik_running = 0; }
