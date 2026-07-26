// REVIEW_35: TODO
#ifndef LIBBOTANIK_CORE_FIRMWARE_H
#define LIBBOTANIK_CORE_FIRMWARE_H

/**
 * @file firmware.h
 * @brief BotanikFirmware<In, Out> - the generic firmware base.
 *
 * Owns every long-lived generic component (serials, logger, scheduler, PLC
 * sync, alarm model/reader, terminal, core pages) and the per-tick loop()
 * order, and exposes a reference accessor for each so a site subclass can build
 * its own components on top. A site derives
 * `GreenhouseXXX : BotanikFirmware<In, Out>`, adds its pages as members, and
 * customizes through the register_*() hooks; see docs and CLAUDE.md
 * ("Site-specific code").
 *
 * Target-independent: the shared PLC region is injected by the caller (sim
 * statics, or HAL::PlcIO::input()/output() on OPTA). Tunable scalars default
 * from Config and the harness overrides them through a set_*() modifier before
 * setup() (set_console_baud(), set_monitor_baud(), set_hw_watchdog_ms()).
 *
 * PLC-optional (BOTANIK_FEATURE_PLC, default on): the sync handler, log reader,
 * payload, and PLC-fault watchdog escalation. With -DBOTANIK_FEATURE_PLC=OFF
 * derive BotanikFirmware<> - the payload params default to the zero-size Empty
 * type, the loop feeds health a zeroed remote bitset (only local platform
 * alarms fire), and HealthMonitor runs with a null link context.
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
 *  - **Descriptor tables** (SiteDescriptor) - the alarm conditions (each with the
 *    byte offset of its boolean in the PLC payload; the AlarmModel splices in the
 *    generic platform-health alarms from alarms/platform.h), config parameters,
 *    shell commands, and status-variable groups. Pure data; no hook.
 *  - **Pages** - the default register_pages() registers the full set. A site only
 *    overrides it to reorder or splice in an extra page (e.g. re-adding a STATUS
 *    page by subclassing Pages::StatusPage). Page 0 is the boot page.
 *  - **Post-wiring / per-beat** - on_setup() runs once near the end of setup();
 *    on_step(now_ms) runs each beat with fresh PLC data.
 *
 * Worked example (the common case: no custom code, just the descriptor):
 * @code
 * // --- site-static tables (single source of truth for this greenhouse) ---
 * const Alarms::AlarmCondition g_alarms[]  = { ... };  // code, desc, offset, dwell
 * const Network::ScalarGroup   g_status[]  = { ... };  // status variables
 * const Shell::ShellCommand    g_cmds[]    = { ... };  // site shell commands
 * const SiteDescriptor g_site = {
 *     Config::SITE_NAME,
 *     nullptr, 0,                          // config parameters (optional)
 *     g_cmds,   1,
 *     g_status, 7,
 *     g_home,   13,                        // home-screen readings (optional)
 *     g_alarms, 23,
 * };
 *
 * class GreenhouseXXX final : public BotanikFirmware<PlcInputData_t, PlcOutputData_t> {
 * public:
 *     GreenhouseXXX(PlcIO::PlcInput<PlcInputData_t>* in,
 *                   PlcIO::PlcOutput<PlcOutputData_t>* out)
 *         : BotanikFirmware(in, out, g_site) {}
 * };
 * @endcode
 */

#include "libbotanik.h"
#include "libbotanik/utils/assert.h"     // set_assert_logger
#include "libbotanik/alarms/alarms.h"    // AlarmCondition (site table type)
#include "libbotanik/health/health.h"    // HealthMonitor (owns the alarm model + notifier)
#include "libbotanik/core/config.h"      // Cfg::ConfigParam (descriptor param table)
#include "libbotanik/shell/shell.h"      // Shell::ShellCommand (descriptor command table)
#include "libbotanik/network/telemetry_types.h"  // Network::ScalarGroup (status vars)
#include "libbotanik/core/stacks.h"      // Monitor/Console/NetworkStack composition
#if BOTANIK_FEATURE_PLC
#include "libbotanik/core/config_plc.h"  // PlcConfigHandler (PLC-owned config)
#else
#include "libbotanik/core/config_local.h"// LocalConfigHandler (flash/file config)
#endif

#include <cstddef>
#include <cstdint>

namespace Botanik {

    /** @brief Zero-size default PLC payload, used to fill the BotanikFirmware
     *  template params when the PLC feature is not enabled. */
    struct Empty {};

    /**
     * @brief The declarative description of a site: its name plus the four
     * generated tables. The firmware base owns every page and wires each table
     * generically, so a site is data (these tables + its PLC payload), not code.
     *
     * The tables mirror what the Arduino PLC project exposes and are meant to be
     * generated from it:
     *   - @c params        - config parameters (Cfg::ConfigParam: name, offset,
     *                        type, scaling, bounds, group).
     *   - @c commands      - site shell commands (Shell::ShellCommand).
     *   - @c status_groups - read-only status variables (Network::ScalarGroup /
     *                        ScalarField: name, offset, type, scaling, desc).
     *   - @c alarms        - alarm conditions (Alarms::AlarmCondition: code,
     *                        desc, payload boolean offset, dwell policy).
     *
     * Any table may be null / count 0. Every pointer is borrowed and must outlive
     * the firmware (site statics). See core/firmware.h "Extending the base".
     */
    struct SiteDescriptor {
        const char*                   name;                ///< == Config::SITE_NAME
        const Cfg::ConfigParam*       params;              ///< site config parameters
        uint8_t                       param_count;
        const Shell::ShellCommand*    commands;            ///< site shell commands
        uint8_t                       command_count;
        const Network::ScalarGroup*   status_groups;       ///< status-variable table
        uint8_t                       status_group_count;
        const Network::HomeField*     home_fields;         ///< home-screen readings
        uint8_t                       home_field_count;
        const Alarms::AlarmCondition* alarms;              ///< alarm-condition table
        uint8_t                       alarm_count;
    };

    /** @brief Generic firmware base: owns the generic components + the loop
     *  order, and exposes each for a site subclass to build on. The PLC pieces
     *  are compiled in only when BOTANIK_FEATURE_PLC is set. */
    template<typename In = Empty, typename Out = Empty>
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
#if BOTANIK_FEATURE_PLC
        PlcIO::PlcSyncHandler<In, Out> _plc_handler;
        PlcIO::PlcLogReader            _plc_log_reader;
        Cfg::PlcConfigHandler          _config;       ///< PLC-owned config snapshot
#else
        Cfg::LocalConfigHandler        _config;       ///< flash/file-backed config
#endif
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
        Pages::ConfigPage              _config_page;
        Pages::MonitorPage             _monitor_page;
#endif

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Wire up the generic components. @p shared_in / @p shared_out:
         *  the caller-owned PLC region; @p site: the site's declarative tables +
         *  name (borrowed). Tunable scalars (baud, watchdog) default from Config;
         *  override via the set_*() modifiers before setup(). */
        BotanikFirmware(PlcIO::PlcInput<In>*  shared_in,
                        PlcIO::PlcOutput<Out>* shared_out,
                        const SiteDescriptor& site)
            : _site(site),
              _logger(),
              _scheduler(Config::TASK_PERIOD_MS, &_logger),
#if BOTANIK_FEATURE_PLC
              _plc_handler(shared_in, shared_out,
                           &_scheduler.context()->timestamp_ms, &_logger),
              _plc_log_reader(_plc_handler.context(), &_logger),
              _config(_plc_handler.context(), &_logger),
              // Health owns the alarm model, which reads each site alarm's
              // boolean from the PLC output payload directly (its offset).
              _health(_site.alarms, _site.alarm_count, &_logger,
                      _scheduler.context(), _plc_handler.context(),
                      shared_in, shared_out, plc_output_data())
#else
              // No PLC: config comes from local storage; health takes a null link
              // context + null regions + an empty reader (only local platform
              // alarms fire, it degrades to "kick while alive").
              _config(&_logger),
              _health(_site.alarms, _site.alarm_count, &_logger,
                      _scheduler.context(), nullptr, nullptr, nullptr)
#endif
              , _sys_ctx()   // fields wired in the body, once addresses are known
#if BOTANIK_FEATURE_MONITOR
              , _monitor(&_logger)
#endif
#if BOTANIK_FEATURE_NETWORK
              // Network reads its config from the ConfigHandler image (NETWORK
              // requires PLC); telemetry publishes the live _sys_ctx (read later).
              , _net(&_config, &_logger, &_sys_ctx)
#endif
#if BOTANIK_FEATURE_CONSOLE
              , _console(_console_cols, _console_rows,
                         &_scheduler.context()->timestamp_ms, _console_baud,
                         &_health.model(), &_logger)
              // Base-owned pages, built from the descriptor + the live payload.
              , _home_page(_console.terminal(), _site.home_fields,
                           _site.home_field_count, plc_output_data())
              , _config_page(_console.terminal(), &_config)
              , _monitor_page(_console.terminal(), _site.status_groups,
                              _site.status_group_count, plc_output_data())
#endif
        {
            // Wire the status aggregate now every subsystem address is fixed,
            // then hand a copy to the terminal (it holds SystemContext by value).
            _sys_ctx.scheduler = _scheduler.context();
#if BOTANIK_FEATURE_PLC
            _sys_ctx.plc = _plc_handler.context();
#endif
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
#if !BOTANIK_FEATURE_PLC
            (void)shared_in; (void)shared_out;   // unused without the PLC link
#endif
        }

#if !BOTANIK_FEATURE_PLC
        /** @brief PLC-less convenience ctor: no shared region, so it omits the
         *  region pointers and delegates to the main ctor with null. */
        explicit BotanikFirmware(const SiteDescriptor& site)
            : BotanikFirmware(nullptr, nullptr, site) {}
#endif

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
            // built-ins) so the CONFIG page and the `config` command see them.
            _config.set_site_params(_site.params, _site.param_count);
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
            // Let the `config` get/set command reach the config backend.
            _console.shell().set_config(&_config);
            // Let the `status` command render the site status-variable table.
            _console.shell().set_status_vars(_site.status_groups,
                                             _site.status_group_count,
                                             plc_output_data());
#if BOTANIK_FEATURE_NTP
            // Let the `ntpsync` command force a resync on the live client.
            _console.shell().set_ntp(&_net.ntp());
#endif

            register_commands();   // site commands, tried after the built-ins
            // tab order (page 0 = boot page); switch_to(0) records it
            register_pages();
            _console.terminal().setup_vt102();
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
         *  the 32-bit display "now" (`SchedulerContext::timestamp_ms`) the
         *  render path shares.
         *  The order is load-bearing (see CONCURRENCY.md):
         *    tick -> serial.receive -> plc -> health -> net -> on_step -> logger
         *         -> terminal/drain -> serial.transmit -> watchdog_kick
         *  Every stepped unit is a Component driven by step(now_ms) with a fresh
         *  64-bit HAL::Clock::uptime_ms(), NOT the 32-bit timestamp_ms - their
         *  fault-grace / alarm-latch timers want a wrap-free clock. Non-virtual
         *  so the order can't be reordered; a site adds per-beat work via
         *  on_step(). */
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

#if BOTANIK_FEATURE_PLC
            _plc_handler.step(now_ms);
            _plc_log_reader.step(now_ms);
#endif
            // Config backend: refresh the image from the fresh PLC snapshot (or,
            // PLC-less, load local storage on the first step) before the
            // config-driven Components (net/sun) read it below.
            _config.step(now_ms);

            // Health after the PLC payload is fresh and before the logger
            // drains, so an alarm START/END lands this beat. It reads the remote
            // alarm bitset + torn count through its stored pointers (both null
            // without a PLC, so only local platform alarms fire).
            _health.step(now_ms);

#if BOTANIK_FEATURE_NETWORK
            // Config-driven network services (resolver/network/ntp/mqtt +
            // telemetry). On the simulator these talk over loopback to an
            // in-process mock responder (botanik_sim/network.h); no public
            // network I/O.
            _net.step(now_ms);
#endif

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

            // Kick while healthy; HealthMonitor withholds only once a PLC fault
            // persists past the grace window (the deliberate reboot).
            if (_health.watchdog_ok()) HAL::Watchdog::kick();
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

#if BOTANIK_FEATURE_PLC
        /** @name HAL region binding (hardware targets)
         *  @{ */

        /** @brief HAL's PLC region cast to the typed PlcInput/PlcOutput<>. The
         *  one place the cast from HAL's opaque void* lives. On the simulator
         *  HAL returns null (the sim binds its own mock region through the
         *  explicit ctor). */
        static PlcIO::PlcInput<In>* hal_input() {
            return static_cast<PlcIO::PlcInput<In>*>(HAL::PlcIO::input());
        }
        static PlcIO::PlcOutput<Out>* hal_output() {
            return static_cast<PlcIO::PlcOutput<Out>*>(HAL::PlcIO::output());
        }

        /** @} */
#endif

        /** @name Component access (for the site subclass / harness)
         *  @{ */

        Logs::Logger&                   logger()          { return _logger; }
        Scheduler::LoopScheduler&       scheduler()       { return _scheduler; }
#if BOTANIK_FEATURE_PLC
        PlcIO::PlcSyncHandler<In, Out>& plc_handler()     { return _plc_handler; }
        const Out*                      plc_output_data() { return _plc_handler.output_data(); }
#else
        /** @brief No PLC link: no shared payload, so null (readers
         *  null-check). */
        const Out*                      plc_output_data() { return nullptr; }
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
        Health::HealthMonitor&          health()          { return _health; }
        Alarms::AlarmModel&             alarm_model()     { return _health.model(); }
        Cfg::ConfigHandler&             config()          { return _config; }
#if BOTANIK_FEATURE_CONSOLE
        Terminal::Console&              terminal()        { return _console.terminal(); }
        Pages::ShellPage&               shell_page()      { return _console.shell(); }
        Pages::LogsPage&                logs_page()       { return _console.logs(); }
        Pages::AlarmsPage&              alarms_page()     { return _console.alarms(); }
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
            _console.terminal().add_page(&_config_page);
            _console.terminal().add_page(&_monitor_page);
        }

        /** @brief Register the site's shell command table (tried after the
         *  built-ins, so a site can't shadow exit/reset). Default: the
         *  descriptor's commands, reading the live PLC payload as their ctx. */
        virtual void register_commands() {
            _console.shell().set_commands(_site.commands, _site.command_count,
                                          (void*)plc_output_data());
        }
#endif

#if BOTANIK_FEATURE_MQTT
        /** @brief Register the site's MQTT telemetry, published after the library
         *  base set each cycle. Default: the descriptor's status-variable groups,
         *  read generically off the live PLC payload. */
        virtual void register_telemetry() {
            telemetry().set_site_scalar_groups(_site.status_groups,
                                               _site.status_group_count,
                                               plc_output_data());
        }
#endif

        /** @brief Site post-wiring, run once near the end of setup(). Default:
         *  nothing. */
        virtual void on_setup() {}

        /** @brief Site per-beat work, run each loop() with fresh PLC data
         *  (after health, before the logger drains). Named to match the Component
         *  hook (the firmware is the loop driver, not itself a Component).
         *  Default: nothing. */
        virtual void on_step(uint64_t now_ms) { (void)now_ms; }

        /** @} */
    };

}  // namespace Botanik

#endif // LIBBOTANIK_CORE_FIRMWARE_H
