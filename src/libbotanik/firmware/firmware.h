// REVIEW_35: TODO
#ifndef LIBBOTANIK_FIRMWARE_FIRMWARE_H
#define LIBBOTANIK_FIRMWARE_FIRMWARE_H

/**
 * @file firmware.h
 * @brief BotanikFirmware - the generic firmware base: owns every long-lived
 *        generic component and the per-tick loop() order, exposing an accessor
 *        for each.
 *
 * A site is DATA: it fills a SiteDescriptor and derives a thin
 * `GreenhouseXXX : BotanikFirmware`, customizing only through register_*(),
 * on_setup() and the six per-phase hooks. See docs/integration.
 * @note Both UART rates are PARAMETERS, not compile-time constants
 *       (`console_baud`, `monitor_baud`), and reach their port through
 *       ConfigBinder like every other setting.
 */

#include <cstdint>

#include "libbotanik/config/identity.h"
#include "libbotanik/config/scheduler.h"
#include "libbotanik/config/serial.h"
#include "libbotanik/config/terminal.h"

#include "libbotanik/alarms/alarms.h"
#include "libbotanik/alarms/publisher.h"
#include "libbotanik/core/context.h"
#include "libbotanik/firmware/stacks.h"
#include "libbotanik/health/health.h"
#include "libbotanik/io/pulse_input.h"
#include "libbotanik/io/local_inputs.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/logs/topics.h"
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
#include "libbotanik/modbus/rtu/client.h"
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
#include "libbotanik/modbus/tcp/slave.h"
#endif
#include "libbotanik/plc/program.h"
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/sun/sun_tracker.h"
#include "libbotanik/shell/shell.h"
#include "libbotanik/tasks/tasks.h"
#include "libbotanik/utils/assert.h"
#include "libbotanik/utils/serial.h"
#include "libbotanik/vars/access.h"
#include "libbotanik/vars/bindings.h"
#include "libbotanik/vars/model.h"
#include "libbotanik/vars/quality_mirror.h"
#include "libbotanik/vars/params.h"
#include "libbotanik/vars/vars.h"

#if BOTANIK_FEATURE_CONSOLE
#include "libbotanik/console/console.h"
#endif
#if BOTANIK_FEATURE_HOME_PAGE
#include "libbotanik/pages/home.h"
#endif
#if BOTANIK_FEATURE_PARAMS_PAGE
#include "libbotanik/pages/params_page.h"
#endif
#if BOTANIK_FEATURE_MONITOR_PAGE
#include "libbotanik/pages/monitor.h"
#endif
#if BOTANIK_FEATURE_TASKS_PAGE
#include "libbotanik/pages/tasks_page.h"
#endif

namespace Botanik {

    /**
     * @brief The declarative description of a site: its name plus its tables.
     * @note Alarms are NOT a table here - each is a SCOPE_A row in @c vars, and
     *       parameters are the ROLE_PARAM rows within it.
     * @note Any table may be null / count 0. Every pointer is borrowed and must
     *       outlive the firmware.
     */
    struct SiteDescriptor {
        const char*                   name;                ///< == Config::SITE_NAME
        const Shell::ShellCommand*    commands;            ///< site shell commands
        uint8_t                       command_count;
        /** @brief Home-screen readings, and where their names resolve to -
         *  filled at bring-up, site-owned, @c home_field_count entries parallel
         *  to the table so the labels stay in flash.
         *  @note NOT gated on VARS, though only a VARS build reads them: a field
         *  a flag can remove is a field every site's initialiser has to guard.
         *  Three borrowed pointers of flash is the cheaper side of that trade. */
        const Vars::HomeVar*          home_fields;
        uint8_t                       home_field_count;
        Vars::VarRef*                 home_refs;
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
        /** @brief What each unit on the field bus will answer: its request
         *  ceiling and address alignment. Null plans every unit as
         *  generic_model(). Last, because every site brace-inits this
         *  aggregate and a field added anywhere else moves the rest. */
        const Modbus::DeviceTable*    devices;
#endif
        /** @brief The edge-counted terminals. LAST, after the guarded field,
         *  so a build without the field bus does not move it. */
        const PulseInput*             pulses;
        uint8_t                       pulse_count;
    };

    /** @brief Generic firmware base: owns the generic components + the loop
     *  order, and exposes each for a site subclass to build on. */
    class BotanikFirmware {

        /** @name Configuration
         *  @{ */

        /** @brief The site's declarative tables + name (borrowed; site statics
         *  outlive the firmware). Drives page/command/telemetry/config wiring. */
        SiteDescriptor                _site;

        /** @brief HW-watchdog period armed in setup(); a harness overrides it
         *  via set_hw_watchdog_ms() before setup(). */
        uint32_t                      _hw_watchdog_ms = Config::HW_WATCHDOG_MS;


        /** @} */

        /** @name Generic components (declared in construction order)
         *  The optional subsystem groups are bundled into the feature-gated
         *  stacks (firmware/stacks.h); the firmware owns the shared singletons and
         *  injects them into the stacks by pointer. @{ */

        /** @brief What runs in the beat. Declared FIRST, before the logger and
         *  the scheduler, because both are constructed with it: a Component is
         *  built with the registry it will register in, and the Logger is a
         *  Component. The scheduler borrows it. */
        Tasks::Registry                _tasks;
        Logs::Logger                   _logger;
        Scheduler::LoopScheduler       _scheduler;
        /** @brief What every Component below is built with: the table to
         *  register in and the log to speak through. @see SetupContext */
        SetupContext                   _setup;
#if BOTANIK_FEATURE_VARS
        Vars::Bank                       _io;          ///< the site's variables
        Vars::KvParamStore             _param_store; ///< one storage key per parameter
        /** @brief The one bridge from the table to a component's own config
         *  struct. Bindings are added in the ctor body, once every component
         *  exists. @see vars/bindings.h */
        /** @brief Publishes a field row's quality into its `_qlt` companion,
         *  for laws that cannot ask the bank. @see Vars::QualityMirror */
        Vars::QualityMirror            _qmirror;
        Vars::ConfigBinder             _binder;
#endif
#if BOTANIK_FEATURE_SUN_TRACKER
        /** @brief The solar-position instrument. Owned here rather than by a
         *  site: it is library code, one build has at most one, and every
         *  program that wants the sun reads it through SetupContext. */
        Sun::SunPositionTracker        _sun;
#endif
        Health::HealthMonitor          _health;
        SystemContext                  _sys_ctx;     ///< Filled in the ctor body
#if BOTANIK_FEATURE_LOGS_MONITOR
        MonitorStack                   _monitor;     ///< MONITOR UART + log sink
#endif
#if BOTANIK_FEATURE_NETWORK
        NetworkStack                   _net;         ///< resolver/network/ntp/mqtt
#endif
#if BOTANIK_FEATURE_VARS
        /** @brief The controller's own input terminals. Not gated on a field-bus
         *  flag: local wiring is there whether or not the site has a bus. */
        Io::LocalInputs                _local_in;
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
        /** @brief The field-bus master. Stepped from two phases like the slave
         *  below, and for the mirror-image reason: it takes the field in before
         *  the programs run, and hands their demands out after. */
        Modbus::RtuClient              _modbus_rtu;
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
        /** @brief The Modbus TCP slave. NOT part of NetworkStack, which is
         *  stepped as one bundle in PHASE_SERVICE: this is stepped from two
         *  phases (requests in before the programs run, answers out after), so
         *  it has to be a member the task table can name twice. It also WRITES
         *  the bank, where the stack's members only read config from it. */
        Modbus::TcpSlave               _modbus_tcp;
#endif
#if BOTANIK_FEATURE_CONSOLE
        ConsoleStack                   _console;     ///< console UART + terminal + pages
        // Site-independent pages the base owns, declared after _console so
        // terminal() is live.
#if BOTANIK_FEATURE_HOME_PAGE
        Pages::HomePage                _home_page;
#endif
#if BOTANIK_FEATURE_PARAMS_PAGE
        Pages::ParamsPage              _params_page;
#endif
#if BOTANIK_FEATURE_MONITOR_PAGE
        Pages::MonitorPage             _monitor_page;
#endif
#if BOTANIK_FEATURE_TASKS_PAGE
        Pages::TasksPage               _tasks_page;
#endif
#endif

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Wire up the generic components. @p site: the site's
         *  declarative tables + name (borrowed). The watchdog period defaults
         *  from Config; override via set_hw_watchdog_ms() before setup(). The
         *  baud rates are parameters - see the file header. */
        explicit BotanikFirmware(const SiteDescriptor& site);

        virtual ~BotanikFirmware() {}

        /** @brief Bring-up in a fixed, non-overridable order: UARTs, core sinks,
         *  register_pages(), terminal type, banner, Registry::setup() (POST and
         *  the parameter load), watchdog. None of it runs on a beat. A site
         *  customizes via the hooks, never setup(). */
        void setup();

        /** @brief The task table, for the TASKS page and the `stats` command. */
        const Tasks::Registry& tasks() const { return _tasks; }
        /** @brief The table a site registers its own components and programs
         *  with, from on_setup(). Non-const: registering mutates it, and this
         *  is the one moment it is meant to. */
        Tasks::Registry&       tasks()       { return _tasks; }

        /** @brief What a site constructs its own components and programs with:
         *  the table they register in and the log they speak through. Named
         *  setup_context() rather than setup(), which is the bring-up entry
         *  point. @see SetupContext */
        const SetupContext&    setup_context() const { return _setup; }

        /** @brief One main-loop iteration. Phase-major: each phase runs the
         *  tasks registered to it whose group has come due. The PHASE order is
         *  load-bearing and stays written out as code; a task's RATE is its own
         *  property, so retuning one can never reorder the cycle.
         *  @note Non-virtual, and inline because the scheduler's tick() paints
         *  the stack below its caller's frame - this is the frame the high-water
         *  mark is taken against.
         *  @return whether a beat actually ran. A harness interleaving its own
         *  work must key off this: tick() busy-waits the tail of every period. */
        bool loop() {
            if (!_scheduler.tick()) return false;   // driver: gates the beat, produces the clock

            // One 64-bit "now" for every Component stepped this beat.
            uint64_t now_ms = HAL::Clock::uptime_ms();

            // HALTED: the plant has reached its safe state and the operator
            // asked for the cycle to stop rather than for a reset. Nothing runs
            // from here - not the console either - and the kick below still
            // goes out, because the loop IS alive. Recovery is `reset`.
            if (_health.halted()) {
                (void)_scheduler.kick(true);
                return true;
            }

            _scheduler.run_beat(now_ms);

            // The escape hatch, after the whole beat and before the kick that
            // vouches for it. Empty by default.
            on_loop(now_ms);

            // The verdict is HealthMonitor's, the watchdog is the scheduler's.
            // A withheld kick chooses to let the hardware reset us, so the field
            // drops to its safe state FIRST: the reset gives no such guarantee,
            // and the gap before the reboot is exactly when an actuator must
            // not be left energised.
            if (!_scheduler.kick(_health.watchdog_ok())) {
#if BOTANIK_FEATURE_VARS
#endif
            }
            return true;
        }

        /** @brief Run loop() forever (the OPTA entry point; the simulator
         *  drives loop() itself so it can break on a shutdown signal). */
        void run() { for (;;) loop(); }

        /** @} */

        /** @name Per-target tuning (call before setup())
         *  @{ */

        /** @brief Override the HW-watchdog period armed by setup(). */
        void set_hw_watchdog_ms(uint32_t ms) { _hw_watchdog_ms = ms; }


        /** @} */


        /** @name Component access (for the site subclass / harness)
         *  @{ */

        Logs::Logger&                   logger()          { return _logger; }
        Scheduler::LoopScheduler&       scheduler()       { return _scheduler; }
        /** @brief The site's variable tables bound to their value stores: what
         *  everything - programs, console, telemetry - reads and writes. */
#if BOTANIK_FEATURE_VARS
        const Vars::Bank&                 io() const { return _io; }
        Vars::Bank&                       io()       { return _io; }
        /** @brief Where a site binds a component of its OWN to the parameters
         *  that configure it. Bind in on_setup(): apply_all() runs after it. */
        Vars::ConfigBinder&               config_binder() { return _binder; }
#endif
#if BOTANIK_FEATURE_NETWORK
        Network::NetworkHandler&        network()         { return _net.network(); }
        Network::NameResolver&          resolver()        { return _net.resolver(); }
#endif
#if BOTANIK_FEATURE_NTP
        Network::NtpClient&             ntp()             { return _net.ntp(); }
#endif
#if BOTANIK_FEATURE_MQTT
        Network::MqttClient&            mqtt()            { return _net.mqtt(); }
        Network::MqttSender&            telemetry()       { return _net.telemetry(); }
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
        Modbus::RtuClient&              modbus_rtu()      { return _modbus_rtu; }
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
        Modbus::TcpSlave&               modbus_tcp()      { return _modbus_tcp; }
#endif
        Health::HealthMonitor&          health()          { return _health; }
#if BOTANIK_FEATURE_CONSOLE
        Terminal::Console&              terminal()        { return _console.terminal(); }
#endif

        /** @} */

    protected:
        /** @name Site customization hooks (Template Method)
         *  Overridden by a site instead of setup()/loop(), so the fixed
         *  bring-up and cycle order can never be skipped or misordered.
         *  @{ */

#if BOTANIK_FEATURE_VARS
        /** @brief Hand the bank the site's variable rows, one call per segment,
         *  at the top of setup(). A site splits its table however it likes, with
         *  ONE rule: the PARAMETERS come first, the library's being already at
         *  the front of a run that has to stay contiguous. Bank::freeze() checks
         *  it and names the row that broke it. */
        virtual void register_vars() {}
#endif

#if BOTANIK_FEATURE_CONSOLE
        /** @brief Register the pages in tab order (page 0 = boot page); the
         *  add_page() call order IS the tab order. Default: the full set -
         *  home / shell / logs / alarms / config / monitor / tasks, and notes
         *  last when BOTANIK_FEATURE_NOTES_PAGE is on. A site override can
         *  reorder them or splice in its own (e.g. re-add a STATUS page by
         *  subclassing Pages::StatusPage and add_page()-ing it here). */
        virtual void register_pages();

#if BOTANIK_FEATURE_SHELL_PAGE
        /** @brief Register the site's shell command table, tried BEFORE the
         *  built-ins, so it can shadow one - avoid reusing exit/reset.
         *  @note Gated on SHELL_PAGE, not CONSOLE: ShellPage owns the
         *  CommandShell, so without it there is nothing to register on. */
        virtual void register_commands();
#endif
#endif

#if BOTANIK_FEATURE_MQTT
        /** @brief Register the site's MQTT telemetry, published after the library
         *  base set each cycle. Default: the descriptor's status-variable
         *  groups, resolved generically against the live variable tables. */
        virtual void register_telemetry();
#endif

        /** @brief Per-beat site work that is neither a Component nor a Program.
         *  Runs after every phase, before the watchdog kick. Default: nothing.
         *  @warning Most sites should never need this. Anything here is OUTSIDE
         *  the task table: no phase, no rate, no cost sample and no TASKS row,
         *  so if it turns out to be slow nothing will say so. */
        virtual void on_loop(uint64_t now_ms) { (void)now_ms; }

        /** @} */

        /** @name Config appliers, as task handlers
         *  @{ */

        /** @brief Site post-wiring: where a site registers its OWN components and
         *  control programs. Run near the end of setup(), after every library
         *  component has registered and before the setup pass, so anything
         *  registered here is a task-table entry like any other.
         *  @note The one extension point for work in the beat: a Component or
         *  a Program registered here gets a name, a rate and a TASKS row.
         *  Default: nothing. */
        virtual void on_setup() {}


        /** @} */

    private:
#if BOTANIK_FEATURE_MONITOR_PAGE
#if BOTANIK_FEATURE_PLC
        /** @brief A writer index back to the program's name, for MONITOR's
         *  "computed (prog_a)" column. Named, like every other callback handed
         *  out as a bare function pointer. */
        static const char* _program_name_of(const void* ctx, uint8_t writer);
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
        /** @brief A unit id back to the plant's own tag for it, from the same
         *  device table the field-bus log lines read. */
        static const char* _device_name_of(const void* ctx, uint8_t unit);
#endif
#endif

#if BOTANIK_FEATURE_VARS
        /** @brief Resolve every name the site's tables declare into the ref
         *  arrays they carry alongside. The alarm table binds itself when the
         *  model copies it, so it is not repeated here. */
        void _resolve_site_refs();
#else
        /** @brief Hand every component its struct defaults. Only a build with no
         *  variable table needs this: with one, ConfigBinder does it from the
         *  rows, and a component nobody configures keeps what it was
         *  CONSTRUCTED with. @see vars/bindings.h */
        void _apply_default_config();
#endif

        /** @brief Build the task table: what runs, at which rate, in which phase.
         *  Registration order is the order within a phase, and the phase order is
         *  the data flow (@ref dev_scan), so this reads as the beat does.
         *  @note The RATE of anything here is a tuning knob; the PHASE is not.
         *  Every entry is named - an unnamed row is not a diagnosis. */
        void _register_tasks();





    };

}  // namespace Botanik

#endif // LIBBOTANIK_FIRMWARE_FIRMWARE_H
