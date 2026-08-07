#ifndef SITE_H
#define SITE_H

/**
 * @file site.h
 * @brief HAND-WRITTEN - the gh_mlb41 site: its shell commands, the descriptor
 *        bundling every table, the firmware binding, and the uniform handle
 *        every target harness builds against.
 *
 * Not generated: this is the file you own and the generator never overwrites.
 * It stays small because a site is data, and the data lives in the generated
 * tables this pulls in:
 *
 *   vars.h    GENERATED  everything declarative - every variable in one table
 *                        (parameters, alarms, sensor inputs, computed and status
 *                        values, actuator outputs), the analog calibration
 *                        blocks, the home-screen readings, and the RAM value
 *                        store behind them
 *   tasks.h   hand-written  the control programs
 *   sim.h     hand-written  the simulated field
 *
 * What used to be separate files and is not any more: `descriptor.h` (one struct
 * literal, below), `commands.h` (one table, below), `alarms.h` (an alarm is a
 * SCOPE_A row in vars.h), `status.h` (a group is a name prefix; the home fields
 * moved into vars.h), `vars.cpp` (the value store is header-only) and
 * `botanik.h` (the library is reached as `libbotanik.h`).
 *
 * `config.h` stays its own file. It CAN be folded in here - libbotanik_config.h
 * would include this header in a first pass, guarded by a macro, and read only
 * the #defines - but that makes one file answer two questions and be enterable
 * twice, which is a lot of cleverness to save a file that says five things.
 *
 * Every site exposes the same surface, so a harness never names the site:
 *   - `BotanikSite`       the firmware type to instantiate
 *   - `Site::simulate()`  the simulated field (sim.h)
 *   - `dilated_timeout()` sim-only timeout dilation (sim.h)
 *
 * A target selects a site by directory (-DBOTANIK_SITE_DIR, or -DBOTANIK_SITE
 * for one in the library tree); CMake points BOTANIK_SITE_HEADER at this file.
 */

#include <cstdint>      // uint8_t/uint16_t

#include "libbotanik.h" // BotanikFirmware, SiteDescriptor, ShellCommand, Scan::Task

// Site-internal includes are RELATIVE on purpose, and must stay that way.
// A site can live outside this tree (-DBOTANIK_SITE_DIR): CMake passes its
// site.h as an absolute BOTANIK_SITE_HEADER and adds no include directory for
// it, so "sites/gh_mlb41/vars.h" would resolve to the IN-TREE reference site
// instead of the one being built - silently, with the wrong variable table.
// A quoted relative include resolves against this file's own directory, which
// is always the right site.
#include "tasks.h"
#include "vars.h"

namespace Site {

/** @brief The site's shell commands: name, one-line help, handler.
 *
 *  A command is code, not schema. They are matched BEFORE the library built-ins,
 *  so a name reused here shadows one - avoid `exit` / `reset`. The ctx handed to
 *  each handler is the live `const Vars::Bank*`, wired by the firmware base, so
 *  a command reads fresh variable data without holding state of its own. */
const Botanik::Shell::ShellCommand* const g_commands = nullptr;
const uint8_t g_command_count = 0;

/** @brief The whole handoff to the library: name + the site's tables. Field
 *  order matches Botanik::SiteDescriptor; any table may be null / count 0.
 *
 *  Alarms are not here - each is a SCOPE_A row in vars.h carrying its own code
 *  and dwell policy, and the AlarmController finds them by scanning. Neither are the
 *  status groups - a variable's group is the prefix of its name. */
/** @name Field devices - what each unit on the RS-485 segment will answer
 *
 *  Constraints, not signal maps: where each reading lives is the row's business
 *  (vars.h), and this is only what a request has to look like for the device to
 *  answer it at all. See modbus/rtu/device.h.
 *  @{ */

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

/** @brief Eastron SDM630-Modbus V2. Manual V1.6B:
 *   - "a maximum of 40 values in a single transaction; therefore the maximum
 *     number of registers requestable is 80";
 *   - "the Starting Address and the Number of Points must be even numbers to
 *     request a floating point variable" - odd either way returns an error;
 *   - supports FC03/FC04/FC16, and holding registers are "write to only one
 *     parameter per message", so multi-write is not usable for this model. */
const Botanik::Modbus::DeviceModel MDL_SDM630 = {
    "Eastron SDM630", 80, 0, 2, 2, false, 0
};

/** @brief Lettel MCT1-140. Its table gives a max of 100 for the quantity field
 *  and stores every measurement as a register-pair Float, so the same pair
 *  alignment applies. Write-multiple is unused here - nothing writes to it. */
const Botanik::Modbus::DeviceModel MDL_MCT1_140 = {
    "Lettel MCT1-140", 100, 0, 2, 2, false, 0
};

/** @brief Waveshare Modbus RTU Analog Input 8CH (25767), "Development Protocol".
 *
 *  Speaks 03/04/06/10. The eight channel readings are INPUT registers at
 *  3x0000..3x0007 and are read with 0x04; its holding registers start at 4x1000
 *  (per-channel range) and there is nothing at 4x0000, so a read of the channels
 *  with 0x03 draws exception 02. Quantity "must not exceed the maximum number of
 *  the channels" - eight. One register per channel, so no alignment rule. */
const Botanik::Modbus::DeviceModel MDL_WS_AI8 = {
    "Waveshare AI 8CH", 8, 0, 1, 1, true, 0
};

/** @brief Waveshare Modbus RTU Analog Output 8CH (26211).
 *
 *  Speaks 03/06/10 - no 0x04 and no coils. The eight outputs are HOLDING
 *  registers at 4x0000..4x0007, read and written, so 0x03/0x06/0x10 all apply
 *  and the multi-write is available. */
const Botanik::Modbus::DeviceModel MDL_WS_AO8 = {
    "Waveshare AO 8CH", 8, 0, 1, 1, true, 0
};

/** @brief Waveshare Modbus RTU Relay (D) (26517), "Development Protocol V2".
 *
 *  Speaks 01/02/03/05/06/0F/10. The eight relays are coils at 0x0000..0x0007,
 *  read with 0x01 and written with 0x05 or 0x0F - so the multi-write is there,
 *  and a block may cover at most the eight relays the board has. (0x00FF is the
 *  all-relays broadcast and 0x0200/0x0400 are the flash-on/off timers; none of
 *  them is a coil a row should name.) */
const Botanik::Modbus::DeviceModel MDL_WS_RELAY = {
    "Waveshare Relay (D)", 0, 8, 1, 1, true, 0
};

const Botanik::Modbus::Device g_devices[] = {
    {  5, &MDL_SDM630 },      // SRR/TAC/MTR-1
    {  6, &MDL_MCT1_140 },    // SRR/TAC/MTR-2
    { 11, &MDL_WS_AI8 },      // SRR/TDC/AI-1
    { 16, &MDL_WS_AO8 },      // SRR/TDC/AO-1
    { 31, &MDL_WS_RELAY },    // SRR/TDC/RO-1
    { 32, &MDL_WS_RELAY },    // SRR/TDC/RO-2
    { 33, &MDL_WS_RELAY },    // SRR/TDC/RO-3
    { 34, &MDL_WS_RELAY },    // SRR/TDC/RO-4
};

const Botanik::Modbus::DeviceTable g_device_table = {
    g_devices, (uint8_t)(sizeof g_devices / sizeof g_devices[0])
};

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT

/** @} */

const Botanik::SiteDescriptor g_site = {
    Botanik::Config::SITE_NAME,
    g_commands,      g_command_count,                    // site shell commands
    g_home_fields,   g_home_field_count,  Store::home_refs,  // home-screen readings
    SITE_VAR_TABLES,                                     // variables + values + string pool
    g_programs,      g_program_count,                    // control programs
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
    &g_device_table,                                     // what each unit answers
#endif
};

}  // namespace Site

// ===========================================================================
// GreenhouseMLB41 - the gh_mlb41 site firmware (derives BotanikFirmware)
// ===========================================================================

/**
 * @brief The gh_mlb41 greenhouse firmware.
 *
 * A thin binding of the generic base to this site's descriptor: it declares no
 * pages and overrides no hooks. The base owns every page
 * (home/shell/logs/alarms/params/monitor) and wires the descriptor generically,
 * so the site is data, not code. A site that needs custom per-beat logic or an
 * extra page (e.g. re-adding a STATUS page by subclassing Pages::StatusPage)
 * would override the base's on_step() / register_pages() here. See
 * libbotanik/firmware/firmware.h for the extension model.
 */
class GreenhouseMLB41 final : public Botanik::BotanikFirmware {
public:
    /** @brief Hand the base this site's descriptor; the base owns the data
     *  struct the tables address. */
    GreenhouseMLB41()
        : Botanik::BotanikFirmware(Site::g_site)
    {}
};

// The simulated field (Site::simulate()). Body compiles only under
// TARGET_SIMULATOR; near-empty on OPTA, where the real field plays that role.
// Included after the variable tables it animates.
#include "sim.h"        // Site::simulate

/** @brief The firmware type the harness instantiates for this site. */
using BotanikSite = GreenhouseMLB41;

#endif // SITE_H
