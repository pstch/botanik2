// REVIEW_35: TODO
#ifndef LIBBOTANIK_CORE_FIRMWARE_H
#define LIBBOTANIK_CORE_FIRMWARE_H

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
 * setup() (set_console_baud(), set_monitor_baud(), set_hw_watchdog_ms()).
 *
 *
 * Console-optional (BOTANIK_FEATURE_CONSOLE, default on): the interactive ANSI
 * UI (AnsiTerminal, the built-in pages, the console UART, the register_*()
 * hooks). Off -> headless. Monitor-optional (BOTANIK_FEATURE_MONITOR, default
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
 *    the boolean variable it reads; the AlarmModel splices in the generic
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
 * const Alarms::AlarmCondition g_alarms[] = { ... };  // code, desc, ref, dwell
 * const Vars::VarGroup         g_status[] = { ... };  // status variables
 * const Vars::HomeVar          g_home[]   = { ... };  // home-screen readings
 * const Shell::ShellCommand    g_cmds[]   = { ... };  // site shell commands
 * const Scan::Task             g_tasks[]  = { ... };  // control programs
 * const SiteDescriptor g_site = {
 *     Config::SITE_NAME,
 *     g_cmds,   1,
 *     g_status, 7,
 *     g_home,   13,
 *     g_alarms, 23,
 *     SITE_VAR_TABLES,                     // descriptors + values + string pool
 *     g_tasks,  2, g_task_stats,
 * };
 *
 * class GreenhouseXXX final : public BotanikFirmware {
 * public:
 *     GreenhouseXXX() : BotanikFirmware(g_site) {}
 * };
 * @endcode
 */

#include "../../libbotanik.h"
#include "../utils/assert.h"     // set_assert_logger
#include "../alarms/alarms.h"    // AlarmCondition (site table type)
#include "../health/health.h"    // HealthMonitor (owns the alarm model + notifier)
#include "libbotanik/vars/params.h"   // Vars::Params (the editable variables)
#include "../shell/shell.h"
#include "../vars/model.h"      // Vars::Bank / VarTables
#include "../scan/scan.h"     // Scan::Driver / Task
#include "../vars/access.h"  // VarGroup / HomeVar (status vars)
#include "stacks.h"      // Monitor/Console/NetworkStack composition
#include "libbotanik/vars/params.h"  // LocalParamStore (flash/file params)

#include <cstddef>
#include <cstdint>

namespace Botanik {

    /**
     * @brief The declarative description of a site: its name plus its generated
     * tables. The firmware base owns every page and wires each table
     * generically, so a site is data (these tables), not code.
     *
     * The tables are meant to be generated:
     *   - @c commands      - site shell commands (Shell::ShellCommand).
     *   - @c status_groups - read-only status variables (Vars::VarGroup: a
     *                        sub-topic plus the VarRefs it publishes).
     *   - @c home_fields   - home-screen readings (Vars::HomeVar).
     *   - @c alarms        - alarm conditions (Alarms::AlarmCondition: code,
     *                        desc, the boolean variable it reads, dwell policy).
     *   - @c vars          - the variable tables themselves (Vars::VarTables);
     *                        parameters are the ROLE_PARAM rows within them.
     *   - @c tasks         - the control programs (Scan::Task) + their stats.
     *
     * Any table may be null / count 0. Every pointer is borrowed and must outlive
     * the firmware (site statics). See core/firmware.h "Extending the base".
     */
    struct SiteDescriptor {
        const char*                   name;                ///< == Config::SITE_NAME
        const Shell::ShellCommand*    commands;            ///< site shell commands
        uint8_t                       command_count;
        const Vars::VarGroup* status_groups;       ///< status-variable table
        uint8_t                       status_group_count;
        const Vars::HomeVar* home_fields;         ///< home-screen readings
        uint8_t                       home_field_count;
        const Alarms::AlarmCondition* alarms;              ///< alarm-condition table
        uint8_t                       alarm_count;
        /** @brief Field IO: the variable tables bound to their value stores.
         *  A site with no variables passes a default-constructed VarTables.
         *  (No default member initialisers here: in C++11 an NSDMI would stop
         *  SiteDescriptor being an aggregate, and every site brace-inits it.) */
        Vars::VarTables               vars;
        /** @brief Control programs and their per-task diagnostics. The stats
         *  array is site-owned and must hold @c task_count entries. */
        const Scan::Task*             tasks;
        uint8_t                       task_count;
        Scan::TaskStats*              task_stats;
    };

    /** @brief Generic firmware base: owns the generic components + the loop
     *  order, and exposes each for a site subclass to build on. */
    class BotanikFirmware {

        /** @name Configuration
         *  @{ */

        /** @brief Console UART baud (sizes the terminal TX gates). */
        uint32_t                      _console_baud  = Config::CONSOLE_BAUD;
        /** @brief Monitor UART baud (log-only, no TX gating) */
        uint32_t                      _monitor_baud  = Config::MONITOR_BAUD;

        /** @brief The site's declarative tables + name (borrowed; site statics
         *  outlive the firmware). Drives page/command/telemetry/config wiring. */
        SiteDescriptor                _site;

        /** @brief HW-watchdog period armed in setup(); the harness overrides it
         *  (the simulator dilates it) via set_hw_watchdog_ms() before
         *  setup(). */
        uint32_t                      _hw_watchdog_ms = Config::HW_WATCHDOG_MS;

#if BOTANIK_FEATURE_CONSOLE
        /** @brief Terminal geometry, fixed at construction (the AnsiTerminal
         *  takes it by value and has no resize). Both targets use 80x24. */
        uint16_t                      _console_cols = 80;
        uint16_t                      _console_rows = 24;
#endif

        /** @} */

        /** @name Generic components (declared in construction order)
         *  The optional subsystem groups are bundled into the feature-gated
         *  stacks (core/stacks.h); the firmware owns the shared singletons and
         *  injects them into the stacks by pointer. @{ */

        Logs::Logger                   _logger;
        Scheduler::LoopScheduler       _scheduler;
        Vars::Bank                       _io;          ///< the site's variables
        Vars::KvParamStore             _param_store; ///< one storage key per parameter
        Vars::Params                   _params;      ///< the editable view of the variables
        Scan::Driver                   _scan;        ///< the control programs
        /** @brief Whether the last beat saw a health fault. Edge tracking only
         *  - the control state itself lives in the Bank (control_enabled()), so
         *  an operator's `control disable` is not something the loop can
         *  silently talk over. */
        bool                           _health_fault = false;
        Health::HealthMonitor          _health;
        SystemContext                  _sys_ctx;     ///< Filled in the ctor body
#if BOTANIK_FEATURE_MONITOR
        MonitorStack                   _monitor;     ///< MONITOR UART + log sink
#endif
#if BOTANIK_FEATURE_NETWORK
        NetworkStack                   _net;         ///< resolver/network/ntp/mqtt
#endif
#if BOTANIK_FEATURE_CONSOLE
        ConsoleStack                   _console;     ///< console UART + terminal + pages
        // Site-independent pages the base owns (declared after _console so
        // terminal() is live): the landing page, the editable CONFIG table over
        // the config backend, and the read-only MONITOR table over the site's
        // status-variable groups.
        Pages::HomePage                _home_page;
        Pages::ParamsPage              _params_page;
        Pages::MonitorPage             _monitor_page;
#endif

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Wire up the generic components. @p site: the site's
         *  declarative tables + name (borrowed). Tunable scalars (baud,
         *  watchdog) default from Config; override via the set_*() modifiers
         *  before setup().
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
              _scan(site.tasks, site.task_count, site.task_stats, &_io, &_logger),
              // Site alarm conditions resolve against the variable tables;
              // platform-health alarms are driven locally either way.
              _health(_site.alarms, _site.alarm_count, &_logger,
                      _scheduler.context(), &_io)
              , _sys_ctx()   // fields wired in the body, once addresses are known
#if BOTANIK_FEATURE_MONITOR
              , _monitor(&_logger)
#endif
#if BOTANIK_FEATURE_NETWORK
              // Network reads its config from the variable bank by name;
              // telemetry publishes the live _sys_ctx (read later).
              , _net(&_io, &_logger, &_sys_ctx)
#endif
#if BOTANIK_FEATURE_CONSOLE
              , _console(_console_cols, _console_rows,
                         &_scheduler.context()->timestamp_ms, _console_baud,
                         &_health.model(), &_logger)
              // Base-owned pages, built from the descriptor + the live payload.
              , _home_page(_console.terminal(), _site.home_fields,
                           _site.home_field_count, &_io)
              , _params_page(_console.terminal(), &_params)
              , _monitor_page(_console.terminal(), _site.status_groups,
                              _site.status_group_count, &_io)
#endif
        {
            // Wire the status aggregate now every subsystem address is fixed,
            // then hand a copy to the terminal (it holds SystemContext by value).
            _sys_ctx.scheduler = _scheduler.context();
            _sys_ctx.vars      = &_io;
#if BOTANIK_FEATURE_NETWORK
            _sys_ctx.network = &_net.network();
#endif
#if BOTANIK_FEATURE_NTP
            _sys_ctx.ntp = &_net.ntp();
#endif
#if BOTANIK_FEATURE_MQTT
            _sys_ctx.mqtt = &_net.mqtt();
#endif
#if BOTANIK_FEATURE_CONSOLE
            _console.terminal().set_context(_sys_ctx);
#endif
        }


        virtual ~BotanikFirmware() {}

        /** @brief Bring-up in a fixed, non-overridable order: begin the UARTs,
         *  register the core sinks, register the pages (register_pages()),
         *  select the terminal type, show the banner, arm the watchdog. POST
         *  runs on HealthMonitor's first step() once loop() starts. A site
         *  customizes via the hooks, never setup(). */
        void setup() {
            // route BOTANIK_ASSERT failures to the log
            set_assert_logger(&_logger);
            // Register the site's config parameters (appended after the library
            // built-ins) so the PARAMS page and the `set` command see them.
            // Seed every variable with its declared default, so a value read
            // before any driver has spoken is the default rather than zero.
            _io.init();
#if BOTANIK_FEATURE_CONSOLE
            HAL::Serial::get(HAL::Serial::CONSOLE)->begin(_console_baud);
#endif
#if BOTANIK_FEATURE_MONITOR
            HAL::Serial::get(HAL::Serial::MONITOR)->begin(_monitor_baud);
#endif

            // Core sinks: monitor + (with the UI) shell/logs pages, plus a
            // TX-drop watch on each live UART. A site adds its own in
            // on_setup().
#if BOTANIK_FEATURE_MONITOR
            _logger.add_writer(&_monitor.writer());
            _logger.watch_tx(&_monitor.serial());
#endif
#if BOTANIK_FEATURE_CONSOLE
            _logger.add_writer(&_console.shell());
            _logger.add_writer(&_console.logs());
            // The home page sinks the log stream into its bottom log-tail band.
            _logger.add_writer(&_home_page);
            _logger.watch_tx(&_console.serial());
#endif

#if BOTANIK_FEATURE_CONSOLE
            // Let the shell's loglevel / alarm-ack commands reach the live
            // logger + model.
            _console.shell().set_diagnostics(&_logger, &_health.model());
#if BOTANIK_FEATURE_NTP
            // Let the `ntpsync` command force a resync on the live client.
            _console.shell().set_ntp(&_net.ntp());
#endif

            register_commands();   // site commands, tried after the built-ins
            // tab order (page 0 = boot page); switch_to(0) records it
            register_pages();
            _console.terminal().setup_terminal();
            _console.terminal().switch_to(0);
#endif

            // Details on a second line (the renderer splits on '\n') so it
            // wraps cleanly.
            _logger.info(Logs::BOOT, "Botanik v%s\nRunning on : %s",
                         Config::FIRMWARE_VERSION, HAL::hardware_description());

#if BOTANIK_FEATURE_MQTT
            // Route alarm transitions out through the MQTT client (it is an
            // Alarms::Publisher). Until now the notifier was a no-op.
            _health.set_alarm_publisher(&_net.mqtt());
            // Stream log entries out to the broker (best-effort, own min level).
            _net.mqtt_log().set_publisher(&_net.mqtt());
            _logger.add_writer(&_net.mqtt_log());
            register_telemetry();   // site telemetry fields (base set always on)
#endif

            on_setup();   // site post-wiring (extra sinks / components)

            HAL::Watchdog::start(_hw_watchdog_ms);

            // Bring-up is done: the field is ours to drive. Control starts
            // disabled (Bank's default), so nothing between power-on and here -
            // a half-wired component, a site hook that faults - can energise an
            // actuator on the way past.
            _io.enable_control();

            // One line per UART so neither overflows 80 cols and each gates with
            // its feature (console/monitor dropped when their UART is absent).
            _logger.debug(Logs::BOOT, "boot complete\nHW watchdog %lu ms",
                          (unsigned long)_hw_watchdog_ms);
#if BOTANIK_FEATURE_CONSOLE
            _logger.debug(Logs::BOOT, "console %lu baud", (unsigned long)_console_baud);
#endif
#if BOTANIK_FEATURE_MONITOR
            _logger.debug(Logs::BOOT, "monitor %lu baud", (unsigned long)_monitor_baud);
#endif
        }

        /** @brief One main-loop iteration. tick() gates a beat and republishes
         *  the display "now" (`SchedulerContext::timestamp_ms`) the render path
         *  shares.
         *  The order is load-bearing (see CONCURRENCY.md):
         *    tick -> serial.receive -> params -> io.expire_stale -> health -> net
         *         -> scan -> on_step -> logger -> terminal/drain
         *         -> serial.transmit -> watchdog_kick
         *  Every stepped unit is a Component driven by step(now_ms) with a fresh
         *  HAL::Clock::uptime_ms(). Non-virtual so the order can't be reordered;
         *  a site adds per-beat work via on_step(). */
        void loop() {
            if (!_scheduler.tick()) return;   // driver: gates the beat, produces the clock

            // One 64-bit "now" for every Component stepped this beat.
            uint64_t now_ms = HAL::Clock::uptime_ms();

            // Serial RX at the top: fresh input before processing. BufferedSerial
            // is a Component (step() == receive()+transmit()), but the loop drives
            // its two halves at the phases they belong - RX here, TX after the
            // render below. Stepping the full pump at both ends would cost an extra
            // read() syscall each beat and split the console's cooperative,
            // baud-paced TX across the render, perturbing throughput.
#if BOTANIK_FEATURE_CONSOLE
            _console.serial().receive();
#endif
#if BOTANIK_FEATURE_MONITOR
            _monitor.serial().receive();
#endif

            // Params first: the config-driven Components (net/sun) read the
            // image below. Loads local storage on the first step.
            _params.step(now_ms);

            // Read phase: age out readings past their timeout, so one beat is
            // coherent. Field drivers slot in here, before anything reads.
            _io.expire_stale(now_ms);

            // Health before the logger drains, so an alarm START/END lands this beat.
            _health.step(now_ms);

#if BOTANIK_FEATURE_NETWORK
            // Config-driven network services (resolver/network/ntp/mqtt +
            // telemetry). On the simulator these talk over loopback to an
            // in-process mock responder (botanik_sim/network.h); no public
            // network I/O.
            _net.step(now_ms);
#endif

            // Withdrawing control is a LATCH, not a per-beat sweep. Sweeping
            // outputs after the scan would have been a flicker: the next beat's
            // tasks re-drive the same demands before the sweep runs again. So a
            // fault takes control away and inhibits the scan, and the outputs
            // stay at their safe state until health actually recovers.
            //
            // Edge-triggered on the fault, so an operator who typed `control
            // disable` is not overruled every beat by a healthy firmware. A
            // health RECOVERY does re-enable: coming back from a fault is a
            // strong enough signal to hand control back.
            if (!_health.watchdog_ok()) {
                if (!_health_fault) {
                    _health_fault = true;
                    _io.disable_control();
                    _logger.critical(Logs::HEALTH,
                        "control disabled: outputs at their safe state, scan inhibited");
                }
            } else if (_health_fault) {
                _health_fault = false;
                _io.enable_control();
                _logger.warning(Logs::HEALTH, "control enabled, scan resuming");
            }

            // Execute phase: every control program that has come due. Inhibited
            // while control is withheld, so nothing can drive an output back
            // out of its safe state.
            if (_io.control_enabled()) _scan.step(now_ms);

            on_step(now_ms);   // site per-beat state machines (default no-op)

            _logger.step();

            // Middle logic: terminal render + monitor log-queue drain.
#if BOTANIK_FEATURE_CONSOLE
            _console.step(now_ms);
#endif
#if BOTANIK_FEATURE_MONITOR
            _monitor.writer().step(now_ms);
#endif

            // Serial TX at the bottom: push this beat's rendered output out after
            // everything drew.
#if BOTANIK_FEATURE_CONSOLE
            _console.serial().transmit();
#endif
#if BOTANIK_FEATURE_MONITOR
            _monitor.serial().transmit();
#endif

            // Kick while healthy; HealthMonitor is what decides that (see
            // health/health.h). Withholding the kick means we have chosen to let
            // the hardware watchdog reset us, so the field is dropped to its safe
            // state FIRST - the reset itself gives no such guarantee, and the gap
            // between the last kick and the reboot is exactly when an actuator
            // must not be left energised.
            if (_health.watchdog_ok()) HAL::Watchdog::kick();
            else                       _io.disable_control();
        }

        /** @brief Run loop() forever (the OPTA entry point; the simulator
         *  drives loop() itself so it can break on a shutdown signal). */
        void run() { for (;;) loop(); }

        /** @} */

        /** @name Per-target tuning (call before setup())
         *  @{ */

        /** @brief Override the console baud read by setup() (sizes the terminal
         *  TX gates). */
        void set_console_baud(uint32_t baud) { _console_baud = baud; }

        /** @brief Override the monitor baud read by setup(). */
        void set_monitor_baud(uint32_t baud) { _monitor_baud = baud; }

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
         *  home / shell / logs / alarms / config / monitor. A site override can
         *  reorder them or splice in its own (e.g. re-add a STATUS page by
         *  subclassing Pages::StatusPage and add_page()-ing it here). */
        virtual void register_pages() {
            _console.terminal().add_page(&_home_page);       // page 0 (boot page)
            _console.terminal().add_page(&_console.shell());
            _console.terminal().add_page(&_console.logs());
            _console.terminal().add_page(&_console.alarms());
            _console.terminal().add_page(&_params_page);
            _console.terminal().add_page(&_monitor_page);
        }

        /** @brief Register the site's shell command table (tried BEFORE the
         *  built-ins, so the table can shadow one - avoid reusing exit/reset).
         *  Default: the descriptor's commands, reading the live variable bank
         *  as their ctx. */
        virtual void register_commands() {
            _console.shell().set_commands(_site.commands, _site.command_count,
                                          (void*)&_io);
        }
#endif

#if BOTANIK_FEATURE_MQTT
        /** @brief Register the site's MQTT telemetry, published after the library
         *  base set each cycle. Default: the descriptor's status-variable
         *  groups, resolved generically against the live variable tables. */
        virtual void register_telemetry() {
            telemetry().set_site_scalar_groups(_site.status_groups,
                                                _site.status_group_count, &_io);
        }
#endif

        /** @brief Site post-wiring, run once near the end of setup(). Default:
         *  nothing. */
        virtual void on_setup() {}

        /** @brief Site per-beat work, run each loop() with fresh variable data
         *  (after health, before the logger drains). Named to match the Component
         *  hook (the firmware is the loop driver, not itself a Component).
         *  Default: nothing. */
        virtual void on_step(uint64_t now_ms) { (void)now_ms; }

        /** @} */
    };

}  // namespace Botanik

#endif // LIBBOTANIK_CORE_FIRMWARE_H
