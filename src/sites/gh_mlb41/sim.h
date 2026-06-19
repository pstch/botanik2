#ifndef SITES_GH_MLB41_SIM_H
#define SITES_GH_MLB41_SIM_H

/**
 * @file sim.h
 * @brief gh_mlb41 simulator-only mock PLC: data animator + emulator wiring.
 *
 * Simulator-only. The definitions in sim.cpp are guarded by TARGET_SIMULATOR, so
 * this file drops out of the OPTA / Arduino-library build (where the real
 * coprocessor plays this role). Included only by the simulator harness.
 */

#include <cstdint>

#include "sites/gh_mlb41/payload.h"

// Forward declarations suffice here (start_greenhouse_emulator() only takes
// pointers); full definitions come in via sim.cpp.
namespace Botanik {
    namespace PlcIO { template<typename> struct PlcInput; template<typename> struct PlcOutput; }
    namespace Logs  { class Logger; }
}

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
 * bound to the shared PLC memory, animating the greenhouse payload (see sim.cpp).
 * On OPTA the real coprocessor plays this role.
 *
 * Must be called before the process is locked into realtime (see the call site).
 */
void start_greenhouse_emulator(Botanik::PlcIO::PlcInput<PlcInputData_t>*   shared_in,
                               Botanik::PlcIO::PlcOutput<PlcOutputData_t>* shared_out,
                               Botanik::Logs::Logger*                      logger);

#endif // SITES_GH_MLB41_SIM_H
