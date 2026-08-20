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

    /** @brief Where the sun is. Site-owned, as the library leaves it: a
     *  greenhouse has one location and the base has no opinion about it.
     *  Declared FIRST, so it is alive when day_phase takes a reference. */
    Botanik::Sun::SunPositionTracker _sun{setup_context()};

    /** @name Control programs
     *
     *  Plain members, in execution order: registering is what puts them in the
     *  beat, so there is no table of pointers and nothing to keep in step with
     *  one. Being members, there is exactly one of each by construction - which
     *  a header-declared static could not promise (a namespace-scope `static` is
     *  internal linkage, so every including TU would get its own).
     *  @{ */

    /** @name The control programs
     *  Declared, constructed and registered in SIGNAL ORDER: conditioning,
     *  then the day's phase, then the three ladders. Registration order is
     *  execution order within a phase, so a law reading a value computed this
     *  beat finds it already written. @see docs/dev/scan.md */

    Site::AcGridWatch        _ac_grid;
    Site::AcLocalWatch       _ac_local;
    Site::DcWatch            _dc;
    Site::TempRHZone         _zones;
    Site::StratSensor        _strat;
    Site::ClimateWatch       _climate_watch;
    Site::DayPhaseController _day_phase;
    Site::TargetTempController _target;
    Site::CoolingController  _cooling;
    Site::DestratController  _destrat;
    Site::HumidityController _humidity;
    Site::IrrigationModulation _irr_mod;
    Site::WaterMeters          _irr_meters;
    Site::IrrigationCycle      _irrigation;
    Site::ScreenController   _screen;
    Site::WindowsController  _windows;
    Site::WeatherWatch       _weather_watch;
    Site::IntractorController _intractors;
    Site::FanController      _fans;
    Site::SkirtController    _skirt;

    /** @} */

public:
    /** @brief Hand the base this site's descriptor. */
    GreenhouseMLB41()
        : Botanik::BotanikFirmware(Site::g_site),
          // The base is fully constructed before any derived member, so
          // setup_context() and io() are already alive here: a program is
          // handed the table it registers in and the field it drives at
          // construction, rather than through a later attach().
          _ac_grid(setup_context(), io()),
          _ac_local(setup_context(), io()),
          _dc(setup_context(), io()),
          _zones(setup_context(), io()),
          _strat(setup_context(), io()),
          _climate_watch(setup_context(), io()),
          _day_phase(setup_context(), io(), _sun),
          _target(setup_context(), io()),
          _cooling(setup_context(), io()),
          _destrat(setup_context(), io()),
          _humidity(setup_context(), io()),
          _irr_mod(setup_context(), io()),
          _irr_meters(setup_context(), io()),
          _irrigation(setup_context(), io()),
          _screen(setup_context(), io()),
          _windows(setup_context(), io()),
          _weather_watch(setup_context(), io()),
          _intractors(setup_context(), io()),
          _fans(setup_context(), io()),
          _skirt(setup_context(), io())
    {}

protected:
    /** @brief Register what this site adds to the beat. Order here is execution
     *  order within PHASE_CONTROL, so a law that computes a setpoint goes before
     *  the law that acts on it. */
    /** @brief Hand the bank this site's rows. The generated register_all()
     *  holds the parameters-first ordering the ROLE_PARAM run depends on. */
    void register_vars() override {
        Site::register_all(io());
    }

    void on_setup() override {
        // The geo pair reaches it on the commit, and again on every edit.
        Botanik::Vars::bind_sun(config_binder(), _sun);
        _sun.register_tasks();

        // The edge-counted terminals, from the table the generator built out of
        // the `isr` column: the channel a row declares and the channel the
        // firmware attaches cannot part. Without this the counter never runs
        // and the inlet meter reads nothing at all.
        for (uint8_t i = 0; i < Site::pulse_input_count; i++)
            Botanik::HAL::Pulse::attach(Site::pulse_inputs[i].channel,
                                        Site::pulse_inputs[i].edge);

        _ac_grid.register_tasks();
        _ac_local.register_tasks();
        _dc.register_tasks();
        _zones.register_tasks();
        _strat.register_tasks();
        _climate_watch.register_tasks();
        _day_phase.register_tasks();
        _target.register_tasks();
        _cooling.register_tasks();
        _destrat.register_tasks();
        _humidity.register_tasks();
        // The ladder, then the instruments, then the law that acts on both.
        _irr_mod.register_tasks();
        _irr_meters.register_tasks();
        _irrigation.register_tasks();
        _screen.register_tasks();
        _windows.register_tasks();
        _weather_watch.register_tasks();
        _intractors.register_tasks();
        _fans.register_tasks();
        _skirt.register_tasks();
    }
};

// The simulated field. Body compiles only under TARGET_SIMULATOR, and is
// included after the variable tables it animates.
#include "sim.h"

/** @brief The firmware type the harness instantiates for this site. */
using BotanikSite = GreenhouseMLB41;

#endif // SITE_H
