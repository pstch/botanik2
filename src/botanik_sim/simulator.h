#ifndef BOTANIK_SIM_SIMULATOR_H
#define BOTANIK_SIM_SIMULATOR_H

/**
 * @file simulator.h
 * @brief Site-specific PLC payload layout and the simulator's mock PLC coprocessor.
 *
 * On OPTA this plumbing lives in the .ino sketch and PlcOutputData_t would mirror
 * the PLC IDE's generated type, verified with:
 *   static_assert(sizeof(PLCSharedVarsInput_t)   == sizeof(PlcIO::PlcInput<PlcInputData_t>));
 *   static_assert(alignof(PLCSharedVarsOutput_t) == alignof(PlcIO::PlcOutput<PlcOutputData_t>));
 * Values follow PLC-style integer conventions (no floats on the wire): voltages
 * in millivolts, temperature in centi-degrees Celsius, humidity in centi-percent,
 * actuator levels in percent, and discrete states packed as enums / flags.
 */

#include <csignal>   // sig_atomic_t - async-signal-safe shutdown flag
#include <cstdint>

// Forward declarations suffice here (start_greenhouse_emulator() only takes
// pointers); full definitions come in via simulator.cpp.
namespace Botanik {
    namespace PlcIO { template<typename> struct PlcInput; template<typename> struct PlcOutput; }
    namespace Logs  { class Logger; }
}

/** @name PLC payload
 *  @{ */

struct PlcInputData_t {};   // no site-specific data beyond the library log fields

/** @brief UPS operating state. */
enum UpsStatus : uint8_t {
    UPS_OFFLINE    = 0,
    UPS_ONLINE     = 1,
    UPS_ON_BATTERY = 2,
    UPS_FAULT      = 3,
};

/** @brief Irrigation tray cycle phase. */
enum TrayStatus : uint8_t {
    TRAY_IDLE     = 0,
    TRAY_FILLING  = 1,
    TRAY_SOAKING  = 2,
    TRAY_DRAINING = 3,
};

/** @brief Network link state. */
enum NetStatus : uint8_t {
    NET_DOWN   = 0,   ///< no link
    NET_LINK   = 1,   ///< L2 up, no address yet
    NET_ONLINE = 2,   ///< up with an address
};

/**
 * @brief Greenhouse PLC output payload (sensors + actuator feedback).
 *
 * Explicitly padded to a 4-byte multiple so the combined PlcOutput<> layout has
 * no implicit padding (matches the byte-for-byte contract in plcio/vars.h).
 * Fields are ordered widest-first so every member lands on its natural alignment
 * with no implicit gaps.
 */
struct PlcOutputData_t {
    // Power (voltages in millivolts)
    uint32_t grid_ac_mv;         ///< mains AC RMS voltage, millivolts
    uint32_t dc_bus_mv;          ///< DC system bus voltage, millivolts
    uint32_t battery_mv;         ///< battery bank voltage, millivolts
    uint32_t generator_mv;       ///< generator output voltage, millivolts
    uint16_t grid_freq_centi_hz; ///< mains frequency, 0.01 Hz
    // Climate (filtered sensor data, one reading per height zone)
    int16_t  ext_temp_c_centi;   ///< exterior air temp, 0.01 degC
    int16_t  floor_temp_c_centi; ///< floor-level temp, 0.01 degC
    int16_t  ceil_temp_c_centi;  ///< ceiling-level temp, 0.01 degC
    uint16_t ext_rh_centi_pct;   ///< exterior relative humidity, 0.01 %
    uint16_t floor_rh_centi_pct; ///< floor relative humidity, 0.01 %
    uint16_t ceil_rh_centi_pct;  ///< ceiling relative humidity, 0.01 %
    uint16_t rain_age_s;         ///< seconds since rain was last detected
    uint16_t wind_peak_kmh;      ///< 15-minute peak wind speed, km/h
    // Irrigation
    uint16_t water_in_l;         ///< cumulative water input, liters
    uint16_t water_out_lpm;      ///< current drain flow, liters/minute
    // System / environment extras
    uint16_t sunrise_min;        ///< sunrise, minutes since local midnight
    uint16_t sunset_min;         ///< sunset, minutes since local midnight
    int16_t  ntp_offset_ms;      ///< NTP clock offset, milliseconds (signed)
    uint8_t  current_tray;       ///< active tray (1..N), 0 = none
    uint8_t  tray_status;        ///< TrayStatus
    // Actuators (levels in percent)
    uint8_t  window_a;           ///< roof window A opening, percent
    uint8_t  window_b;           ///< roof window B opening, percent
    uint8_t  window_c;           ///< roof window C opening, percent
    uint8_t  window_d;           ///< roof window D opening, percent
    uint8_t  blower_a;           ///< blower A level, percent
    uint8_t  blower_b;           ///< blower B level, percent
    uint8_t  blower_c;           ///< blower C level, percent
    uint8_t  blower_d;           ///< blower D level, percent
    uint8_t  extractor_a;        ///< air extractor A level, percent
    uint8_t  extractor_b;        ///< air extractor B level, percent
    uint8_t  thermal_screen;     ///< thermal screen closure, percent
    // Discrete status / monitored autonomous systems
    uint8_t  ups_status;         ///< UpsStatus
    uint8_t  dc_bus_a_ok;        ///< DC rectifier A healthy, boolean
    uint8_t  dc_bus_b_ok;        ///< DC rectifier B healthy, boolean
    uint8_t  rain_detected;      ///< rain sensor, boolean
    uint8_t  cooling_on;         ///< humidifier (evaporative cooling) on, boolean
    uint8_t  generator_running;  ///< emergency generator, boolean (monitored)
    uint8_t  heat_blower_on;     ///< emergency heat blower, boolean (monitored)
    uint8_t  heatpump_on;        ///< heatpump, boolean (monitored)
    uint8_t  net_status;         ///< NetStatus (down / link / online)
    uint8_t  ntp_synced;         ///< NTP disciplined the RTC, boolean
    uint8_t  _reserved[1];       ///< pad to a 4-byte multiple
};
static_assert(sizeof(PlcOutputData_t) % 4 == 0, "PlcOutputData_t must be 4-byte aligned");

/** @} */

/** @name Emulator
 *  @{ */

/**
 * @brief Grow a base PLC link timeout (ms) by time-dilation factor S as base *
 * S^1.5, so jitter (amplified ~linearly in S by a high BOTANIK_TIME_SCALE) can't
 * false-trip the link; real-time budget then scales with sqrt(S). Clamped below
 * 2^31 to keep the 32-bit elapsed compares rollover-safe. No-op when S == 1.
 *
 * Called from the host handler (main.cpp) and the emulator thread (the forward
 * declaration in plcio/emulator.h resolves here).
 */
uint32_t plc_dilated_timeout(uint32_t base_ms);

/**
 * @brief Start the simulator's mock PLC coprocessor: spawn the emulator thread
 * bound to the shared PLC memory, animating the greenhouse payload (see
 * simulator.cpp). On OPTA the real coprocessor plays this role.
 *
 * Must be called before the process is locked into realtime (see the call site).
 */
void start_greenhouse_emulator(Botanik::PlcIO::PlcInput<PlcInputData_t>*   shared_in,
                               Botanik::PlcIO::PlcOutput<PlcOutputData_t>* shared_out,
                               Botanik::Logs::Logger*                      logger);

/** @} */

/** @name Process lifecycle
 *  @{ */

/** @brief Main-loop run flag: cleared by botanik_sim_shutdown() so the loop breaks
 *  and main() returns through atexit (terminal restore) rather than being killed
 *  mid-loop. sig_atomic_t is the async-signal-safe flag type. */
extern volatile sig_atomic_t botanik_running;

/** @brief Signal that triggered the shutdown (0 = none), so main() can report the
 *  conventional 128 + signo exit status. */
extern volatile sig_atomic_t botanik_shutdown_signo;

/** @brief SIGINT/SIGTERM handler: records the signal and clears botanik_running.
 *  extern "C" so it can be installed with std::signal. */
extern "C" void botanik_sim_shutdown(int);

/** @} */

#endif // BOTANIK_SIM_SIMULATOR_H
