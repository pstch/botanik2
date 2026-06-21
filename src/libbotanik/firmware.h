#ifndef LIBBOTANIK_FIRMWARE_H
#define LIBBOTANIK_FIRMWARE_H

/**
 * @file firmware.h
 * @brief BotanikFirmware<In, Out> - the generic firmware base.
 *
 * Owns every long-lived generic component (serials, logger, scheduler, PLC sync,
 * alarm model/reader, terminal, core pages) and the per-tick loop() order, and
 * exposes a reference accessor for each so a site subclass can build its own
 * components on top. A site derives `GreenhouseXXX : BotanikFirmware<In, Out>`,
 * adds its pages as members, and customizes registration through the register_*()
 * hooks; see docs and CLAUDE.md ("Site-specific code").
 *
 * Target-independent: the shared PLC region is injected by the caller (the
 * simulator owns it as local statics; on OPTA it comes from
 * HAL::get_plc_input()/get_plc_output()). Per-target timing (link-timeout
 * dilation on the sim, the HW watchdog period) is applied by the harness through
 * the accessors / set_hw_watchdog_ms() before setup().
 *
 * PLC-optional: the PLC sync handler, log reader, shared-memory payload, and the
 * PLC-fault watchdog escalation are compiled in only when BOTANIK_FEATURE_PLC is
 * set (the default); see libbotanik_features.h. A firmware with no coprocessor
 * builds with -DBOTANIK_FEATURE_PLC=OFF and derives BotanikFirmware<> - the
 * payload template parameters default to a zero-size Empty type, and the loop
 * feeds the health step a zeroed remote bitset, so only the local platform-health
 * alarms fire and HealthMonitor runs with a null link context.
 *
 * @section firmware_extend Extending the base for a site
 *
 * A site is a thin subclass that contributes data (its PLC payload, alarm table,
 * pages, shell commands) and wires it through the base. The base owns the generic
 * machinery and the bring-up/loop order; the site never overrides setup()/loop(),
 * only the protected Template-Method hooks, so the order can't be skipped or
 * misordered. The extension surface is:
 *
 *  - **Alarm table** - passed to the constructor (`alarm_table`, `alarm_count`).
 *    The base hands it to the AlarmModel, which splices in the generic
 *    platform-health alarms (alarms/platform.h). Site/PLC alarms fill bit 0 up;
 *    platform alarms sit at the high end. Pure data; no hook needed.
 *  - **Pages** - override register_pages() to set the tab order (page 0 is the
 *    boot page); call terminal().add_page(...) for each. The default registers the
 *    built-ins (shell/logs/alarms).
 *  - **Shell commands** - override register_commands() and call
 *    shell_page().set_commands(table, count, ctx) with a site-static ShellCommand
 *    table. Tried after the built-ins, so a site can't shadow exit/reset.
 *  - **Post-wiring / per-beat work** - on_setup() runs once near the end of setup()
 *    (e.g. to add a log sink); on_loop(now_ms) runs each beat with fresh PLC data.
 *
 * Worked example (a site with one extra page and one extra command):
 * @code
 * // --- site-static tables (single source of truth for this greenhouse) ---
 * extern const Alarms::AlarmCondition g_alarms[];   // alarms/<site>.cpp
 * extern const uint8_t                g_alarm_count;
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

#include <cstddef>
#include <cstdint>

namespace Botanik {

    /** @brief Zero-size default PLC payload. A firmware built without PLC
     *  (BOTANIK_FEATURE_PLC=0) never names a real payload type, so the template
     *  parameters default to this and a PLC-less site derives BotanikFirmware<>. */
    struct Empty {};

    /** @brief Generic firmware base: owns the generic components + the loop order,
     *  and exposes each component for a site subclass to build on. The PLC pieces
     *  (handler, log reader, payload threading) are compiled in only when
     *  BOTANIK_FEATURE_PLC is set; see libbotanik_features.h. */
    template<typename In = Empty, typename Out = Empty>
    class BotanikFirmware {
        /** @name Configuration
         *  @{ */

        /** @brief Console/monitor UART baud (also sizes the terminal's TX gates). */
        uint32_t                  _baud;
        /** @brief Site PLC alarm table + length, handed to the alarm model (copied in). */
        const Alarms::AlarmCondition* _alarm_table;
        size_t                    _alarm_count;
        /** @brief Hardware-watchdog period armed in setup(); the harness overrides
         *  it (the simulator dilates it) via set_hw_watchdog_ms() before setup(). */
        uint32_t                  _hw_watchdog_ms = Config::HW_WATCHDOG_MS;

        /** @} */

        /** @name Generic components (declared in construction order)
         *  @{ */

        Utils::BufferedSerial     _console_serial;
        Utils::BufferedSerial     _monitor_serial;
        Logs::Logger              _logger;
        Logs::MonitorLogWriter    _monitor;
        Scheduler::LoopScheduler  _scheduler;
#if BOTANIK_FEATURE_PLC
        PlcIO::PlcSyncHandler<In, Out> _plc_handler;
        PlcIO::PlcLogReader       _plc_log_reader;
#endif
        Health::HealthMonitor     _health;
        SystemContext             _sys_ctx;
        Terminal::AnsiTerminal    _terminal;
        Pages::ShellPage          _shell_page;
        Pages::LogsPage           _logs_page;
        Pages::AlarmsPage         _alarms_page;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Wire up the generic components. @p shared_in / @p shared_out are
         *  the PLC region (caller-owned); @p alarm_table / @p alarm_count is the
         *  site alarm table; @p console_baud is the console UART baud; @p cols /
         *  @p rows is the terminal geometry (entry-point-supplied, like the baud). */
        BotanikFirmware(PlcIO::PlcInput<In>*  shared_in,
                        PlcIO::PlcOutput<Out>* shared_out,
                        const Alarms::AlarmCondition* alarm_table, size_t alarm_count,
                        uint32_t console_baud, uint16_t cols = 80, uint16_t rows = 24)
            : _baud(console_baud),
              _alarm_table(alarm_table),
              _alarm_count(alarm_count),
              _console_serial(HAL::get_serial(HAL::CONSOLE)),
              _monitor_serial(HAL::get_serial(HAL::MONITOR)),
              _logger(),
              _monitor(_monitor_serial),
              _scheduler(Config::TASK_PERIOD_MS, &_logger),
#if BOTANIK_FEATURE_PLC
              _plc_handler(shared_in, shared_out, &_scheduler.context()->timestamp_ms, &_logger),
              _plc_log_reader(_plc_handler.context(), &_logger),
              _health(_alarm_table, (uint8_t)_alarm_count, &_logger,
                      _scheduler.context(), _plc_handler.context(),
                      shared_in, shared_out),
              _sys_ctx(_scheduler.context(), _plc_handler.context()),
#else
              // No PLC: the health monitor takes a null link context + null regions
              // (it already degrades the watchdog verdict to "kick while alive"), and
              // the system context carries no PLC status (SystemContext.plc stays null).
              _health(_alarm_table, (uint8_t)_alarm_count, &_logger,
                      _scheduler.context(), nullptr, nullptr, nullptr),
              _sys_ctx(_scheduler.context()),
#endif
              _terminal(_console_serial, cols, rows, &_scheduler.context()->timestamp_ms,
                        _baud, _sys_ctx, &_health.model()),
              _shell_page(_terminal),
              _logs_page(_terminal),
              _alarms_page(_terminal, &_health.model())
        {
#if !BOTANIK_FEATURE_PLC
            (void)shared_in; (void)shared_out;   // unused without the PLC link
#endif
        }

#if !BOTANIK_FEATURE_PLC
        /** @brief PLC-less convenience ctor: a single-core firmware has no shared
         *  region, so it omits the region pointers entirely. Delegates to the main
         *  ctor with null regions (the only place they'd be used is gated out). */
        BotanikFirmware(const Alarms::AlarmCondition* alarm_table, size_t alarm_count,
                        uint32_t console_baud, uint16_t cols = 80, uint16_t rows = 24)
            : BotanikFirmware(nullptr, nullptr, alarm_table, alarm_count,
                              console_baud, cols, rows) {}
#endif

        virtual ~BotanikFirmware() {}

        /** @brief Bring the firmware up in a fixed, non-overridable order (so a site
         *  can't skip or misorder the bring-up): begin the UARTs, register the core
         *  sinks, register the pages via register_pages(), select the terminal type,
         *  show the boot banner, and arm the hardware watchdog. (The boot self-test,
         *  POST, runs itself on HealthMonitor's first step() once loop() starts.)
         *  A site customizes through the protected register_pages() / on_setup() hooks,
         *  never by overriding setup(). */
        void setup() {
            set_assert_logger(&_logger);   // route BOTANIK_ASSERT failures to the log
            HAL::get_serial(HAL::CONSOLE)->begin(_baud);
            HAL::get_serial(HAL::MONITOR)->begin(_baud);

            // Core log sinks: diagnostic monitor + the shell/logs pages, plus the
            // console TX-drop watch. A site adds its own sink(s) in on_setup().
            _logger.add_writer(&_monitor);
            _logger.add_writer(&_shell_page);
            _logger.add_writer(&_logs_page);
            _logger.watch_tx(&_console_serial);
            _logger.watch_tx(&_monitor_serial);

            // Let the shell's loglevel / alarm-ack commands reach the live logger + model.
            _shell_page.set_diagnostics(&_logger, &_health.model());

            // Site shell commands (tried after the built-ins, so a site can't shadow
            // exit/reset). The default hook registers none.
            register_commands();

            // Pages, in tab order: the hook decides the set/order (page 0 is the boot
            // page). The default registers the built-ins; a site overrides the hook to
            // interleave its own (or drop a built-in). switch_to(0) records the boot page.
            register_pages();
            _terminal.setup_vt102();
            _terminal.switch_to(0);

            // Details on a second line (the renderer splits on '\n') so it wraps cleanly.
            _logger.info(PlcIO::BOOT, "Botanik v%s\nRunning on : %s",
                         Config::FIRMWARE_VERSION, HAL::hardware_description());

            on_setup();   // site post-wiring (extra sinks / components)

            // The boot self-test (POST) now runs itself on HealthMonitor's first step()
            // in loop(), by which point every sink is registered so its verdict is logged.

            HAL::watchdog_start(_hw_watchdog_ms);
            // Details on a second line (the renderer splits on '\n') so the entry wraps cleanly.
            _logger.debug(PlcIO::BOOT, "boot complete\nconsole %lu baud, HW watchdog %lu ms",
                          (unsigned long)_baud, (unsigned long)_hw_watchdog_ms);
        }

        /** @brief One main-loop iteration. tick() gates a beat (returns early until
         *  the next period boundary) and republishes the 32-bit display "now"
         *  (`SchedulerContext::timestamp_ms`) that the render path (terminal, pages)
         *  shares. The order is load-bearing - see CONCURRENCY.md.
         *    tick -> receive -> plc -> health -> on_loop -> logger -> terminal
         *         -> drain -> transmit -> watchdog_kick
         *  Time base: the health / alarm / on_loop steps take a fresh 64-bit
         *  HAL::uptime_ms() (below), NOT the 32-bit timestamp_ms - their fault-grace
         *  and alarm-latch timers want a wrap-free monotonic clock; the few microseconds
         *  between tick() and this read are immaterial at their second scale.
         *  Non-virtual so the order can't be reordered; a site adds per-beat work
         *  through the on_loop() hook (run with the freshly-read PLC data). */
        void loop() {
            if (!_scheduler.tick()) return;

            _console_serial.receive();
            _monitor_serial.receive();

#if BOTANIK_FEATURE_PLC
            _plc_handler.step();
            _plc_log_reader.step();
#endif

            // Health step after the PLC payload is fresh and before the logger drains,
            // so any alarm START/END lands this beat. HealthMonitor asserts the
            // platform-health alarms, updates the watchdog verdict, and advances the
            // alarm model with the freshly-read remote bitset (zeroed without a PLC,
            // so only the local platform-health alarms fire).
            uint64_t now_ms = HAL::uptime_ms();
#if BOTANIK_FEATURE_PLC
            _health.step(plc_output_data()->alarms, _plc_handler.rx_torn_count(), now_ms);
#else
            _health.step(0, 0, now_ms);
#endif

            on_loop(now_ms);   // site per-beat state machines (default no-op)

            _logger.step();

            _terminal.step();
            _monitor.drain();

            _console_serial.transmit();
            _monitor_serial.transmit();

            // Kick the watchdog while we're healthy; HealthMonitor withholds it only
            // once a PLC fault persists past the grace window (the deliberate reboot).
            if (_health.watchdog_ok()) HAL::watchdog_kick();
        }

        /** @brief Run loop() forever (the OPTA entry point; the simulator drives
         *  loop() itself so it can break on a shutdown signal). */
        void run() { for (;;) loop(); }

        /** @} */

        /** @name Per-target timing
         *  @{ */

        /** @brief Override the HW-watchdog period armed by setup() (call before it). */
        void set_hw_watchdog_ms(uint32_t ms) { _hw_watchdog_ms = ms; }

        /** @} */

#if BOTANIK_FEATURE_PLC
        /** @name HAL region binding (hardware targets)
         *  @{ */

        /** @brief The coprocessor's PLC input/output region from HAL, cast to the
         *  typed PlcInput/PlcOutput<> this firmware expects. This is the one place the
         *  cast from HAL's opaque void* lives, so a site convenience constructor (and
         *  any integrator sketch) never spells it out. On the simulator HAL returns
         *  null - the sim binds its own mock region through the explicit constructor. */
        static PlcIO::PlcInput<In>*   hal_input()  { return static_cast<PlcIO::PlcInput<In>*>(HAL::get_plc_input()); }
        static PlcIO::PlcOutput<Out>* hal_output() { return static_cast<PlcIO::PlcOutput<Out>*>(HAL::get_plc_output()); }

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
        /** @brief No PLC link: there is no shared payload, so this is null. Site
         *  pages/commands that read it already null-check (Home/Status show "n/a"). */
        const Out*                      plc_output_data() { return nullptr; }
#endif
        Health::HealthMonitor&          health()          { return _health; }
        Alarms::AlarmModel&             alarm_model()     { return _health.model(); }
        Terminal::AnsiTerminal&         terminal()        { return _terminal; }
        Pages::ShellPage&               shell_page()      { return _shell_page; }
        Pages::LogsPage&                logs_page()       { return _logs_page; }
        Pages::AlarmsPage&              alarms_page()     { return _alarms_page; }

        /** @} */

    protected:
        /** @name Site customization hooks (Template Method)
         *  Overridden by a site instead of setup()/loop(), so the fixed bring-up and
         *  cycle order can never be skipped or misordered.
         *  @{ */

        /** @brief Register the pages in tab order (page 0 is the boot page). The default
         *  registers the built-ins; a site overrides this to interleave its own pages or
         *  drop a built-in - the order of add_page() calls IS the tab order. */
        virtual void register_pages() {
            _terminal.add_page(&_shell_page);
            _terminal.add_page(&_logs_page);
            _terminal.add_page(&_alarms_page);
        }

        /** @brief Register the site's shell command table on the shell page, tried
         *  after the built-ins (so a site can't shadow exit/reset). Default: none.
         *  Override and call shell_page().set_commands(table, count, ctx) with a
         *  site-static ShellCommand table; see the extension example in the file header. */
        virtual void register_commands() {}

        /** @brief Site post-wiring, run once near the end of setup() (after the core
         *  sinks/pages are registered). Default: nothing. */
        virtual void on_setup() {}

        /** @brief Site per-beat work, run each loop() with the freshly-read PLC data
         *  (after the health step, before the logger drains). Default: nothing. */
        virtual void on_loop(uint64_t now_ms) { (void)now_ms; }

        /** @} */
    };

}  // namespace Botanik

#endif // LIBBOTANIK_FIRMWARE_H
