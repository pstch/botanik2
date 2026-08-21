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
 *                           blocks, the field devices, the home-screen readings
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

#include <cstdint>

#include "libbotanik.h"

// Site-internal includes are RELATIVE on purpose, and must stay that way. A
// site can live outside this tree (-DBOTANIK_SITE_DIR), with no include
// directory of its own, so "sites/gh_mlb41/vars.h" would silently resolve to the
// IN-TREE reference site instead of the one being built.
#include "tasks.h"
#include "vars.h"

#include "generated/programs.gen.h"

namespace Site {

/** @brief The site's shell commands (Botanik::Shell::ShellCommand): none here.
 *  A site table is matched BEFORE the library built-ins, so a name reused there
 *  shadows one - avoid `exit` / `reset`. */
const Botanik::Shell::ShellCommand* const g_commands = nullptr;
const uint8_t g_command_count = 0;

/** @brief The whole handoff to the library: name + the site's tables. Field
 *  order matches Botanik::SiteDescriptor; any table may be null / count 0.
 *
 *  Alarms are not here (a SCOPE_A row in the variable table) and neither are the
 *  status groups (a variable's group is its name prefix). The device table is
 *  generated, from tables/devices.csv. */
const Botanik::SiteDescriptor g_site = {
    Botanik::Config::SITE_NAME,
    g_commands,     g_command_count,                     // site shell commands
    g_home_fields,  g_home_field_count, home_refs,       // home-screen readings
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
    &g_device_table,                                     // what each unit answers
#endif
};

}  // namespace Site

// GreenhouseMLB41 - the gh_mlb41 site firmware (derives BotanikFirmware)

/**
 * @brief The gh_mlb41 greenhouse firmware.
 *
 * A thin binding of the generic base to this site's descriptor: it declares no
 * pages, and overrides only register_vars() and on_setup(). A site needing an
 * extra page or its own component overrides register_pages() / on_setup() here;
 * see libbotanik/firmware/firmware.h for the extension model.
 */
class GreenhouseMLB41 final : public Botanik::BotanikFirmware {

    /** @name Control programs
     *
     *  One list, from generated/programs.gen.h, expanded here for the members,
     *  below for the constructor init, and again for the registration. Being
     *  members, there is exactly one of each by construction - which a
     *  header-declared static could not promise (a namespace-scope `static` is
     *  internal linkage, so every including TU would get its own).
     *
     *  The order is the table's, and it is EXECUTION order: registering is what
     *  puts a program in the beat, so a law reading a value computed this beat
     *  finds it already written. @see docs/dev/scan.md
     *  @{ */

#define X(cls, mem) Site::cls mem;
    SITE_PROGRAM_LIST(X)
#undef X

    /** @} */

public:
    /** @brief Hand the base this site's descriptor. */
    GreenhouseMLB41()
        : Botanik::BotanikFirmware(Site::g_site)
          // The base is fully constructed before any derived member, so
          // setup_context() and io() are already alive here: a program is
          // handed the table it registers in and the field it drives at
          // construction, rather than through a later attach().
#define X(cls, mem) , mem(setup_context(), io())
          SITE_PROGRAM_LIST(X)
#undef X
    {}

protected:
    /** @brief Hand the bank this site's rows. The generated register_all()
     *  holds the parameters-first ordering the ROLE_PARAM run depends on. */
    void register_vars() override {
        Site::register_all(io());
    }

    void on_setup() override {
        // The geo pair reaches the tracker on the commit, and again on every
        // edit. sun_watch owns it and registers it with its own tasks.
        Botanik::Vars::bind_sun(config_binder(), _sun_watch.tracker());

        // The edge-counted terminals, from the table the generator built out of
        // the `isr` column: the channel a row declares and the channel the
        // firmware attaches cannot part. Without this the counter never runs
        // and the inlet meter reads nothing at all.
        for (uint8_t i = 0; i < Site::pulse_input_count; i++)
            Botanik::HAL::Pulse::attach(Site::pulse_inputs[i].channel,
                                        Site::pulse_inputs[i].edge);

#define X(cls, mem) mem.register_tasks();
        SITE_PROGRAM_LIST(X)
#undef X
    }
};

// The simulated field. Body compiles only under TARGET_SIMULATOR, and is
// included after the variable tables it animates.
#include "sim.h"

/** @brief The firmware type the harness instantiates for this site. */
using BotanikSite = GreenhouseMLB41;

#endif // SITE_H
