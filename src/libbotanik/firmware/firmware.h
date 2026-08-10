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
 * on_setup() / on_step() / on_control() hooks. See docs/integration and CLAUDE.md
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

#if BOTANIK_FEATURE_PLC_SCAN
#include "libbotanik/scan/scan.h"           // Scan::Driver
#endif
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
        /** @brief Control programs, in execution order. The array is const
         *  (flash) and the programs it points at are site-owned statics; each
         *  carries its own scheduling state, so there is no parallel array to
         *  keep the same length as this one. */
        Plc::Program* const*          programs;
        uint8_t                       program_count;
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

        /** @brief HW-watchdog period armed in setup(); the harness overrides it
         *  (the simulator dilates it) via set_hw_watchdog_ms() before
         *  setup(). */
        uint32_t                      _hw_watchdog_ms = Config::HW_WATCHDOG_MS;

#if BOTANIK_FEATURE_CONSOLE
        /** @brief Terminal geometry, fixed at construction (the AnsiTerminal
         *  takes it by value and has no resize). From the config: it is a
         *  property of the installation, not of the target. */
        uint16_t                      _console_cols = Config::CONSOLE_COLS;
        uint16_t                      _console_rows = Config::CONSOLE_ROWS;
        static_assert(Config::CONSOLE_COLS <= Config::TERMINAL_MAX_COLS &&
                      Config::CONSOLE_ROWS <= Config::TERMINAL_MAX_ROWS,
                      "console geometry exceeds the screen back-buffer");
#endif

        /** @} */

        /** @name Generic components (declared in construction order)
         *  The optional subsystem groups are bundled into the feature-gated
         *  stacks (firmware/stacks.h); the firmware owns the shared singletons and
         *  injects them into the stacks by pointer. @{ */

        Logs::Logger                   _logger;
        Scheduler::LoopScheduler       _scheduler;
        Vars::Bank                       _io;          ///< the site's variables
        Vars::KvParamStore             _param_store; ///< one storage key per parameter
        Vars::Params                   _params;      ///< the editable view of the variables
#if BOTANIK_FEATURE_PLC_SCAN
        Scan::Driver                   _scan;        ///< the control programs
#endif
        /** @brief Whether the last beat saw a health fault. Edge tracking only:
         *  the control state itself lives in the Bank (control_enabled()), so
         *  an operator's `control disable` is not something the loop can
         *  silently talk over. */
        bool                           _health_fault = false;
        /** @brief The beat's task table: what runs, how often, and in which
         *  phase. Filled by _register_tasks() during setup(); frozen after. */
        Tasks::Registry            _tasks;
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
         *  work via on_step(), which is registered as the "site" task.
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

            // This order IS the data flow; it must not become configurable.
            _tasks.run_phase(Tasks::PHASE_INPUT,   now_ms);
            _tasks.run_phase(Tasks::PHASE_CONFIG,  now_ms);
            _tasks.run_phase(Tasks::PHASE_SERVICE, now_ms);
            _tasks.run_phase(Tasks::PHASE_EXECUTE, now_ms);
            _tasks.run_phase(Tasks::PHASE_OUTPUT,  now_ms);
            _tasks.run_phase(Tasks::PHASE_PRESENT, now_ms);

            // Kick while healthy (HealthMonitor decides that). Withholding the
            // kick chooses to let the hardware watchdog reset us, so the field
            // drops to its safe state FIRST: the reset gives no such guarantee,
            // and the gap before the reboot is exactly when an actuator must
            // not be left energised.
            if (_health.watchdog_ok()) HAL::Watchdog::kick();
            else                       _io.disable_control(Vars::Bank::INHIBIT_HEALTH);
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

        /** @brief Site post-wiring, run once near the end of setup(). Default:
         *  nothing. */
        virtual void on_setup() {}

        /** @brief Site per-beat work, run each loop() with fresh variable data
         *  (after health, before the logger drains). Default: nothing.
         *
         *  **Runs whatever control authority says**, so it is for bookkeeping,
         *  derived values and telemetry: work that stays correct while the plant
         *  is stopped. Anything that DRIVES the field goes in on_control(), or
         *  it will keep driving through a health fault. */
        virtual void on_step(uint64_t now_ms) { (void)now_ms; }

        /** @brief Site control law, run each beat only while control authority
         *  is held. Default: nothing.
         *
         *  The gated twin of on_step(), and where a site's own driving belongs.
         *  Control authority is enforced per dispatch point rather than per
         *  write - `Vars::Var` is two pointers passed by value and has no route
         *  to the latch - so the gate has to be here, next to the scan driver's.
         *
         *  Carries no scheduling state, so nothing is reset on the way down or
         *  reseeded on the way back up; a site needing those wants a
         *  `Plc::Program`. Runs BEFORE on_step(), so bookkeeping there sees this
         *  beat's decisions. */
        virtual void on_control(uint64_t now_ms) { (void)now_ms; }

        /** @} */

    private:
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

        /** @brief Re-apply either UART rate when its parameter has moved.
         *  Registered as the "serial_cfg" task, so it runs on the CONFIG phase.
         *
         *  Edge-triggered against what is actually running, so the steady case
         *  is two integer compares and no device call. `begin()` on an
         *  already-open port is a reconfigure, not a reopen. Not gated on a
         *  commit serial like the network group: a baud rate is one number,
         *  coherent on its own.
         *
         *  @warning Retuning the console drops the terminal on the other end
         *  mid-sentence: it is at the old rate. Inherent, and the parameter's
         *  own notes say so. The confirming log line lands at the NEW rate, so
         *  it is the first thing read after reconnecting. */
        void _apply_serial_config();

        /** @brief Track the health verdict against control authority. Registered
         *  as the "control_latch" task.
         *
         *  Withdrawing control is a LATCH, not a per-beat sweep: sweeping
         *  outputs after the scan would flicker, since the next beat's programs
         *  re-drive the same demands. A fault takes control away and the outputs
         *  stay safe until health recovers.
         *
         *  Health holds its OWN inhibit, so recovering releases only that one:
         *  an operator's `control disable` stays in force across a fault that
         *  came and went. */
        void _latch_control();
    };

}  // namespace Botanik

#endif // LIBBOTANIK_FIRMWARE_FIRMWARE_H
