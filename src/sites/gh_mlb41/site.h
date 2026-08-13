#ifndef SITE_H
#define SITE_H

/**
 * @file site.h
 * @brief HAND-WRITTEN - the gh_mlb41 site: its shell commands, the descriptor
 *        bundling every table, the firmware binding, and the uniform handle
 *        every target harness builds against.
 *
 * Not generated: the generator never overwrites this file. A site is data, and
 * the data lives in the tables it pulls in:
 *
 *   vars.h    GENERATED     every variable in one table, the analog calibration
 *                           blocks, the home-screen readings, the value store
 *   tasks.h   hand-written  the control programs
 *   sim.h     hand-written  the simulated field
 *
 * `config.h` stays its own file: CMake includes it before the library defaults.
 *
 * Every site exposes the same surface, so a harness never names the site:
 *   - `BotanikSite`       the firmware type to instantiate
 *   - `Site::simulate()`  the simulated field (sim.h)
 *
 * A target selects a site by directory (-DBOTANIK_SITE_DIR, or -DBOTANIK_SITE
 * for one in the library tree); CMake points BOTANIK_SITE_HEADER at this file.
 */

#include <cstdint>      // uint8_t/uint16_t

#include "libbotanik.h" // BotanikFirmware, SiteDescriptor, ShellCommand, Scan::Task

// Site-internal includes are RELATIVE on purpose, and must stay that way. A
// site can live outside this tree (-DBOTANIK_SITE_DIR), with no include
// directory of its own, so "sites/gh_mlb41/vars.h" would silently resolve to the
// IN-TREE reference site instead of the one being built.
#include "tasks.h"
#include "vars.h"

namespace Site {

/** @brief The site's shell commands (Botanik::Shell::ShellCommand): none here.
 *  A site table is matched BEFORE the library built-ins, so a name reused there
 *  shadows one - avoid `exit` / `reset`. */
const Botanik::Shell::ShellCommand* const g_commands = nullptr;
const uint8_t g_command_count = 0;

/** @brief The whole handoff to the library: name + the site's tables. Field
 *  order matches Botanik::SiteDescriptor; any table may be null / count 0.
 *
 *  Alarms are not here (a SCOPE_A row in vars.h) and neither are the status
 *  groups (a variable's group is its name prefix). */
/** @name Field devices - what each unit on the RS-485 segment will answer
 *
 *  Constraints, not signal maps: where each reading lives is the row's business
 *  (vars.h); this is what a request must look like for the device to answer at
 *  all. See modbus/rtu/device.h.
 *  @{ */

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

/** @brief Eastron SDM630-Modbus V2. Manual V1.6B: 40 values (80 registers) per
 *  transaction, start address and count must both be even for a float, and
 *  holding registers take one parameter per message (so no multi-write). */
const Botanik::Modbus::DeviceModel MDL_SDM630 =
    Botanik::Modbus::meter("Eastron SDM630", 80, 2);

/** @brief Lettel MCT1-140. Max quantity 100, every measurement a register-pair
 *  Float, so the same pair alignment applies. Nothing writes to it. */
const Botanik::Modbus::DeviceModel MDL_MCT1_140 =
    Botanik::Modbus::meter("Lettel MCT1-140", 100, 2);

/** @brief Waveshare Modbus RTU Analog Input 8CH (25767), "Development Protocol".
 *
 *  The eight readings are INPUT registers at 3x0000..3x0007, read with 0x04;
 *  nothing sits at 4x0000, so reading them with 0x03 draws exception 02.
 *  Quantity is capped at the eight channels, one register each (no alignment). */
const Botanik::Modbus::DeviceModel MDL_WS_AI8 =
    Botanik::Modbus::io_board("Waveshare AI 8CH", 8);

/** @brief Waveshare Modbus RTU Analog Output 8CH (26211). Speaks 03/06/10 - no
 *  0x04 and no coils. The eight outputs are HOLDING registers at
 *  4x0000..4x0007, read and written, so the multi-write is available. */
const Botanik::Modbus::DeviceModel MDL_WS_AO8 =
    Botanik::Modbus::io_board("Waveshare AO 8CH", 8);

/** @brief Waveshare Modbus RTU Relay (D) (26517), "Development Protocol V2".
 *
 *  The eight relays are coils at 0x0000..0x0007, read with 0x01 and written with
 *  0x05 or 0x0F, so a block may cover at most those eight. (0x00FF is the
 *  all-relays broadcast and 0x0200/0x0400 the flash timers; none is a coil a row
 *  should name.) */
const Botanik::Modbus::DeviceModel MDL_WS_RELAY =
    Botanik::Modbus::relay_board("Waveshare Relay (D)", 8);

/** @brief The temperature / humidity sensors: an f32 at 0 (temperature) and
 *  another at 2 (humidity), so four registers, pair-aligned and read-only.
 *
 *  @note FC04 is assumed, matching every other measurement on this bus. If these
 *  sensors answer only FC03 it shows as exception 02 on all twelve at once, and
 *  the fix is one character per row in vars.h. */
const Botanik::Modbus::DeviceModel MDL_TEMP_RH =
    Botanik::Modbus::meter("Temp/RH sensor", 4, 2);

/** @brief Every unit on the segment, and what the plant calls it: the tag is
 *  data, so a log line can name the cabinet. */
const Botanik::Modbus::Device g_devices[] = {
    {  5, &MDL_SDM630,   "SRR/TAC/MTR-1" },
    {  6, &MDL_MCT1_140, "SRR/TAC/MTR-2" },
    { 11, &MDL_WS_AI8,   "SRR/TDC/AI-1" },
    { 16, &MDL_WS_AO8,   "SRR/TDC/AO-1" },
    { 31, &MDL_WS_RELAY, "SRR/TDC/RO-1" },
    { 32, &MDL_WS_RELAY, "SRR/TDC/RO-2" },
    { 33, &MDL_WS_RELAY, "SRR/TDC/RO-3" },
    { 34, &MDL_WS_RELAY, "SRR/TDC/RO-4" },
    { 41, &MDL_TEMP_RH,  "SRR/CLI/TRH-EXT-1" },
    { 42, &MDL_TEMP_RH,  "SRR/CLI/TRH-EXT-2" },
    { 43, &MDL_TEMP_RH,  "SRR/CLI/TRH-EXT-3" },
    { 44, &MDL_TEMP_RH,  "SRR/CLI/TRH-CS-1" },
    { 45, &MDL_TEMP_RH,  "SRR/CLI/TRH-CS-2" },
    { 46, &MDL_TEMP_RH,  "SRR/CLI/TRH-CS-3" },
    { 47, &MDL_TEMP_RH,  "SRR/CLI/TRH-LO-1" },
    { 48, &MDL_TEMP_RH,  "SRR/CLI/TRH-LO-2" },
    { 49, &MDL_TEMP_RH,  "SRR/CLI/TRH-LO-3" },
    { 50, &MDL_TEMP_RH,  "SRR/CLI/TRH-HI-1" },
    { 51, &MDL_TEMP_RH,  "SRR/CLI/TRH-HI-2" },
    { 52, &MDL_TEMP_RH,  "SRR/CLI/TRH-HI-3" },
};

const Botanik::Modbus::DeviceTable g_device_table = {
    g_devices, (uint8_t)(sizeof g_devices / sizeof g_devices[0])
};

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT

/** @} */

/** @brief Every table this greenhouse declares, in the one struct the firmware
 *  base takes: the site is data, and this is all of it. */
const Botanik::SiteDescriptor g_site = {
    Botanik::Config::SITE_NAME,
    g_commands,      g_command_count,                    // site shell commands
    g_home_fields,   g_home_field_count,  Store::home_refs,  // home-screen readings
    SITE_VAR_TABLES,                                     // variables + values + string pool
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
 * pages and overrides no hooks. A site needing custom per-beat logic or an extra
 * page overrides one of the base's phase hooks / register_pages() here; see
 * libbotanik/firmware/firmware.h for the extension model.
 */
class GreenhouseMLB41 final : public Botanik::BotanikFirmware {

    /** @name Control programs
     *
     *  Plain members, in execution order: registering is what puts them in the
     *  beat, so there is no table of pointers and nothing to keep in step with
     *  one. Being members, there is exactly one of each by construction - which
     *  a header-declared static could not promise (a namespace-scope `static` is
     *  internal linkage, so every including TU would get its own).
     *  @{ */

    Site::SupplyWatch  _supply_watch;
    Site::SupplyReport _supply_report;

    /** @} */

public:
    /** @brief Hand the base this site's descriptor. */
    GreenhouseMLB41()
        : Botanik::BotanikFirmware(Site::g_site),
          // The base is fully constructed before any derived member, so io()
          // and logger() are already alive here: a program is handed the field
          // it drives at construction rather than through a later attach().
          _supply_watch(io(), &logger()),
          _supply_report(io(), &logger())
    {}

protected:
    /** @brief Register what this site adds to the beat. Order here is execution
     *  order within PHASE_CONTROL, so a law that computes a setpoint goes before
     *  the law that acts on it. */
    void on_setup() override {
        _supply_watch.register_tasks(scheduler().tasks());
        _supply_report.register_tasks(scheduler().tasks());
    }
};

// The simulated field. Body compiles only under TARGET_SIMULATOR, and is
// included after the variable tables it animates.
#include "sim.h"        // Site::simulate

/** @brief The firmware type the harness instantiates for this site. */
using BotanikSite = GreenhouseMLB41;

#endif // SITE_H
