#ifndef SITE_SIM_H
#define SITE_SIM_H

/**
 * @file sim.h
 * @brief HAND-WRITTEN - the gh_mlb41 mock PLC: the data animator, the emulator
 *        instance the simulator runs, and the timeout dilation.
 *
 * Not generated: how a greenhouse *behaves* is judgement, not schema, so this
 * file is yours. What matters for the interface is that it animates THIS site's
 * generated payload, so the simulator exercises the same layout, tables and
 * offsets the hardware will.
 *
 * Header-only; the whole body is guarded by TARGET_SIMULATOR, so it is a
 * near-empty include on the OPTA / Arduino build, where the real M4 coprocessor
 * plays this role. The floats here only shape the mock signal.
 */

#ifdef TARGET_SIMULATOR

#include <cstdint>
#include <cstring>  // strncpy (config host strings)
#include <cmath>    // pow
#include <random>

// Src-rooted on purpose, unlike every other include in this site. The emulator
// lives in the simulator harness, not the library, so it is only ever compiled by
// our CMake build - which puts the library's src/ on the include path. This
// include sits inside the TARGET_SIMULATOR guard, so a vendored sketch build
// never reaches it and never needs the harness present.
#include "botanik_sim/emulator.h"       // PlcEmulator + start_emulator_thread

#include "botanik.h"

#include "alarms.h"                     // g_alarms - the one alarm definition

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
inline void fill_output(PlcOutputData_t* o, const PlcInputData_t* in,
                                   uint32_t now_ms) {
    // seconds - drives irrigation walk
    const float t = (float)now_ms / 1000.0f;

    // Slow mean-reverting climate driver in [-1, 1] (the * factor pulls it back
    // toward 0): mostly mild, rarely excursing far enough to trip TOO_HOT/HUM.
    static float climate = 0.0f;
    climate = drift(climate, 0.35f, -1.0f, 1.0f, 0.06f) * 0.985f;

    // Climate sensors - three height zones share the level with a static
    // offset (ceiling warmer/drier than the floor).
    float temp_c = 22.0f + 3.0f * climate;                       // ~19..25 degC
    o->ext_temp_c   = temp_c - 1.5f;
    o->floor_temp_c = temp_c;
    o->ceil_temp_c  = temp_c + 2.0f;
    float rh = 60.0f - 10.0f * climate;                          // ~50..70 %
    o->ext_rh_pct   = rh + 2.0f;
    o->floor_rh_pct = rh;
    o->ceil_rh_pct  = rh - 4.0f;

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
    static float grid_v  = 230.0f;
    static float grid_hz = 50.0f;
    static float dc_v    = 24.0f;
    static float batt_v  = 12.6f;
    // Grid sits inside spec ([226, 234] V is the AC_GRID band), with a rare
    // sag/surge out of spec to exercise AC_GRID (drift clamps it back later).
    grid_v  = drift(grid_v,  0.9f,  227.0f, 233.0f, 0.05f);
    if (chance(0.0025f)) grid_v = chance(0.5f) ? 224.0f : 236.0f;
    grid_hz = drift(grid_hz, 0.08f, 49.90f, 50.10f, 0.05f);
    dc_v    = drift(dc_v,    0.15f, 23.5f,  24.5f,  0.05f);
    batt_v  = drift(batt_v,  0.30f, 11.8f,  13.0f,  0.03f);
    o->grid_ac_v    = grid_v;
    o->grid_freq_hz = grid_hz;
    o->dc_bus_v     = dc_v;
    o->dc_bus_a_ok  = 1;
    static bool dc_b_ok = true;
    // mostly OK, rare dropout
    dc_b_ok = dc_b_ok ? !chance(0.004f) : chance(0.12f);
    o->dc_bus_b_ok  = dc_b_ok ? 1 : 0;
    o->battery_v    = batt_v;
    o->ups_status   = UpsStatus::ONLINE;

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
    o->generator_v       = o->generator_running ? 13.8f : 0.0f;
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
    if (o->grid_ac_v < 226.0f || o->grid_ac_v > 234.0f)  raw |= abit("AC_GRID");
    if (!o->dc_bus_b_ok)                                  raw |= abit("DC_PS2");
    if (o->ups_status != UpsStatus::ONLINE)               raw |= abit("DC_UPS");
    if (o->generator_running && o->generator_v < 12.0f)  raw |= abit("GEN_FLT");
    if (o->net_status == NetStatus::DOWN)                 raw |= abit("NET_FLT");
    if (!o->ntp_synced)                                   raw |= abit("NTP_TIM");
    if (o->ceil_temp_c > 26.0f)                           raw |= abit("TOO_HOT");
    if (o->floor_temp_c < 15.0f)                          raw |= abit("TOO_COLD");
    if (o->ext_rh_pct > 68.0f)                            raw |= abit("TOO_HUM");
    if (o->wind_peak_kmh > 35 || raining)                 raw |= abit("WH_STORM");

    // Settle window: keep alarms clear for the first stretch after boot (a real
    // PLC likewise masks process alarms until its sensors settle). Kept generous
    // so the first alarm lands well after boot, not seconds in.
    const uint32_t SETTLE_MS = 120000;   // 2 min
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
    // Scatter the debounced bitset into the per-alarm boolean fields (bit i ->
    // g_alarms[i].offset), the wire form the firmware's AlarmReader gathers. On
    // hardware the PLC program drives these booleans directly.
    uint8_t* base = reinterpret_cast<uint8_t*>(o);
    for (uint8_t i = 0; i < g_alarm_count; i++)
        base[g_alarms[i].offset] = (uint8_t)((debounced >> i) & 1u);

    // ---- Config (network / NTP / MQTT / geo) ------------------------------
    // Authoritative config: the coprocessor's NVM stand-in. Config parameters
    // are now ordinary payload fields, so the mock PLC owns a copy, seeds it
    // once (serials start at 1 so a consumer latched at 0 applies once), adopts
    // any group the host committed (its input serial advanced on the params
    // page), and mirrors the whole block into the output the params page and the
    // config-driven Components read.
    static GreenhouseConfig_t cfg;
    static bool params_seeded = false;
    auto set_host = [](char* dst, const char* s) {
        std::strncpy(dst, s, GreenhouseConfig_t::STRLEN - 1);
        dst[GreenhouseConfig_t::STRLEN - 1] = '\0';
    };
    if (!params_seeded) {
        params_seeded = true;
        memset(&cfg, 0, sizeof(cfg));
        cfg.net = 1; cfg.net_dhcp = 1;                  // DHCP; static empty
        set_host(cfg.net_dns1, "192.168.1.1");
        cfg.ntp = 1; cfg.ntp_enabled = 1; cfg.ntp_poll_s = 64;
        set_host(cfg.ntp_server1, "pool.ntp.org");
        // Broker port points at the in-process mock responder (sim has no
        // privileged ports); the name resolves to loopback in the HAL.
        cfg.mqtt = 1; cfg.mqtt_enabled = 1; cfg.mqtt_keepalive_s = 60;
        cfg.mqtt_port = Botanik::HAL::Network::MQTT_MOCK_PORT;
        set_host(cfg.mqtt_broker, "broker.local");
        cfg.geo = 1; cfg.geo_latitude = 48.8566f; cfg.geo_longitude = 2.3522f;  // Paris
    }
    // Adopt each group the host committed (its serial advanced and is non-zero):
    // take the staged fields as-is (the params page is responsible for a coherent
    // group), then the new serial the Components watch.
    if (in != nullptr) {
        const GreenhouseConfig_t& ic = in->config;
        if (ic.net && ic.net != cfg.net) {
            cfg.net_dhcp = ic.net_dhcp;
            set_host(cfg.net_ip, ic.net_ip);         set_host(cfg.net_netmask, ic.net_netmask);
            set_host(cfg.net_gateway, ic.net_gateway);
            set_host(cfg.net_dns1, ic.net_dns1);     set_host(cfg.net_dns2, ic.net_dns2);
            cfg.net = ic.net;
        }
        if (ic.ntp && ic.ntp != cfg.ntp) {
            cfg.ntp_enabled = ic.ntp_enabled; cfg.ntp_poll_s = ic.ntp_poll_s;
            set_host(cfg.ntp_server1, ic.ntp_server1); set_host(cfg.ntp_server2, ic.ntp_server2);
            cfg.ntp = ic.ntp;
        }
        if (ic.mqtt && ic.mqtt != cfg.mqtt) {
            cfg.mqtt_enabled = ic.mqtt_enabled; cfg.mqtt_port = ic.mqtt_port;
            cfg.mqtt_keepalive_s = ic.mqtt_keepalive_s;
            set_host(cfg.mqtt_broker, ic.mqtt_broker);
            cfg.mqtt = ic.mqtt;
        }
        if (ic.geo && ic.geo != cfg.geo) {
            cfg.geo_latitude = ic.geo_latitude; cfg.geo_longitude = ic.geo_longitude;
            cfg.geo = ic.geo;
        }
    }
    o->config = cfg;
}

// ---------------------------------------------------------------------------
// The site's mock PLC instance
// ---------------------------------------------------------------------------

/**
 * @brief The mock PLC coprocessor for this site: it owns the shared region and
 * animates it with fill_output() above.
 *
 * The site owns this, not the harness, so the emulator is instantiated against
 * THIS site's generated payload types - the simulator therefore runs the real
 * generated code, with the same struct layout and table offsets the hardware
 * uses. On OPTA the M4 coprocessor plays this role and none of this compiles.
 *
 * The harness allocates one of these (as a static - the region is large and must
 * outlive the firmware), hands the two region pointers to the firmware, and
 * calls start() at the right point in its own bring-up order: after setup(), and
 * before it locks process memory (mlockall(MCL_FUTURE) would otherwise trip
 * RLIMIT_MEMLOCK on the new thread's stack).
 */
class MockPlc {
    Botanik::PlcIO::PlcInput<PlcInputData_t>   _in  {};
    Botanik::PlcIO::PlcOutput<PlcOutputData_t> _out {};

public:
    /** @brief Host -> PLC region (the firmware writes, the mock reads). */
    Botanik::PlcIO::PlcInput<PlcInputData_t>*   input()  { return &_in; }
    /** @brief PLC -> host region (the mock writes, the firmware reads). */
    Botanik::PlcIO::PlcOutput<PlcOutputData_t>* output() { return &_out; }

    /** @brief Spawn the emulator thread driving the shared-memory protocol from
     *  the PLC's side, animating the payload through fill_output(). */
    void start(Botanik::Logs::Logger* logger) {
        Botanik::PlcIO::start_emulator_thread(&_in, &_out, &fill_output, logger);
    }
};

}  // namespace Site

#endif // TARGET_SIMULATOR

#endif // SITE_SIM_H
