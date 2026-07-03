// REVIEW_35: TODO
#ifndef SITES_GH_MLB41_SIM_H
#define SITES_GH_MLB41_SIM_H

/**
 * @file sim.h
 * @brief gh_mlb41 simulator-only mock PLC: data animator + timeout dilation.
 *
 * Header-only; the whole body is guarded by TARGET_SIMULATOR, so it's a
 * near-empty include on the OPTA / Arduino-library build (where the real
 * coprocessor plays this role). Included only via site.h, after the payload.
 * The floats here only shape the mock signal; the payload keeps the PLC integer
 * conventions and never carries a float.
 */

#ifdef TARGET_SIMULATOR

#include <cstdint>
#include <cstring>  // strncpy (config host strings)
#include <cmath>    // pow
#include <random>

#include "libbotanik.h"
#include "libbotanik/alarms/alarms.h"   // alarm_bit / alarm_index, ALARM_BITS
#include "sites/gh_mlb41/alarms.h"      // g_alarms - the one alarm definition

/**
 * @brief Grow a base PLC link timeout (ms) by time-dilation factor S, so jitter
 * (amplified by a high BOTANIK_TIME_SCALE) can't false-trip the link. Clamped
 * below 2^31 to keep the 32-bit elapsed compares rollover-safe. No-op when
 * S == 1. Called from main.cpp and the emulator thread (forward-declared in
 * botanik_sim/emulator.h).
 */
inline uint32_t plc_dilated_timeout(uint32_t base_ms) {
    uint32_t s = Botanik::HAL::_time_scale();
    if (s <= 1) return base_ms;
    double scaled = (double)base_ms * std::pow((double)s, 3);
    const double cap = 2147483647.0;  // < 2^31
    return scaled >= cap ? (uint32_t)cap : (uint32_t)scaled;
}

namespace {

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

/** @brief Mock-PLC alarm bit for a table code (0 if undefined).
 *  Resolves through g_alarms so the mock bitset uses the firmware's bit
 *  layout. */
inline uint64_t abit(const char* code) {
    int i = Botanik::Alarms::alarm_index(g_alarms, g_alarm_count, code);
    return i >= 0 ? Botanik::Alarms::alarm_bit((uint8_t)i) : 0;
}

/**
 * @brief Animate the greenhouse payload with plausible, mostly-steady values.
 *
 * Called from the emulator thread (~1 Hz); static state lets values settle and
 * only drift now and then. @p now_ms is the uptime in milliseconds.
 */
inline void fill_greenhouse_output(PlcOutputData_t* o, uint32_t now_ms) {
    // seconds - drives irrigation walk
    const float t = (float)now_ms / 1000.0f;

    // Slow mean-reverting climate driver in [-1, 1] (the * factor pulls it back
    // toward 0): mostly mild, rarely excursing far enough to trip TOO_HOT/HUM.
    static float climate = 0.0f;
    climate = drift(climate, 0.35f, -1.0f, 1.0f, 0.06f) * 0.985f;

    // Climate sensors - three height zones share the level with a static
    // offset (ceiling warmer/drier than the floor).
    int temp_centi = 2200 + (int)(300.0f * climate);             // ~19..25 degC
    o->ext_temp_c_centi   = (int16_t)(temp_centi - 150);
    o->floor_temp_c_centi = (int16_t)(temp_centi);
    o->ceil_temp_c_centi  = (int16_t)(temp_centi + 200);
    int rh_centi = 6000 - (int)(1000.0f * climate);              // ~50..70 %
    o->ext_rh_centi_pct   = (uint16_t)(rh_centi + 200);
    o->floor_rh_centi_pct = (uint16_t)(rh_centi);
    o->ceil_rh_centi_pct  = (uint16_t)(rh_centi - 400);

    // Weather - rain rarely sets in then clears; wind a light breeze, gusts.
    static bool     raining       = false;
    static uint32_t rain_since_ms = 0;
    raining = raining ? !chance(0.005f) : chance(0.002f);
    if (raining) rain_since_ms = now_ms;
    o->rain_detected = raining ? 1 : 0;
    o->rain_age_s    = raining ? 0
                               : (uint32_t)((now_ms - rain_since_ms) / 1000);
    static float wind = 12.0f;
    wind = drift(wind, 5.0f, 5.0f, 40.0f, 0.05f);
    o->wind_peak_kmh = (uint16_t)wind;

    // Power - nominal within tight tolerances; the B bus occasionally drops.
    static float grid_mv   = 230000.0f;
    static float grid_freq = 5000.0f;
    static float dc_mv     = 24000.0f;
    static float batt_mv   = 12600.0f;
    // Grid sits inside spec ([226000, 234000] is the AC_GRID band), with a rare
    // sag/surge out of spec to exercise AC_GRID (drift clamps it back later).
    grid_mv   = drift(grid_mv,   900.0f,  227000.0f, 233000.0f, 0.05f);
    if (chance(0.0025f)) grid_mv = chance(0.5f) ? 224000.0f : 236000.0f;
    // ~50 Hz
    grid_freq = drift(grid_freq, 8.0f,    4990.0f,   5010.0f,   0.05f);
    dc_mv     = drift(dc_mv,     150.0f,  23500.0f,  24500.0f,  0.05f);
    batt_mv   = drift(batt_mv,   300.0f,  11800.0f,  13000.0f,  0.03f);
    o->grid_ac_mv         = (uint32_t)grid_mv;
    o->grid_freq_centi_hz = (uint16_t)grid_freq;
    o->dc_bus_mv          = (uint32_t)dc_mv;
    o->dc_bus_a_ok        = 1;
    static bool dc_b_ok = true;
    // mostly OK, rare dropout
    dc_b_ok = dc_b_ok ? !chance(0.004f) : chance(0.12f);
    o->dc_bus_b_ok        = dc_b_ok ? 1 : 0;
    o->battery_mv         = (uint32_t)batt_mv;
    o->ups_status         = UpsStatus::ONLINE;

    // Ventilation tracks temperature; channels stagger to distinct levels.
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
    o->tray_status  = (uint8_t)(TrayStatus::FILLING + cycle % 3);
    o->water_in_l   = (uint16_t)(100 + cycle % 50);
    static float water_out = 3.0f;
    water_out = drift(water_out, 1.0f, 1.0f, 6.0f, 0.05f);
    o->water_out_lpm = (uint16_t)water_out;

    // Monitored autonomous heating (active when it gets cold)
    o->generator_running = (climate < -0.7f) ? 1 : 0;
    o->generator_mv      = o->generator_running ? 13800 : 0;
    o->heat_blower_on    = (climate < -0.7f) ? 1 : 0;
    o->heatpump_on       = (climate < -0.4f) ? 1 : 0;

    // Network: mostly online, rare brief outage recovering via LINK.
    static bool net_up = true;
    net_up = net_up ? !chance(0.003f) : chance(0.3f);
    o->net_status = net_up ? NetStatus::ONLINE
                           : (chance(0.5f) ? NetStatus::LINK : NetStatus::DOWN);

    // NTP: disciplined while online, small wandering offset.
    static float ntp_off = 2.0f;
    ntp_off = drift(ntp_off, 3.0f, -25.0f, 25.0f, 0.1f);
    o->ntp_synced     = net_up ? 1 : 0;
    o->ntp_offset_ms  = (int16_t)ntp_off;

    // Sun times: roughly fixed, drift seasonally only a touch.
    static float sunrise = 6.0f * 60 + 12;   // 06:12
    static float sunset  = 21.0f * 60 + 34;  // 21:34
    sunrise = drift(sunrise, 1.0f, 5.0f * 60, 8.0f * 60, 0.02f);
    sunset  = drift(sunset,  1.0f, 19.0f * 60, 22.0f * 60, 0.02f);
    o->sunrise_min = (uint16_t)sunrise;
    o->sunset_min  = (uint16_t)sunset;

    // Alarms: synthesize the PLC's raw bitset from the mock state. Bits
    // addressed by table code (abit) so the mock PLC and firmware share the one
    // definition (g_alarms).
    uint64_t raw = 0;
    if (o->grid_ac_mv < 226000 || o->grid_ac_mv > 234000) raw |= abit("AC_GRID");
    if (!o->dc_bus_b_ok)                                   raw |= abit("DC_PS2");
    if (o->ups_status != UpsStatus::ONLINE)                raw |= abit("DC_UPS");
    if (o->generator_running && o->generator_mv < 12000)   raw |= abit("GEN_FLT");
    if (o->net_status == NetStatus::DOWN)                  raw |= abit("NET_FLT");
    if (!o->ntp_synced)                                    raw |= abit("NTP_TIM");
    if (o->ceil_temp_c_centi > 2600)                       raw |= abit("TOO_HOT");
    if (o->floor_temp_c_centi < 1500)                      raw |= abit("TOO_COLD");
    if (o->ext_rh_centi_pct > 6800)                        raw |= abit("TOO_HUM");
    if (o->wind_peak_kmh > 35 || raining)                  raw |= abit("WH_STORM");

    // Settle window: keep alarms clear for the first stretch after boot (a real
    // PLC likewise masks process alarms until its sensors settle).
    const uint32_t SETTLE_MS = 20000;
    if (now_ms < SETTLE_MS) raw = 0;

    // Debounce: hold each raw bit ON for a minimum dwell so a signal hovering
    // at its threshold can't strobe the ribbon (mirrors the real PLC's hardware
    // debounce; the firmware's per-alarm min_on_s is the further local net).
    // Countdown drained by dt.
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
    o->alarms = debounced;
}

/**
 * @brief Publish the mock PLC config block (network / NTP / MQTT / geo). On
 * hardware the coprocessor publishes these; in the sim the emulator does, so
 * the config originates from the emulated PLC like the rest of the output.
 * Per-block serials start at 1 so a consumer latched at 0 applies them once.
 */
inline void fill_greenhouse_config(Botanik::PlcIO::PlcOutputConfig_t* cfg) {
    auto set_host = [](Botanik::PlcIO::HostStr_t dst, const char* s) {
        std::strncpy(dst, s, Botanik::Config::PLC_HOST_STR_LEN - 1);
        dst[Botanik::Config::PLC_HOST_STR_LEN - 1] = '\0';
    };
    cfg->net.serial  = 1; cfg->net.dhcp = 1;            // DHCP; static empty
    set_host(cfg->net.dns1, "192.168.1.1");
    cfg->ntp.serial  = 1; cfg->ntp.enabled = 1; cfg->ntp.poll_s = 64;
    set_host(cfg->ntp.server1, "pool.ntp.org");
    // Broker port points at the in-process mock responder (sim has no
    // privileged ports); the name resolves to loopback in the HAL.
    cfg->mqtt.serial = 1; cfg->mqtt.enabled = 1; cfg->mqtt.keepalive_s = 60;
    cfg->mqtt.port = Botanik::HAL::Network::MQTT_MOCK_PORT;
    set_host(cfg->mqtt.broker, "broker.local");
    cfg->geo.serial = 1;                                // example: Paris (E7)
    cfg->geo.latitude_e7  =  488566000;                 //  48.8566 N
    cfg->geo.longitude_e7 =   23522000;                 //   2.3522 E
}

}  // namespace

#endif // TARGET_SIMULATOR

#endif // SITES_GH_MLB41_SIM_H
