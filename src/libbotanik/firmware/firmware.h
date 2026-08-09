// REVIEW_35: TODO
#ifndef LIBBOTANIK_FIRMWARE_FIRMWARE_H
#define LIBBOTANIK_FIRMWARE_FIRMWARE_H

/**
 * @file firmware.h
 * @brief BotanikFirmware - the generic firmware base.
 *
 * Owns every long-lived generic component (serials, logger, scheduler, variable
 * bank, scan driver, health/alarm model, terminal, core pages) and the per-tick
 * loop() order, and exposes a reference accessor for each so a site subclass can
 * build its own components on top. A site derives
 * `GreenhouseXXX : BotanikFirmware`, adds its pages as members, and
 * customizes through the register_*() hooks; see docs and CLAUDE.md
 * ("Site-specific code").
 *
 * Target-independent: the site's tables are injected by the caller (site
 * statics). Tunable scalars default
 * from Config and the harness overrides them through a set_*() modifier before
 * setup() (set_hw_watchdog_ms()).
 *
 * Both UART rates are PARAMETERS, not compile-time constants: `serial_console_baud`
 * and `serial_monitor_baud` (declared by the library - library_vars.h), defaulting
 * from Config::CONSOLE_BAUD / MONITOR_BAUD. _apply_serial_config() runs on the
 * CONFIG phase and re-applies either on change, retuning the UART and - for the
 * console - AnsiTerminal's per-render TX budget in the same breath, since a
 * renderer pacing for a rate the wire no longer runs at is the failure that kept
 * this compile-time before.
 *
 *
 * Console-optional (BOTANIK_FEATURE_CONSOLE, default on): the interactive ANSI
 * UI (AnsiTerminal, the built-in pages, the console UART, the register_*()
 * hooks). Off -> headless. Monitor-optional (BOTANIK_FEATURE_LOGS_MONITOR, default
 * on): the secondary MONITOR UART and its SerialLogWriter sink. The two are
 * independent; a fully headless build turns both off (logs still reach any
 * site-registered sink). The site extension below assumes the console is
 * present.
 *
 * @section firmware_extend Extending the base for a site
 *
 * A site is data, not code: it fills a SiteDescriptor (name + the config-param,
 * shell-command, status-variable, and alarm tables) and hands it to the base.
 * The base owns every page (home/shell/logs/alarms/config/monitor) and wires
 * each table generically, so a site subclass is a thin binding of the payload
 * type + the descriptor. Extension surface:
 *
 *  - **Descriptor tables** (SiteDescriptor) - the alarm conditions (each naming
 *    the boolean variable it reads; the AlarmController splices in the generic
 *    platform-health alarms from alarms/platform.h), shell commands,
 *    status-variable groups, and the variable tables. Pure data; no hook.
 *  - **Pages** - the default register_pages() registers the full set. A site only
 *    overrides it to reorder or splice in an extra page (e.g. re-adding a STATUS
 *    page by subclassing Pages::StatusPage). Page 0 is the boot page.
 *  - **Post-wiring / per-beat** - on_setup() runs once near the end of setup();
 *    on_step(now_ms) runs each beat with fresh variable data.
 *
 * Worked example (the common case: no custom code, just the descriptor):
 * @code
 * // --- site-static tables (single source of truth for this greenhouse) ---
 * const Vars::HomeVar          g_home[]   = { ... };  // home-screen readings
 * const Shell::ShellCommand    g_cmds[]   = { ... };  // site shell commands
 * Plc::Program* const          g_programs[] = { ... };  // control programs
 * const SiteDescriptor g_site = {
 *     Config::SITE_NAME,
 *     g_cmds,   1,
 *     g_home,   13,
 *     SITE_VAR_TABLES,                     // descriptors + values + string pool
 *     g_programs, 2,
 * };
 *
 * class GreenhouseXXX final : public BotanikFirmware {
 * public:
 *     GreenhouseXXX() : BotanikFirmware(g_site) {}
 * };
 * @endcode
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
     * @brief The declarative description of a site: its name plus its generated
     * tables. The firmware base owns every page and wires each table
     * generically, so a site is data (these tables), not code.
     *
     * The tables are meant to be generated:
     *   - @c commands      - site shell commands (Shell::ShellCommand).
     *   - @c home_fields   - home-screen readings (Vars::HomeVar).
     *   - alarms are NOT a table here: each is a SCOPE_A row in @c vars,
     *     carrying its code and dwell policy, and the AlarmController scans for them.
     *   - @c vars          - the variable tables themselves (Vars::VarTables);
     *                        parameters are the ROLE_PARAM rows within them.
     *   - @c programs      - the control programs (Plc::Program), which carry
     *                        their own scheduling state.
     *
     * Any table may be null / count 0. Every pointer is borrowed and must outlive
     * the firmware (site statics). See firmware/firmware.h "Extending the base".
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
         *  _apply_serial_config() can tell a changed parameter from a steady
         *  one and reconfigure only on the edge. Seeded from the same Config
         *  defaults the parameters carry, so the first pass is a no-op unless
         *  the store held something else.
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
         *  takes it by value and has no resize). A property of the
         *  installation, so it comes from the config rather than the target. */
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
        /** @brief Whether the last beat saw a health fault. Edge tracking only
         *  - the control state itself lives in the Bank (control_enabled()), so
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
        // Site-independent pages the base owns (declared after _console so
        // terminal() is live): the landing page, the editable CONFIG table over
        // the config backend, and the read-only MONITOR table over the site's
        // status-variable groups.
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
         *  baud rates are parameters - see the file header.
         *
         *  The initialiser list follows declaration order exactly (-Wreorder):
         *  _io must be alive before _params and _scan take its address. */
        explicit BotanikFirmware(const SiteDescriptor& site)
            : _site(site),
              _logger(),
              _scheduler(Config::TASK_PERIOD_MS, &_logger),
              _io(site.vars),
              // Params come from local storage.
              _param_store(),
              _params(&_io, &_param_store, &_logger),
#if BOTANIK_FEATURE_PLC_SCAN
              _scan(site.programs, site.program_count, &_io, &_logger),
#endif
              // The alarm model scans the variable table for SCOPE_A rows;
              // platform-health alarms are appended and driven locally.
              _health(&_logger, _scheduler.context(), &_io)
              , _sys_ctx()   // fields wired in the body, once addresses are known
#if BOTANIK_FEATURE_LOGS_MONITOR
              , _monitor(&_logger)
#endif
#if BOTANIK_FEATURE_NETWORK
              // Network reads its config from the variable bank by name;
              // telemetry publishes the live _sys_ctx (read later).
              , _net(&_io, &_logger, &_sys_ctx)
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
              , _modbus_rtu(&_io, &_logger, site.devices)
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
              // The alarm model is the ack block's target; it is HealthMonitor's,
              // and this member is declared after it so the reference is live.
              , _modbus_tcp(&_io, &_logger, &_health.alarm_model())
#endif
#if BOTANIK_FEATURE_CONSOLE
              // _sys_ctx is declared above, so it exists (empty) by now; the
              // console borrows it by pointer and reads it later, once the body
              // below has filled in the subsystem addresses.
              , _console(_console_cols, _console_rows,
                         &_scheduler.context()->timestamp_ms, _console_baud,
                         &_health.alarm_model(), &_logger,
                         _scheduler.context(), &_sys_ctx)
              // Base-owned pages, built from the descriptor + the live payload.
#if BOTANIK_FEATURE_HOME_PAGE
              , _home_page(_console.terminal(), _site.home_fields,
                           _site.home_field_count, &_io, _site.home_refs)
#endif
#if BOTANIK_FEATURE_PARAMS_PAGE
              , _params_page(_console.terminal(), &_params)
#endif
#if BOTANIK_FEATURE_MONITOR_PAGE
              , _monitor_page(_console.terminal(), &_io)
#endif
#if BOTANIK_FEATURE_TASKS_PAGE
              , _tasks_page(_console.terminal(), _tasks)
#endif
#endif
        {
            // Wire the status aggregate now every subsystem address is fixed,
            // then hand a copy to the terminal (it holds SystemContext by value).
            _sys_ctx.scheduler = _scheduler.context();
            _sys_ctx.tasks     = &_tasks;
            _sys_ctx.vars      = &_io;
#if BOTANIK_FEATURE_NETWORK
            _sys_ctx.network = &_net.network();
#endif
#if BOTANIK_FEATURE_NTP
            _sys_ctx.ntp = &_net.ntp();
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
            _sys_ctx.modbus_tcp = &_modbus_tcp;
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
            _sys_ctx.modbus_rtu = &_modbus_rtu;
#endif
#if BOTANIK_FEATURE_MQTT
            _sys_ctx.mqtt = &_net.mqtt();
#endif
        }


        virtual ~BotanikFirmware() {}

        /** @brief Bring-up in a fixed, non-overridable order: begin the UARTs,
         *  register the core sinks, register the pages (register_pages()),
         *  select the terminal type, show the banner, run every registered task's
         *  one-time init (Registry::setup(), which is where POST and the
         *  parameter load happen), arm the watchdog. Nothing here runs on a beat:
         *  by the time loop() first ticks, every component is already up. A site
         *  customizes via the hooks, never setup(). */
        void setup() {
            // route BOTANIK_ASSERT failures to the log
            set_assert_logger(&_logger);
            // Register the site's config parameters (appended after the library
            // built-ins) so the PARAMS page and the `set` command see them.
            // Seed every variable with its declared default, so a value read
            // before any driver has spoken is the default rather than zero.
            _io.init();
            // Bind the site's declarative tables to the variable table. They
            // name their rows rather than indexing them, so this is where a
            // name becomes an index - once, here, because the lookup is a
            // linear scan and no renderer should pay for it per frame. A name
            // with no row lands on the sink and is counted (and logged) by the
            // bank, which is what the site test asserts is zero.
            _resolve_site_refs();

            // How often each component runs on the beat. The loop below still
            // calls step() on every one every beat - the ORDER there is
            // load-bearing and stays code - but a component whose work resolves
            // something measured in seconds need not pay 100 Hz for it. The scan
            // driver and the logger keep the beat deliberately: the scan's
            // not-due path is two comparisons per task, and the logger drains a
            // queue that evicts under pressure.
            _register_tasks();

#if BOTANIK_FEATURE_CONSOLE
            HAL::Serial::get(HAL::Serial::CONSOLE)->begin(_console_baud);
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR
            HAL::Serial::get(HAL::Serial::MONITOR)->begin(_monitor_baud);
#endif

            // Core sinks: monitor + (with the UI) shell/logs pages, plus a
            // TX-drop watch on each live UART. A site adds its own in
            // on_setup().
#if BOTANIK_FEATURE_LOGS_MONITOR
            _logger.add_writer(&_monitor.writer());
            _logger.watch_tx(&_monitor.serial());
#endif
#if BOTANIK_FEATURE_CONSOLE
#if BOTANIK_FEATURE_SHELL_PAGE
            _logger.add_writer(&_console.shell());
#endif
#if BOTANIK_FEATURE_LOGS_PAGE
            _logger.add_writer(&_console.logs());
#endif
            // The home page sinks the log stream into its bottom log-tail band.
#if BOTANIK_FEATURE_HOME_PAGE
            _logger.add_writer(&_home_page);
#endif
            _logger.watch_tx(&_console.serial());
#endif

#if BOTANIK_FEATURE_CONSOLE
#if BOTANIK_FEATURE_SHELL_PAGE
            // Let the shell's loglevel / alarm-ack commands reach the live
            // logger + model.
            _console.shell().set_diagnostics(&_logger, &_health.alarm_model());
#if BOTANIK_FEATURE_NTP
            // Let the `ntpsync` command force a resync on the live client.
            _console.shell().set_ntp(&_net.ntp());
#endif

            register_commands();   // site commands, tried after the built-ins
#endif
            // tab order (page 0 = boot page); switch_to(0) records it
            register_pages();
            _console.terminal().setup_terminal();
            _console.terminal().switch_to(0);
#endif

            // Details on a second line (the renderer splits on '\n') so it
            // wraps cleanly.
            _logger.info(Logs::BOOT, "Botanik v%s\nRunning on : %s",
                         Config::FIRMWARE_VERSION, HAL::hardware_description());

#if BOTANIK_FEATURE_ALARMS_PUBLISH
            // Route alarm transitions out through the MQTT client (it is an
            // Alarms::Publisher). Until now the notifier was a no-op.
            _health.set_alarm_publisher(&_net.mqtt());
#endif
#if BOTANIK_FEATURE_LOGS_PUBLISH
            // Stream log entries out to the broker (best-effort, own min level).
            _net.mqtt_log().set_publisher(&_net.mqtt());
            _logger.add_writer(&_net.mqtt_log());
#endif
#if BOTANIK_FEATURE_MQTT
            register_telemetry();   // site telemetry fields (base set always on)
#endif

            on_setup();   // site post-wiring (extra sinks / components)

            // Every registered task's one-time init, before the loop exists.
            // Deliberately HERE and not beside _register_tasks(): this is what
            // runs each component's announce() and HealthMonitor's POST, and
            // before the sinks above were registered every one of those lines
            // would have gone nowhere. After on_setup() too, so a site's own
            // sinks catch them.
            //
            // It also takes the parameter load - 423 storage reads, tens of ms
            // of flash - out of the first beat, where it read as a boot overrun
            // against a 10 ms budget.
            _tasks.setup(HAL::Clock::uptime_ms());

            // The parameter load just happened, so this is the first moment the
            // stored UART rates exist. Applied here rather than left to the
            // first beat so the boot lines below go out at the rate the operator
            // actually configured - everything above them was emitted at the
            // compile-time default, which is unavoidable: something has to open
            // the port before the store can be read.
            _apply_serial_config();

            HAL::Watchdog::start(_hw_watchdog_ms);

            // Bring-up is done: the system's hold on control is released, so
            // the field is ours to drive. It starts held (Bank's default), so
            // nothing between power-on and here - a half-wired component, a site
            // hook that faults - can energise an actuator on the way past.
            _io.enable_control(Vars::Bank::INHIBIT_HEALTH);

            // One line per UART so neither overflows 80 cols and each gates with
            // its feature (console/monitor dropped when their UART is absent).
            _logger.debug(Logs::BOOT, "boot complete\nHW watchdog %lu ms",
                          (unsigned long)_hw_watchdog_ms);
#if BOTANIK_FEATURE_CONSOLE
            _logger.debug(Logs::BOOT, "console %lu baud", (unsigned long)_console_baud);
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR
            _logger.debug(Logs::BOOT, "monitor %lu baud", (unsigned long)_monitor_baud);
#endif
        }

        /** @brief Build the task table: what runs, at which rate, in which phase.
         *
         *  Registration order is the order within a phase, and the phase order
         *  is the data flow (@ref dev_scan) - so this reads top to bottom as the
         *  beat does. The RATE of anything here is a tuning knob (its group); the
         *  PHASE is not, because it encodes what has to happen before what.
         *
         *  Every entry is named, because the TASKS page and the `stats` command
         *  report per-task cost and an unnamed row is not a diagnosis. */
        void _register_tasks() {
            using namespace Tasks;

            // --- INPUT: fresh bytes and fresh field, before anything reads ---
#if BOTANIK_FEATURE_CONSOLE
            // BufferedSerial is one Component whose halves belong at opposite
            // ends of the beat - RX here, TX after the render. Stepping the
            // whole pump at both ends would cost an extra read() each beat and
            // split the console's baud-paced TX across the render.
            _tasks.add("console_rx", GROUP_FAST, PHASE_INPUT,
                [](void* s, uint64_t) { static_cast<Utils::BufferedSerial*>(s)->receive(); },
                &_console.serial());
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
            // The field bus commits its readings here, ahead of expire_stale:
            // the sweep that decides a reading has gone stale has to judge the
            // freshest image, not the one from before this beat's answers.
            _tasks.add("modbus_rtu_rx", GROUP_FAST, PHASE_INPUT,
                [](void* s, uint64_t now) {
                    static_cast<Modbus::RtuClient*>(s)->receive_step(now);
                },
                &_modbus_rtu);
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
            // A master's write lands HERE, before the programs run, so a setpoint
            // written this beat is what they run on this beat. add_component, so
            // the map is built and the `mb` group read in Registry::setup()
            // rather than inside the first beat.
            _tasks.add_component("modbus_tcp_rx", GROUP_FAST, PHASE_INPUT, _modbus_tcp);
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR
            _tasks.add("monitor_rx", GROUP_FAST, PHASE_INPUT,
                [](void* s, uint64_t) { static_cast<Utils::BufferedSerial*>(s)->receive(); },
                &_monitor.serial());
#endif
            // A full linear scan of the variable table, resolving timeouts
            // measured in seconds. The field driver's commit will slot in here
            // too, ahead of it.
            _tasks.add("expire_stale", GROUP_BACKGROUND, PHASE_INPUT,
                [](void* s, uint64_t now) { static_cast<Vars::Bank*>(s)->expire_stale(now); },
                &_io);
            // Beside it, and for the same reason: a reading can be fresh and
            // still be nonsense. A dead 4-20 mA loop reads full scale, which no
            // timeout catches - the declared plausibility band does.
            _tasks.add("reject_implausible", GROUP_BACKGROUND, PHASE_INPUT,
                [](void* s, uint64_t) { (void)static_cast<Vars::Bank*>(s)->reject_implausible(); },
                &_io);

            // --- CONFIG: the image every config-driven component reads below ---
            _tasks.add_component("params", GROUP_SLOW, PHASE_CONFIG, _params);
            // ...and the two rates that are config for the firmware ITSELF.
            // After the params component, so an edit committed this beat is
            // applied this beat rather than the next.
            _tasks.add("serial_cfg", GROUP_SLOW, PHASE_CONFIG,
                [](void* s, uint64_t) {
                    static_cast<BotanikFirmware*>(s)->_apply_serial_config();
                }, this);

            // --- SERVICE: supervision and the background state machines ---
            // Health before the logger drains, so an alarm START/END lands this
            // beat, and before EXECUTE, so a fault inhibits this beat's programs.
            _tasks.add_component("health", GROUP_SLOW, PHASE_SERVICE, _health);
            _tasks.add("control_latch", GROUP_FAST, PHASE_SERVICE,
                [](void* s, uint64_t) { static_cast<BotanikFirmware*>(s)->_latch_control(); },
                this);
#if BOTANIK_FEATURE_NETWORK
            _tasks.add_component("net", GROUP_FAST, PHASE_SERVICE, _net);
#endif

            // --- EXECUTE: the control programs ---
            // Unconditional: the driver holds the Bank and inhibits the programs
            // itself while control is withheld, so it still announces at boot
            // (when control is withheld by definition) and a future
            // output-collect phase runs DURING a withdrawal rather than being
            // skipped with it.
#if BOTANIK_FEATURE_PLC_SCAN
            _tasks.add_component("scan", GROUP_FAST, PHASE_EXECUTE, _scan);
#endif
            // The site's own control law, under authority - the same gate the
            // scan driver applies to the programs, for the same reason. Ahead of
            // the ungated hook below, so a site's law runs with the programs and
            // its bookkeeping sees the result.
            //
            // The gate is per DISPATCH POINT rather than per write: Var is two
            // pointers passed by value and stays that way, so it cannot check a
            // latch it has no route to. That makes this list exhaustive by
            // construction - the scan driver and this - and a third place that
            // drives the field has to come and gate itself here.
            _tasks.add("site_ctl", GROUP_FAST, PHASE_EXECUTE,
                [](void* s, uint64_t now) {
                    BotanikFirmware* f = static_cast<BotanikFirmware*>(s);
                    if (f->io().control_enabled()) f->on_control(now);
                },
                this);
            _tasks.add("site", GROUP_FAST, PHASE_EXECUTE,
                [](void* s, uint64_t now) { static_cast<BotanikFirmware*>(s)->on_step(now); },
                this);

            // --- OUTPUT: hand what the programs drove to the field ---
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
            // add_component: on_step() IS the transmit half, and this is also
            // what runs the port bring-up and the bus planning in
            // Registry::setup() rather than inside the first beat.
            _tasks.add_component("modbus_rtu_tx", GROUP_FAST, PHASE_OUTPUT, _modbus_rtu);
#endif

            // --- PRESENT: report what just happened ---
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
            // ...and the answer goes out here, off the image the scan just
            // produced. One beat of latency on a read, against a poll measured
            // in seconds, and no request ever answered from a half-updated image.
            // The flash half of a master's write, paced. Storage is synchronous
            // and one request can cover 61 parameters, so the commits are queued
            // where they are decoded and made durable a couple per beat here.
            _tasks.add("modbus_tcp_wr", GROUP_FAST, PHASE_SERVICE,
                [](void* s, uint64_t now) {
                    static_cast<Modbus::TcpSlave*>(s)->service_persist(now);
                },
                &_modbus_tcp);
            _tasks.add("modbus_tcp_tx", GROUP_FAST, PHASE_PRESENT,
                [](void* s, uint64_t now) {
                    static_cast<Modbus::TcpSlave*>(s)->service_tx(now);
                },
                &_modbus_tcp);
#endif
#if BOTANIK_FEATURE_LOGGING
            _tasks.add("logger", GROUP_FAST, PHASE_PRESENT,
                [](void* s, uint64_t) { static_cast<Logs::Logger*>(s)->refill(); },
                &_logger);
#endif
#if BOTANIK_FEATURE_CONSOLE
            // The Console is the Component, not the stack around it: the stack's
            // serial pump is registered separately, at its own phases.
            _tasks.add_component("console", GROUP_SLOW, PHASE_PRESENT, _console.terminal());
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR
            _tasks.add_component("monitor_log", GROUP_FAST, PHASE_PRESENT, _monitor.writer());
#endif
#if BOTANIK_FEATURE_CONSOLE
            _tasks.add("console_tx", GROUP_FAST, PHASE_PRESENT,
                [](void* s, uint64_t) { static_cast<Utils::BufferedSerial*>(s)->transmit(); },
                &_console.serial());
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR
            _tasks.add("monitor_tx", GROUP_FAST, PHASE_PRESENT,
                [](void* s, uint64_t) { static_cast<Utils::BufferedSerial*>(s)->transmit(); },
                &_monitor.serial());
#endif

            if (_tasks.overflow() != 0)
                _logger.critical(Logs::BOOT, "%u task(s) refused: MAX_TASKS is %u",
                                 (unsigned)_tasks.overflow(), (unsigned)Config::MAX_TASKS);
        }

        /** @brief Re-apply either UART rate when its parameter has moved.
         *
         *  Edge-triggered against what is actually running, so the steady case -
         *  every beat but the one after an edit - is two integer compares and no
         *  device call. `begin()` on an already-open port is a reconfigure, not
         *  a reopen, so there is nothing to tear down.
         *
         *  Deliberately NOT gated on a commit serial the way the network group
         *  is. That exists because a static-IP block is incoherent until every
         *  field is in; a baud rate is one number, coherent on its own, and
         *  making an operator commit it separately would only be a second step
         *  to forget.
         *
         *  @warning Retuning the console drops the terminal on the other end
         *  mid-sentence - it is still at the old rate. That is inherent, and the
         *  parameter's own notes say so; the log line lands at the NEW rate, so
         *  the operator's confirmation is the first thing they read after
         *  reconnecting. */
        void _apply_serial_config() {
#if BOTANIK_FEATURE_CONSOLE
            const uint32_t con = (uint32_t)Vars::get_int(&_io, "serial_console_baud",
                                                         (int32_t)_console_baud);
            if (con != _console_baud && con != 0) {
                _console_baud = con;
                // Both, together: the wire and the budget the renderer paces to.
                // Either alone is the bug this used to be compile-time to avoid.
                HAL::Serial::get(HAL::Serial::CONSOLE)->begin(con);
                _console.terminal().terminal().set_baud(con);
                _logger.notice(Logs::BOOT, "console now %lu baud",
                               (unsigned long)con);
            }
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR
            const uint32_t mon = (uint32_t)Vars::get_int(&_io, "serial_monitor_baud",
                                                         (int32_t)_monitor_baud);
            if (mon != _monitor_baud && mon != 0) {
                _monitor_baud = mon;
                HAL::Serial::get(HAL::Serial::MONITOR)->begin(mon);
                _logger.notice(Logs::BOOT, "monitor now %lu baud",
                               (unsigned long)mon);
            }
#endif
        }

        /** @brief Track the health verdict against control authority.
         *
         *  Withdrawing control is a LATCH, not a per-beat sweep: sweeping outputs
         *  after the scan would flicker, since the next beat's programs re-drive
         *  the same demands before the sweep runs again. So a fault takes control
         *  away and the outputs stay safe until health actually recovers.
         *
         *  Health holds its OWN inhibit, so recovering releases only that one: an
         *  operator who typed `control disable` stays in force across a fault
         *  that came and went, which is the only reading of "I turned control
         *  off" worth anything. */
        void _latch_control() {
            if (!_health.watchdog_ok()) {
                if (!_health_fault) {
                    _health_fault = true;
                    _io.disable_control(Vars::Bank::INHIBIT_HEALTH);
                    _logger.critical(Logs::HEALTH,
                        "control disabled\n"
                        "outputs at their safe state, scan inhibited");
                }
            } else if (_health_fault) {
                _health_fault = false;
                _io.enable_control(Vars::Bank::INHIBIT_HEALTH);
                // The lead is hoisted out of the ternary, so the format string
                // is a literal the printf attribute can still check, and both
                // outcomes grep as "health recovered".
                _logger.warning(Logs::HEALTH, "health recovered\n%s",
                    _io.control_enabled()
                        ? "control enabled, scan resuming"
                        : "control still disabled by the operator");
            }
        }

    public:
        /** @brief The task table, for the TASKS page and the `stats` command. */
        const Tasks::Registry& tasks() const { return _tasks; }

        /** @brief One main-loop iteration. tick() gates a beat and republishes
         *  the display "now" (`SchedulerContext::timestamp_ms`) the render path
         *  shares.
         *
         *  The beat is phase-major: each phase runs the tasks registered to it
         *  whose group has come due (_register_tasks()). The PHASE order below is
         *  load-bearing and stays written out here as code; a task's RATE is a
         *  property of the task. Letting a group imply an order would mean
         *  retuning a rate silently reordered the cycle.
         *
         *  Non-virtual, so the order can't be overridden; a site adds per-beat
         *  work via on_step(), which is registered as the "site" task. */
        /** @return whether a beat actually ran. The scheduler gates it, and a
         *  harness with work of its own to interleave needs to know: doing that
         *  work per ITERATION rather than per beat runs it tens of times a beat,
         *  since tick() busy-waits the tail of every period. */
        bool loop() {
            if (!_scheduler.tick()) return false;   // driver: gates the beat, produces the clock

            // One 64-bit "now" for every Component stepped this beat.
            uint64_t now_ms = HAL::Clock::uptime_ms();

            // Phase-major dispatch: each phase runs the tasks registered to
            // it whose group has come due. The order of these calls IS the data
            // flow, and it is the part that must not become configurable.
            _tasks.run_phase(Tasks::PHASE_INPUT,   now_ms);
            _tasks.run_phase(Tasks::PHASE_CONFIG,  now_ms);
            _tasks.run_phase(Tasks::PHASE_SERVICE, now_ms);
            _tasks.run_phase(Tasks::PHASE_EXECUTE, now_ms);
            _tasks.run_phase(Tasks::PHASE_OUTPUT,  now_ms);
            _tasks.run_phase(Tasks::PHASE_PRESENT, now_ms);

            // Kick while healthy; HealthMonitor is what decides that (see
            // health/health.h). Withholding the kick means we have chosen to let
            // the hardware watchdog reset us, so the field is dropped to its safe
            // state FIRST - the reset itself gives no such guarantee, and the gap
            // between the last kick and the reboot is exactly when an actuator
            // must not be left energised.
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
        virtual void register_pages() {
            // Whichever page registers FIRST is page 0, the boot page. That is
            // positional, not a property of HOME: with HOME_PAGE off the boot
            // page is simply the next one that exists.
#if BOTANIK_FEATURE_HOME_PAGE
            _console.terminal().add_page(&_home_page);
#endif
#if BOTANIK_FEATURE_SHELL_PAGE
            _console.terminal().add_page(&_console.shell());
#endif
#if BOTANIK_FEATURE_LOGS_PAGE
            _console.terminal().add_page(&_console.logs());
#endif
#if BOTANIK_FEATURE_ALARMS_PAGE
            _console.terminal().add_page(&_console.alarms());
#endif
#if BOTANIK_FEATURE_PARAMS_PAGE
            _console.terminal().add_page(&_params_page);
#endif
#if BOTANIK_FEATURE_MONITOR_PAGE
            _console.terminal().add_page(&_monitor_page);
#if BOTANIK_FEATURE_PLC_SCAN
            // Only this class holds both the page and the scan driver, so the
            // join is made here rather than by widening either of them.
            _monitor_page.set_writer_names(
                [](const void* ctx, uint8_t w) -> const char* {
                    const Scan::Driver* d = static_cast<const Scan::Driver*>(ctx);
                    const Plc::Program* p = d->program(w);
                    return (p != nullptr) ? p->name() : nullptr;
                },
                &_scan);
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
            // ...and the plant's name for a unit id, from the same place the
            // field-bus log lines get it.
            _monitor_page.set_device_names(
                [](const void* ctx, uint8_t unit) -> const char* {
                    return Modbus::device_tag(
                        static_cast<const Modbus::DeviceTable*>(ctx), unit);
                },
                _site.devices);
#endif
#endif
#if BOTANIK_FEATURE_TASKS_PAGE
            _console.terminal().add_page(&_tasks_page);
#endif
#if BOTANIK_FEATURE_NOTES_PAGE
            _console.terminal().add_page(&_console.notes());
#endif
        }

#if BOTANIK_FEATURE_SHELL_PAGE
        /** @brief Register the site's shell command table (tried BEFORE the
         *  built-ins, so the table can shadow one - avoid reusing exit/reset).
         *  Default: the descriptor's commands, reading the live variable bank
         *  as their ctx.
         *
         *  Gated on SHELL_PAGE, not CONSOLE: ShellPage owns the CommandShell, so
         *  without the page there is nothing to register commands on. */
        virtual void register_commands() {
            _console.shell().set_commands(_site.commands, _site.command_count,
                                          (void*)&_io);
        }
#endif
#endif

#if BOTANIK_FEATURE_MQTT
        /** @brief Register the site's MQTT telemetry, published after the library
         *  base set each cycle. Default: the descriptor's status-variable
         *  groups, resolved generically against the live variable tables. */
        virtual void register_telemetry() {
            telemetry().set_variables(&_io);
        }
#endif

        /** @brief Site post-wiring, run once near the end of setup(). Default:
         *  nothing. */
        virtual void on_setup() {}

        /** @brief Site per-beat work, run each loop() with fresh variable data
         *  (after health, before the logger drains). Named to match the Component
         *  hook (the firmware is the loop driver, not itself a Component).
         *  Default: nothing.
         *
         *  **Runs whatever control authority says.** For bookkeeping, derived
         *  values, telemetry - anything that is still correct to do while the
         *  plant is stopped. Anything that DRIVES the field goes in
         *  on_control(), or it will keep driving it through a health fault. */
        virtual void on_step(uint64_t now_ms) { (void)now_ms; }

        /** @brief Site control law, run each beat only while control authority
         *  is held. Default: nothing.
         *
         *  The gated twin of on_step(), and where a site's own driving belongs.
         *  Control authority is enforced per dispatch point rather than per
         *  write - `Vars::Var` is two pointers passed by value and has no route
         *  to the latch - so the gate has to be here, next to the scan driver's.
         *
         *  Unlike a `Plc::Program` this carries no scheduling state, so there is
         *  nothing to reset on the way down or reseed on the way back up. A site
         *  needing those wants a program, which the driver already gives both.
         *  It runs BEFORE on_step(), so bookkeeping there sees this beat's
         *  decisions. */
        virtual void on_control(uint64_t now_ms) { (void)now_ms; }

        /** @} */

    private:
        /** @brief Resolve every name the site's tables declare into the ref
         *  arrays they carry alongside. The alarm table binds itself when the
         *  model copies it, so it is not repeated here. */
        void _resolve_site_refs() {
            if (_site.home_refs != nullptr && _site.home_fields != nullptr)
                for (uint8_t i = 0; i < _site.home_field_count; i++)
                    _site.home_refs[i] = _io.find(_site.home_fields[i].name);
        }
    };

}  // namespace Botanik

#endif // LIBBOTANIK_FIRMWARE_FIRMWARE_H
