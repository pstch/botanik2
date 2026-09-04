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

#include "libbotanik/plc/health.h"
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
#include "libbotanik/io/local_inputs.h"
#include "libbotanik/io/local_outputs.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/logs/topics.h"
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
#include "libbotanik/modbus/rtu/master.h"
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
#include "libbotanik/modbus/tcp/slave.h"
#endif
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/sun/sun_tracker.h"
#include "libbotanik/plc/runtime/runtime.h"
#include "libbotanik/shell/shell.h"
#include "libbotanik/tasks/tasks.h"
#include "libbotanik/utils/assert.h"
#include "libbotanik/utils/serial.h"
#include "libbotanik/vars/access.h"
#include "libbotanik/vars/bindings.h"
#include "libbotanik/vars/model.h"
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
#if BOTANIK_FEATURE_SYSTEM_PAGE
#include "libbotanik/pages/status.h"
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

        /** @brief The SYSTEM band's site half: rows about the UNIT rather than
         *  the plant - its supplies, its rails, its enclosure. Same shape and
         *  the same reasoning as the home table, since no rule tells a
         *  greenhouse reading from a panel one either. Null / 0 for a site
         *  that adds nothing to the library's own rows. */
        const Vars::HomeVar*          system_fields;
        uint8_t                       system_field_count;
        Vars::VarRef*                 system_refs;
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
        /** @brief What each unit on the field bus will answer: its request
         *  ceiling and address alignment. Null plans every unit as
         *  generic_model(). Last, because every site brace-inits this
         *  aggregate and a field added anywhere else moves the rest. */
        const Modbus::DeviceTable*    devices;
#endif
        /** @brief The site's whole variable table, in ONE run - every kind of
         *  row in it, parameters and edge counters included. The base
         *  registers it, so a site declares rows by pointing at them rather
         *  than by overriding a hook.
         *  ONE rule, and Bank::freeze() names the row that breaks it: the
         *  PARAMETERS come first, the library's being already at the front of
         *  a run that has to stay contiguous - which is a property of the
         *  table's ORDER, and the generator emits it. Null on a site that
         *  registers its own in register_vars(). LAST, like the rest. */
        const Vars::Descriptor*       rows;
        uint16_t                      row_count;
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
        Vars::ConfigBinder             _binder;
#endif
#if BOTANIK_FEATURE_SUN_TRACKER
        /** @brief The solar-position instrument. Owned here rather than by a
         *  site: it is library code, one build has at most one, and every
         *  program that wants the sun reads it through SetupContext. */
        Sun::SunPositionTracker        _sun;
#endif
#if BOTANIK_FEATURE_PLC
        /** @brief The site's control programs. ALL of them: they are ST, and
         *  `Plc::Runtime::programs[]` is a link-time symbol the site's
         *  generated `plant.cpp` defines - so the base owns the component and
         *  finds the roster by NAME rather than being handed one.
         *  @note That is also what makes the beat order automatic: this
         *  registers in _register_tasks(), which runs before a site's
         *  on_setup(), so nothing a site adds can get in front of the laws. */
        Plc::Runtime::Plant            _st;
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
        Io::LocalOutputs               _local_out;
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
        /** @brief The field-bus master. Stepped from two phases like the slave
         *  below, and for the mirror-image reason: it takes the field in before
         *  the programs run, and hands their demands out after. */
        Modbus::RtuMaster              _modbus_rtu;
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
#if BOTANIK_FEATURE_SYSTEM_PAGE
        Pages::StatusPage              _system_page;  ///< see system_page()
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
         *  @note Non-virtual, and inline: this is the whole of the main loop's
         *  body, and tick() folds into it.
         *  @return whether a beat actually ran. A harness interleaving its own
         *  work must key off this: tick() returns false for every pass that is
         *  waiting out the period. */
        bool loop() {
            if (!_scheduler.tick()) return false;   // driver: gates the beat, produces the clock

            // One 64-bit "now" for every Component stepped this beat - the
            // scheduler's, not a fresh reading, which would drift off the grid.
            const uint64_t now_ms = _scheduler.context()->timestamp_ms;

            _advance_run_state(now_ms);

            // HALTED: the operator asked for the cycle to stop rather than for
            // a reset, and the laws have already stopped. Nothing runs from
            // here - not the console either - and the kick below still goes
            // out, because the loop IS alive. Recovery is `reset`.
            if (_health.halted() &&
                _scheduler.context()->run_state == Scheduler::RunState::HALT) {
                (void)_scheduler.kick(true);
                return true;
            }

            _scheduler.run_beat(now_ms);

            // The escape hatch, after the whole beat and before the kick that
            // vouches for it. Empty by default.
            on_loop(now_ms);

            // The verdict is HealthMonitor's, the watchdog is the scheduler's,
            // and the LIFECYCLE says when the verdict may be acted on: a
            // withheld kick lets the hardware reset us, and that must not
            // happen until the laws have stopped and sys_shutdown_sec has run
            // out: withholding it the moment `reboot` is typed would race the
            // retained rows on their way to flash.
            const bool ending =
                _scheduler.context()->run_state == Scheduler::RunState::HALT;
            (void)_scheduler.kick(ending ? _health.watchdog_ok() : true);
            return true;
        }

        /** @brief Advance the lifecycle. The one place the two holds are
         *  counted, so the ends of the run are the same mechanism.
         *
         *  START is TIMED; RUN ends the moment a stop is asked for, because
         *  there is nothing to wait for on that edge. STOP is timed AND waits
         *  for anything still PARKING - a roof that has been told to shut is
         *  worth a few seconds of the way out - which Plant caps per program at
         *  plc_parkTimeout_sec, so a seized ram cannot hold the unit open. There
         *  is no way back: a unit on its way out does not return to service. */
        void _advance_run_state(uint64_t now_ms) {
            Scheduler::SchedulerContext* sc = _scheduler.context();
            switch (sc->run_state) {
            case Scheduler::RunState::START:
                if (now_ms >= _startup_hold_ms()) {
                    sc->run_state = Scheduler::RunState::RUN;
                    _logger.notice(Logs::BOOT,
                                   "control tasks enabled, system operational");
                }
                break;
            case Scheduler::RunState::RUN:
                if (_health.stop_requested() != Health::HealthMonitor::Stop::NONE) {
                    sc->run_state = Scheduler::RunState::STOP;
                    _stop_at_ms   = now_ms;
                    _logger.warning(Logs::HEALTH, "entered safe mode, stopping...");
                }
                break;
            case Scheduler::RunState::STOP:
                if (now_ms - _stop_at_ms >= _shutdown_hold_ms() && !_parking()) {
                    sc->run_state = Scheduler::RunState::HALT;
                    _logger.warning(Logs::HEALTH,
                                    "control tasks disabled, system will halt soon");
                }
                break;
            default: break;             // SETUP is left by setup(); HALT is the end
            }
        }

        /** @brief The two holds, from their rows where there is a table and
         *  from the build's own floor where there is not. */
        uint32_t _startup_hold_ms() const {
#if BOTANIK_FEATURE_VARS
            return _io.startup_hold_ms();
#else
            return Config::STARTUP_HOLD_MS;
#endif
        }
        uint32_t _shutdown_hold_ms() const {
#if BOTANIK_FEATURE_VARS
            return _io.shutdown_hold_ms();
#else
            return Config::SHUTDOWN_HOLD_MS;
#endif
        }

        /** @brief Is a control program still travelling to its parked state?
         *  False where there is no plant to ask. */
        bool _parking() const {
#if BOTANIK_FEATURE_PLC
            return _st.parking();
#else
            return false;
#endif
        }

        /** @brief Uptime the laws stopped at; the STOP hold runs from here. */
        uint64_t _stop_at_ms = 0;

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
        Modbus::RtuMaster&              modbus_rtu()      { return _modbus_rtu; }
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
        Modbus::TcpSlave&               modbus_tcp()      { return _modbus_tcp; }
#endif
        Health::HealthMonitor&          health()          { return _health; }
#if BOTANIK_FEATURE_CONSOLE
        Terminal::Console&              terminal()        { return _console.terminal(); }
#endif
#if BOTANIK_FEATURE_SHELL_PAGE
        /** @brief The SHELL page, which owns the command interpreter. What a
         *  site's register_commands() override calls set_commands() on. */
        Pages::ShellPage&               shell()           { return _console.shell(); }
#endif

        /** @} */

    protected:
        /** @name Site customization hooks (Template Method)
         *  Overridden by a site instead of setup()/loop(), so the fixed
         *  bring-up and cycle order can never be skipped or misordered.
         *  @{ */

#if BOTANIK_FEATURE_VARS
        /** @brief Hand the bank the site's variable rows, one call per segment,
         *  at the top of setup(), AFTER the descriptor's own two runs. Most
         *  sites need nothing here: `SiteDescriptor::params` / `rows` is the
         *  ordinary way to declare a table. This is for one built some other
         *  way - and it inherits the same rule, that PARAMETERS come first,
         *  which by then means "there are none left to add". */
        virtual void register_vars() {}
#endif

#if BOTANIK_FEATURE_SYSTEM_PAGE
        /** @brief The page register_pages() puts in the SYSTEM slot. Override
         *  to return a `Pages::StatusPage` subclass of your own, constructed
         *  with system_context(); the base's own is then never registered, so
         *  the site gets the slot rather than a second copy of the page.
         *  Called during setup(), long after every member exists. */
        virtual Pages::StatusPage& system_page() { return _system_page; }
#endif

        /** @brief The component aggregate, for a site page that reads it.
         *  `Pages::StatusPage` takes one and nothing else hands it out.
         *  Valid from the base constructor's body onward, which runs before
         *  any derived member is constructed. */
        const SystemContext& system_context() const { return _sys_ctx; }

#if BOTANIK_FEATURE_CONSOLE
        /** @brief Register the pages in tab order (page 0 = boot page); the
         *  add_page() call order IS the tab order. Default: the full set -
         *  home / shell / logs / alarms / config / monitor / tasks / system,
         *  and notes last when BOTANIK_FEATURE_NOTES_PAGE is on. A site
         *  override can reorder them or splice in its own - a SYSTEM page with
         *  site rows on it is a Pages::StatusPage subclass, constructed with
         *  system_context() and add_page()-ed here in place of the base one. */
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

#if BOTANIK_FEATURE_NETWORK
    /** @brief The TCP socket budget against what this build actually asks of
     *  it. HERE because the composition root is the one place that knows which
     *  optional subsystems exist; config/net.h can only state the ceiling.
     *
     *  Running out is silent by nature - HAL::Network answers a slotless accept
     *  exactly as it answers "nothing pending" - so the count is checked where
     *  it can still fail a build. */
    namespace detail {
        constexpr int TCP_SOCKETS_WANTED =
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
            1 + (int)Config::MODBUS_TCP_MAX_CONN +   // listener + its clients
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_CLIENT
            1 +                                      // the TCP master
#endif
#if BOTANIK_FEATURE_MQTT
            1 +                                      // the broker connection
#endif
            0;
        static_assert(Config::NET_MAX_TCP >= TCP_SOCKETS_WANTED,
                      "NET_MAX_TCP is below what this build's TCP users need: "
                      "the Modbus slave's listener and its connections, the TCP "
                      "master, and MQTT. Raise BOTANIK_NET_MAX_TCP.");
    }
#endif

}  // namespace Botanik

#endif // LIBBOTANIK_FIRMWARE_FIRMWARE_H
