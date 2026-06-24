#ifndef LIBBOTANIK_FIRMWARE_H
#define LIBBOTANIK_FIRMWARE_H

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
 * statics, or HAL::PlcIO::input()/get_plc_output() on OPTA). Per-target timing
 * is applied by the harness through the accessors / set_hw_watchdog_ms() before
 * setup().
 *
 * PLC-optional (BOTANIK_FEATURE_PLC, default on): the sync handler, log reader,
 * payload, and PLC-fault watchdog escalation. With -DBOTANIK_FEATURE_PLC=OFF
 * derive BotanikFirmware<> - the payload params default to the zero-size Empty
 * type, the loop feeds health a zeroed remote bitset (only local platform
 * alarms fire), and
 * HealthMonitor runs with a null link context.
 *
 * Terminal-optional (BOTANIK_FEATURE_TERMINAL, default on): the ANSI UI
 * (AnsiTerminal, the built-in pages, the console UART, the register_*() hooks).
 * Off -> headless; diagnostics still reach the MONITOR UART via
 * MonitorLogWriter. The site extension below assumes the UI is present.
 *
 * @section firmware_extend Extending the base for a site
 *
 * A site is a thin subclass contributing data (PLC payload, alarm table, pages,
 * shell commands), never overriding setup()/loop() - only the protected
 * Template-Method hooks, so the order can't be misordered. Extension surface:
 *
 *  - **Alarm table** - ctor arg (`alarm_table`, `alarm_count`). The AlarmModel
 *    splices in the generic platform-health alarms (alarms/platform.h):
 *    site/PLC alarms fill bit 0 up, platform alarms sit at the high end. Pure
 *    data; no hook.
 *  - **Pages** - override register_pages() for the tab order (page 0 = boot
 *    page); default registers the built-ins (shell/logs/alarms).
 *  - **Shell commands** - override register_commands(), call
 *    shell_page().set_commands(...). Tried after the built-ins (can't shadow
 *    exit/reset).
 *  - **Post-wiring / per-beat** - on_setup() runs once near the end of setup();
 *    on_loop(now_ms) runs each beat with fresh PLC data.
 *
 * Worked example (a site with one extra page and one extra command):
 * @code
 * // --- site-static tables (single source of truth for this greenhouse) ---
 * const Alarms::AlarmCondition g_alarms[] = { ... };  // alarms.h (header-only)
 * const uint8_t                g_alarm_count = ...;
 *
 * static void cmd_grow(Shell::ShellWriter& out, const char*, uint16_t, void*) {
 *     static const char msg[] = "growth program nominal";
 *     out.write(msg, sizeof(msg) - 1);
 * }
 * static const Shell::ShellCommand g_commands[] = {
 *     { "grow", "growth program status", &cmd_grow },
 * };
 *
 * class GreenhouseXXX final : public BotanikFirmware<PlcInputData_t, PlcOutputData_t> {
 *     SiteHomePage _home;   // built from the base's components (see ctor)
 * public:
 *     GreenhouseXXX(PlcIO::PlcInput<PlcInputData_t>* in,
 *                   PlcIO::PlcOutput<PlcOutputData_t>* out, uint32_t baud)
 *         : BotanikFirmware(in, out, g_alarms, g_alarm_count, baud),
 *           _home(terminal(), plc_output_data()) {}
 * protected:
 *     void register_pages()    override { terminal().add_page(&_home);            // page 0
 *                                         terminal().add_page(&shell_page()); }   // ...
 *     void register_commands() override { shell_page().set_commands(
 *                                             g_commands, 1, this); }
 *     void on_setup()          override { logger().add_writer(&_home); }
 * };
 * @endcode
 */

#include "libbotanik.h"
#include "libbotanik/utils/assert.h"     // set_assert_logger
#include "libbotanik/alarms/alarms.h"    // AlarmCondition (site table type)
#include "libbotanik/health/health.h"    // HealthMonitor (owns the alarm model + notifier)
#if BOTANIK_FEATURE_NETWORK
#include "libbotanik/network/resolver.h" // NameResolver
#include "libbotanik/network/network.h"  // NetworkHandler, NtpClient
#endif
#if BOTANIK_FEATURE_MQTT
#include "libbotanik/network/mqtt.h"     // MqttClient
#endif

#include <cstddef>
#include <cstdint>

namespace Botanik {

    /** @brief Zero-size default PLC payload: the template params default to
     *  this so a PLC-less firmware (BOTANIK_FEATURE_PLC=0) derives
     *  BotanikFirmware<>. */
    struct Empty {};

    /** @brief Generic firmware base: owns the generic components + the loop
     *  order, and exposes each for a site subclass to build on. The PLC pieces
     *  are compiled in only when BOTANIK_FEATURE_PLC is set. */
    template<typename In = Empty, typename Out = Empty>
    class BotanikFirmware {
        /** @name Configuration
         *  @{ */

        /** @brief Console/monitor baud (sizes the TX gates). */
        uint32_t                      _baud;
        /** @brief Site PLC alarm table (copied into the model). */
        const Alarms::AlarmCondition* _alarm_table;
        size_t                        _alarm_count;
        /** @brief HW-watchdog period armed in setup(); the harness overrides it
         *  (the simulator dilates it) via set_hw_watchdog_ms() before
         *  setup(). */
        uint32_t                      _hw_watchdog_ms = Config::HW_WATCHDOG_MS;

        /** @} */

        /** @name Generic components (declared in construction order)
         *  @{ */

#if BOTANIK_FEATURE_TERMINAL
        // dropped headless (no UI)
        Utils::BufferedSerial          _console_serial;
#endif
        Utils::BufferedSerial          _monitor_serial;
        Logs::Logger                   _logger;
        Logs::MonitorLogWriter         _monitor;
        Scheduler::LoopScheduler       _scheduler;
#if BOTANIK_FEATURE_PLC
        PlcIO::PlcSyncHandler<In, Out> _plc_handler;
        PlcIO::PlcLogReader            _plc_log_reader;
#endif
        Health::HealthMonitor          _health;
        SystemContext                  _sys_ctx;
#if BOTANIK_FEATURE_NETWORK
        Network::NameResolver          _resolver;
        Network::NetworkHandler        _network;
#endif
#if BOTANIK_FEATURE_NTP
        Network::NtpClient             _ntp;
#endif
#if BOTANIK_FEATURE_MQTT
        Network::MqttClient            _mqtt;
#endif
#if BOTANIK_FEATURE_TERMINAL
        Terminal::AnsiTerminal         _terminal;
        Pages::ShellPage               _shell_page;
        Pages::LogsPage                _logs_page;
        Pages::AlarmsPage              _alarms_page;
#endif

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Wire up the generic components. @p shared_in / @p shared_out:
         *  the caller-owned PLC region; @p cols / @p rows: terminal geometry
         *  (entry-point supplied, like the baud). */
        BotanikFirmware(PlcIO::PlcInput<In>*  shared_in,
                        PlcIO::PlcOutput<Out>* shared_out,
                        const Alarms::AlarmCondition* alarm_table, size_t alarm_count,
                        uint32_t console_baud, uint16_t cols = 80, uint16_t rows = 24)
            : _baud(console_baud),
              _alarm_table(alarm_table),
              _alarm_count(alarm_count),
#if BOTANIK_FEATURE_TERMINAL
              _console_serial(HAL::Serial::get(HAL::Serial::CONSOLE)),
#endif
              _monitor_serial(HAL::Serial::get(HAL::Serial::MONITOR)),
              _logger(),
              _monitor(_monitor_serial),
              _scheduler(Config::TASK_PERIOD_MS, &_logger),
#if BOTANIK_FEATURE_PLC
              _plc_handler(shared_in, shared_out,
                           &_scheduler.context()->timestamp_ms, &_logger),
              _plc_log_reader(_plc_handler.context(), &_logger),
              _health(_alarm_table, (uint8_t)_alarm_count, &_logger,
                      _scheduler.context(), _plc_handler.context(),
                      shared_in, shared_out),
              _sys_ctx(_scheduler.context(), _plc_handler.context())
#else
              // No PLC: health takes a null link context + null regions (it
              // degrades the verdict to "kick while alive"); SystemContext.plc
              // stays null.
              _health(_alarm_table, (uint8_t)_alarm_count, &_logger,
                      _scheduler.context(), nullptr, nullptr, nullptr),
              _sys_ctx(_scheduler.context())
#endif
              // Past _sys_ctx is optional; leading commas let a feature drop
              // out cleanly.
#if BOTANIK_FEATURE_NETWORK
              // Network reads its config from the PLC block (NETWORK requires
              // PLC); the resolver is shared by every consumer.
              , _resolver(&_logger)
              , _network(_plc_handler.context(), &_logger, &_resolver)
#endif
#if BOTANIK_FEATURE_NTP
              , _ntp(_plc_handler.context(), &_logger, &_resolver, &_network)
#endif
#if BOTANIK_FEATURE_MQTT
              , _mqtt(_plc_handler.context(), &_logger, &_resolver, &_network)
#endif
#if BOTANIK_FEATURE_TERMINAL
              , _terminal(_console_serial, cols, rows, &_scheduler.context()->timestamp_ms,
                          _baud, _sys_ctx, &_health.model(), &_logger)
              , _shell_page(_terminal)
              , _logs_page(_terminal)
              , _alarms_page(_terminal, &_health.model())
#endif
        {
#if !BOTANIK_FEATURE_PLC
            (void)shared_in; (void)shared_out;   // unused without the PLC link
#endif
#if !BOTANIK_FEATURE_TERMINAL
            (void)cols; (void)rows;   // terminal geometry unused without the UI
#endif
        }

#if !BOTANIK_FEATURE_PLC
        /** @brief PLC-less convenience ctor: no shared region, so it omits the
         *  region pointers and delegates to the main ctor with null. */
        BotanikFirmware(const Alarms::AlarmCondition* alarm_table, size_t alarm_count,
                        uint32_t console_baud, uint16_t cols = 80, uint16_t rows = 24)
            : BotanikFirmware(nullptr, nullptr, alarm_table, alarm_count,
                              console_baud, cols, rows) {}
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
#if BOTANIK_FEATURE_TERMINAL
            HAL::Serial::get(HAL::Serial::CONSOLE)->begin(_baud);
#endif
            HAL::Serial::get(HAL::Serial::MONITOR)->begin(_baud);

            // Core sinks: monitor + (with the UI) shell/logs pages, plus a
            // TX-drop watch on each live UART. A site adds its own in
            // on_setup().
            _logger.add_writer(&_monitor);
#if BOTANIK_FEATURE_TERMINAL
            _logger.add_writer(&_shell_page);
            _logger.add_writer(&_logs_page);
            _logger.watch_tx(&_console_serial);
#endif
            _logger.watch_tx(&_monitor_serial);

#if BOTANIK_FEATURE_TERMINAL
            // Let the shell's loglevel / alarm-ack commands reach the live
            // logger + model.
            _shell_page.set_diagnostics(&_logger, &_health.model());

            register_commands();   // site commands, tried after the built-ins
            // tab order (page 0 = boot page); switch_to(0) records it
            register_pages();
            _terminal.setup_vt102();
            _terminal.switch_to(0);
#endif

            // Details on a second line (the renderer splits on '\n') so it
            // wraps cleanly.
            _logger.info(PlcIO::BOOT, "Botanik v%s\nRunning on : %s",
                         Config::FIRMWARE_VERSION, HAL::hardware_description());

#if BOTANIK_FEATURE_MQTT
            // Route alarm transitions out through the MQTT client (it is an
            // Alarms::Publisher). Until now the notifier was a no-op.
            _health.set_alarm_publisher(&_mqtt);
#endif

            on_setup();   // site post-wiring (extra sinks / components)

            HAL::Watchdog::start(_hw_watchdog_ms);
            _logger.debug(PlcIO::BOOT, "boot complete\nconsole %lu baud, HW watchdog %lu ms",
                          (unsigned long)_baud, (unsigned long)_hw_watchdog_ms);
        }

        /** @brief One main-loop iteration. tick() gates a beat and republishes
         *  the 32-bit display "now" (`SchedulerContext::timestamp_ms`) the
         *  render path shares.
         *  The order is load-bearing (see CONCURRENCY.md):
         *    tick -> receive -> plc -> health -> on_loop -> logger -> terminal
         *         -> drain -> transmit -> watchdog_kick
         *  The health / alarm / on_loop steps take a fresh 64-bit
         *  HAL::Clock::uptime_ms(), NOT the 32-bit timestamp_ms - their fault-grace /
         *  alarm-latch timers want a wrap-free clock. Non-virtual so the order
         *  can't be reordered; a site adds per-beat work via on_loop(). */
        void loop() {
            if (!_scheduler.tick()) return;

#if BOTANIK_FEATURE_TERMINAL
            _console_serial.receive();
#endif
            _monitor_serial.receive();

#if BOTANIK_FEATURE_PLC
            _plc_handler.step();
            _plc_log_reader.step();
#endif

            // Health after the PLC payload is fresh and before the logger
            // drains, so an alarm START/END lands this beat. Asserts platform
            // alarms, updates the watchdog verdict, advances the model (remote
            // bitset zeroed without a PLC).
            uint64_t now_ms = HAL::Clock::uptime_ms();
#if BOTANIK_FEATURE_PLC
            _health.step(plc_output_data()->alarms, _plc_handler.rx_torn_count(), now_ms);
#else
            _health.step(0, 0, now_ms);
#endif

#if BOTANIK_FEATURE_NETWORK
            // Config-driven network services. On the simulator these talk over
            // loopback to an in-process mock responder (network/sim_netd.h); no
            // public network I/O.
            _network.step(now_ms);
#endif
#if BOTANIK_FEATURE_NTP
            _ntp.step(now_ms);
#endif
#if BOTANIK_FEATURE_MQTT
            _mqtt.step(now_ms);
#endif

            on_loop(now_ms);   // site per-beat state machines (default no-op)

            _logger.step();

#if BOTANIK_FEATURE_TERMINAL
            _terminal.step();
#endif
            _monitor.drain();

#if BOTANIK_FEATURE_TERMINAL
            _console_serial.transmit();
#endif
            _monitor_serial.transmit();

            // Kick while healthy; HealthMonitor withholds only once a PLC fault
            // persists past the grace window (the deliberate reboot).
            if (_health.watchdog_ok()) HAL::Watchdog::kick();
        }

        /** @brief Run loop() forever (the OPTA entry point; the simulator
         *  drives loop() itself so it can break on a shutdown signal). */
        void run() { for (;;) loop(); }

        /** @} */

        /** @name Per-target timing
         *  @{ */

        /** @brief Override the HW-watchdog period armed by setup() (call before
         *  it). */
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
        Network::NetworkHandler&        network()         { return _network; }
        Network::NameResolver&          resolver()        { return _resolver; }
#endif
#if BOTANIK_FEATURE_NTP
        Network::NtpClient&             ntp()             { return _ntp; }
#endif
#if BOTANIK_FEATURE_MQTT
        Network::MqttClient&            mqtt()            { return _mqtt; }
#endif
        Health::HealthMonitor&          health()          { return _health; }
        Alarms::AlarmModel&             alarm_model()     { return _health.model(); }
#if BOTANIK_FEATURE_TERMINAL
        Terminal::AnsiTerminal&         terminal()        { return _terminal; }
        Pages::ShellPage&               shell_page()      { return _shell_page; }
        Pages::LogsPage&                logs_page()       { return _logs_page; }
        Pages::AlarmsPage&              alarms_page()     { return _alarms_page; }
#endif

        /** @} */

    protected:
        /** @name Site customization hooks (Template Method)
         *  Overridden by a site instead of setup()/loop(), so the fixed
         *  bring-up and cycle order can never be skipped or misordered.
         *  @{ */

#if BOTANIK_FEATURE_TERMINAL
        /** @brief Register the pages in tab order (page 0 = boot page); the
         *  add_page() call order IS the tab order. Default: the built-ins. */
        virtual void register_pages() {
            _terminal.add_page(&_shell_page);
            _terminal.add_page(&_logs_page);
            _terminal.add_page(&_alarms_page);
        }

        /** @brief Register the site's shell command table (tried after the
         *  built-ins, so a site can't shadow exit/reset). Default: none. */
        virtual void register_commands() {}
#endif

        /** @brief Site post-wiring, run once near the end of setup(). Default:
         *  nothing. */
        virtual void on_setup() {}

        /** @brief Site per-beat work, run each loop() with fresh PLC data
         *  (after health, before the logger drains). Default: nothing. */
        virtual void on_loop(uint64_t now_ms) { (void)now_ms; }

        /** @} */
    };

}  // namespace Botanik

#endif // LIBBOTANIK_FIRMWARE_H
