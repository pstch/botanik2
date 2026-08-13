// REVIEW_35: TODO
#ifndef LIBBOTANIK_FIRMWARE_FIRMWARE_H
#define LIBBOTANIK_FIRMWARE_FIRMWARE_H

/**
 * @file firmware.h
 * @brief BotanikFirmware - the generic firmware base.
 *
 * Owns every long-lived generic component (serials, logger, scheduler, variable
 * bank, scan driver, health/alarm model, terminal, core pages) and the per-tick
 * loop() order, and exposes a reference accessor for each. A site is DATA: it
 * fills a SiteDescriptor and derives a thin `GreenhouseXXX : BotanikFirmware`
 * that passes it to the base ctor, customizing only through the register_*() /
 * on_setup() plus one hook per phase (on_input ... on_present). See
 * docs/integration and CLAUDE.md
 * ("Site-specific code").
 *
 * Target-independent: the site's tables are borrowed site statics. Tunable
 * scalars default from Config; the harness overrides them through a set_*()
 * modifier before setup() (set_hw_watchdog_ms()).
 *
 * Both UART rates are PARAMETERS, not compile-time constants:
 * `serial_console_baud` and `serial_monitor_baud` (library_vars.h), defaulting
 * from Config::CONSOLE_BAUD / MONITOR_BAUD. _apply_serial_config() re-applies
 * either on change from the CONFIG phase, retuning the UART and - for the
 * console - AnsiTerminal's per-render TX budget in the same breath: a renderer
 * pacing for a rate the wire does not run at is worse than not retuning at all.
 *
 * BOTANIK_FEATURE_CONSOLE (default on) gates the interactive ANSI UI
 * (AnsiTerminal, the built-in pages, the console UART, the register_*() hooks);
 * BOTANIK_FEATURE_LOGS_MONITOR (default on) the secondary MONITOR UART and its
 * SerialLogWriter. Independent: a headless build turns both off, and logs still
 * reach any site-registered sink.
 */

#include <cstdint>                          // uint8_t/uint32_t/uint64_t

#include "libbotanik/config/identity.h"   // Config::SITE_NAME / FIRMWARE_VERSION / MQTT_TOPIC_PREFIX
#include "libbotanik/config/scheduler.h"  // Config::LOOP_* / TASK_* / GROUP_*
#include "libbotanik/config/serial.h"     // Config::SERIAL_* / *_BAUD
#include "libbotanik/config/terminal.h"   // Config::SCREEN_* / EDITOR_* / SHELL_* / PAGE_*

#include "libbotanik/alarms/alarms.h"       // AlarmCondition
#include "libbotanik/alarms/publisher.h"    // Alarms::Publisher
#include "libbotanik/core/context.h"        // SystemContext
#include "libbotanik/firmware/stacks.h"     // Monitor/Console/NetworkStack
#include "libbotanik/health/health.h"       // Health::HealthMonitor
#include "libbotanik/logs/logger.h"         // Logs::Logger
#include "libbotanik/logs/topics.h"         // Logs::BOOT / HEALTH
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
#include "libbotanik/modbus/rtu/client.h"   // Modbus::RtuClient - the field-bus master
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
#include "libbotanik/modbus/tcp/slave.h"    // Modbus::TcpSlave - the SCADA-facing server
#endif
#include "libbotanik/plc/program.h"         // Plc::Program
#include "libbotanik/scheduler/scheduler.h" // Scheduler::LoopScheduler
#include "libbotanik/shell/shell.h"         // Shell::ShellCommand
#include "libbotanik/tasks/tasks.h"         // Tasks::Registry / GroupId / Phase
#include "libbotanik/utils/assert.h"        // set_assert_logger
#include "libbotanik/utils/serial.h"        // Utils::BufferedSerial
#include "libbotanik/vars/access.h"         // Vars::HomeVar
#include "libbotanik/vars/model.h"          // Vars::Bank / VarTables
#include "libbotanik/vars/params.h"         // Vars::Params / KvParamStore

#if BOTANIK_FEATURE_CONSOLE
#include "libbotanik/console/console.h"     // Terminal::Console
#endif
#if BOTANIK_FEATURE_HOME_PAGE
#include "libbotanik/pages/home.h"          // Pages::HomePage
#endif
#if BOTANIK_FEATURE_PARAMS_PAGE
#include "libbotanik/pages/params_page.h"   // Pages::ParamsPage
#endif
#if BOTANIK_FEATURE_MONITOR_PAGE
#include "libbotanik/pages/monitor.h"       // Pages::MonitorPage
#endif
#if BOTANIK_FEATURE_TASKS_PAGE
#include "libbotanik/pages/tasks_page.h"    // Pages::TasksPage
#endif

namespace Botanik {

    /**
     * @brief The declarative description of a site: its name plus its tables.
     * The firmware base owns every page and wires each table generically, so a
     * site is data, not code.
     *
     * Alarms are NOT a table here: each is a SCOPE_A row in @c vars carrying its
     * code and dwell policy, and the AlarmController scans for them. Parameters
     * are likewise the ROLE_PARAM rows within @c vars.
     *
     * Any table may be null / count 0. Every pointer is borrowed and must outlive
     * the firmware (site statics).
     */
    struct SiteDescriptor {
        const char*                   name;                ///< == Config::SITE_NAME
        const Shell::ShellCommand*    commands;            ///< site shell commands
        uint8_t                       command_count;
        const Vars::HomeVar* home_fields;         ///< home-screen readings
        uint8_t                       home_field_count;
        /** @brief Where the home fields' names resolve to, filled at bring-up.
         *  Site-owned and must hold @c home_field_count entries. Parallel to
         *  the table above so the labels stay in flash. */
        Vars::VarRef*                 home_refs;
        /** @brief Field IO: the variable tables bound to their value stores.
         *  A site with no variables passes a default-constructed VarTables.
         *  (No default member initialisers here: in C++11 an NSDMI would stop
         *  SiteDescriptor being an aggregate, and every site brace-inits it.) */
        Vars::VarTables               vars;
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
        /** @brief What each unit on the field bus will answer: its request
         *  ceiling and address alignment. Null plans every unit as
         *  generic_model(). Last, because every site brace-inits this
         *  aggregate and a field added anywhere else moves the rest. */
        const Modbus::DeviceTable*    devices;
#endif
    };

    /** @brief Generic firmware base: owns the generic components + the loop
     *  order, and exposes each for a site subclass to build on. */
    class BotanikFirmware {

        /** @name Configuration
         *  @{ */

        /** @name Applied UART rates
         *
         *  What the two ports are actually running at, so
         *  _apply_serial_config() reconfigures only on the edge. Seeded from the
         *  same Config defaults the parameters carry.
         *  @{ */
        uint32_t                      _console_baud  = Config::CONSOLE_BAUD;
        uint32_t                      _monitor_baud  = Config::MONITOR_BAUD;
        /** @} */

        /** @brief The site's declarative tables + name (borrowed; site statics
         *  outlive the firmware). Drives page/command/telemetry/config wiring. */
        SiteDescriptor                _site;

        /** @brief HW-watchdog period armed in setup(); a harness overrides it
         *  via set_hw_watchdog_ms() before setup(). */
        uint32_t                      _hw_watchdog_ms = Config::HW_WATCHDOG_MS;

#if BOTANIK_FEATURE_CONSOLE
#endif

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
        Vars::Bank                       _io;          ///< the site's variables
        Vars::KvParamStore             _param_store; ///< one storage key per parameter
        Vars::Params                   _params;      ///< the editable view of the variables
        Health::HealthMonitor          _health;
        SystemContext                  _sys_ctx;     ///< Filled in the ctor body
#if BOTANIK_FEATURE_LOGS_MONITOR
        MonitorStack                   _monitor;     ///< MONITOR UART + log sink
#endif
#if BOTANIK_FEATURE_NETWORK
        NetworkStack                   _net;         ///< resolver/network/ntp/mqtt
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

        /** @brief Bring-up in a fixed, non-overridable order: begin the UARTs,
         *  register the core sinks, register the pages (register_pages()),
         *  select the terminal type, show the banner, run every registered
         *  task's one-time init (Registry::setup(), which takes POST and the
         *  parameter load), arm the watchdog. None of it runs on a beat: by the
         *  first tick every component is up. A site customizes via the hooks,
         *  never setup(). */
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

        /** @brief One main-loop iteration. tick() gates a beat and republishes
         *  the display "now" (`SchedulerContext::timestamp_ms`) the render path
         *  shares.
         *
         *  The beat is phase-major: each phase runs the tasks registered to it
         *  whose group has come due (_register_tasks()). The PHASE order below
         *  is load-bearing and stays written out as code; a task's RATE is a
         *  property of the task, so retuning one can never reorder the cycle.
         *
         *  Non-virtual, so the order can't be overridden; a site adds per-beat
         *  work through the per-phase hooks, each its own "site_*" task.
         *
         *  Inline, unlike the rest of the bring-up: it is the hot path, and the
         *  scheduler's tick() paints the stack below its caller's frame, so this
         *  is the frame the high-water measurement is taken against.
         *
         *  @return whether a beat actually ran. A harness interleaving work of
         *  its own must key it off this: tick() busy-waits the tail of every
         *  period, so per-ITERATION work runs tens of times a beat. */
        bool loop() {
            if (!_scheduler.tick()) return false;   // driver: gates the beat, produces the clock

            // One 64-bit "now" for every Component stepped this beat.
            uint64_t now_ms = HAL::Clock::uptime_ms();

            _scheduler.run_beat(now_ms);

            // The escape hatch, after the whole beat and before the kick that
            // vouches for it. Empty by default.
            on_loop(now_ms);

            // The verdict is HealthMonitor's, the watchdog is the scheduler's.
            // A withheld kick chooses to let the hardware reset us, so the field
            // drops to its safe state FIRST: the reset gives no such guarantee,
            // and the gap before the reboot is exactly when an actuator must
            // not be left energised.
            if (!_scheduler.kick(_health.watchdog_ok())) _io.set_health_hold(true);
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
        const Vars::Bank&                 io() const { return _io; }
        Vars::Bank&                       io()       { return _io; }
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
        Vars::Params&                     params()          { return _params; }
#if BOTANIK_FEATURE_CONSOLE
        Terminal::Console&              terminal()        { return _console.terminal(); }
#endif

        /** @} */

    protected:
        /** @name Site customization hooks (Template Method)
         *  Overridden by a site instead of setup()/loop(), so the fixed
         *  bring-up and cycle order can never be skipped or misordered.
         *  @{ */

#if BOTANIK_FEATURE_CONSOLE
        /** @brief Register the pages in tab order (page 0 = boot page); the
         *  add_page() call order IS the tab order. Default: the full set -
         *  home / shell / logs / alarms / config / monitor / tasks, and notes
         *  last when BOTANIK_FEATURE_NOTES_PAGE is on. A site override can
         *  reorder them or splice in its own (e.g. re-add a STATUS page by
         *  subclassing Pages::StatusPage and add_page()-ing it here). */
        virtual void register_pages();

#if BOTANIK_FEATURE_SHELL_PAGE
        /** @brief Register the site's shell command table (tried BEFORE the
         *  built-ins, so the table can shadow one - avoid reusing exit/reset).
         *  Default: the descriptor's commands, reading the live variable bank
         *  as their ctx.
         *
         *  Gated on SHELL_PAGE, not CONSOLE: ShellPage owns the CommandShell, so
         *  without the page there is nothing to register commands on. */
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
         *
         *  Runs once per BEAT (inside the tick gate, so never per loop()
         *  iteration), after every phase and before the watchdog kick that
         *  vouches for the beat. Default: nothing.
         *
         *  **Most sites should never need this.** Work in the beat belongs in
         *  the table: as a Component or a Program registered in on_setup(), or
         *  as one of the six per-phase hooks. Anything here is outside the
         *  table, so it has no phase, no rate, no cost sample and no row on the
         *  TASKS page - if it turns out to be slow, nothing will say so. It is
         *  the hatch for library-level code that genuinely fits none of those. */
        virtual void on_loop(uint64_t now_ms) { (void)now_ms; }

        /** @name Config appliers, as task handlers
         *  @{ */

        /** @brief Site post-wiring: where a site registers its OWN components
         *  and control programs, and wires up anything else it needs.
         *
         *  Run once near the end of setup(), after every library component has
         *  registered and every log sink exists, and still **before the setup
         *  pass** - so a component registered here is a task-table entry like
         *  any other: set up in the same sweep, dispatched by the same loop, and
         *  named on the TASKS page. Register through `tasks()`:
         *
         *  @code
         *  void on_setup() override {
         *      _my_pump.register_tasks();   // a Component
         *      _my_law.register_tasks();    // a Plc::Program
         *  }
         *  @endcode
         *
         *  This is the extension point for work the six per-phase hooks cannot
         *  express: a subsystem with state of its own, several rates, or more
         *  than one point in the beat.
         *
         *  Default: nothing. */
        virtual void on_setup() {}


        /** @} */

    private:
#if BOTANIK_FEATURE_MONITOR_PAGE
#if BOTANIK_FEATURE_PLC_SCAN
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

        /** @brief Resolve every name the site's tables declare into the ref
         *  arrays they carry alongside. The alarm table binds itself when the
         *  model copies it, so it is not repeated here. */
        void _resolve_site_refs();

        /** @brief Build the task table: what runs, at which rate, in which phase.
         *
         *  Registration order is the order within a phase, and the phase order
         *  is the data flow (@ref dev_scan), so this reads top to bottom as the
         *  beat does. The RATE of anything here is a tuning knob (its group);
         *  the PHASE is not, because it encodes what must happen before what.
         *
         *  Every entry is named: the TASKS page and `stats` report per-task
         *  cost, and an unnamed row is not a diagnosis. */
        void _register_tasks();





    };

}  // namespace Botanik

#endif // LIBBOTANIK_FIRMWARE_FIRMWARE_H
