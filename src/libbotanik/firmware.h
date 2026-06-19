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
 */

#include "libbotanik.h"
#include "libbotanik_asserts.h"          // set_assert_logger
#include "libbotanik/alarms/alarms.h"    // AlarmCondition (site table type)
#include "libbotanik/health/health.h"    // HealthMonitor (owns the alarm model + notifier)

#include <cstddef>
#include <cstdint>

namespace Botanik {

    /** @brief Generic firmware base: owns the generic components + the loop order,
     *  and exposes each component for a site subclass to build on. */
    template<typename In, typename Out>
    class BotanikFirmware {
        /** @name Configuration
         *  @{ */

        /** @brief Console/monitor UART baud (also sizes the terminal's TX gates). */
        uint32_t                  _baud;
        /** @brief Site PLC alarm table + length, handed to the alarm model (copied in). */
        const Alarms::AlarmCondition* _alarm_table;
        size_t                    _alarm_count;
        /** @brief PLC shared region pointers, kept for the boot self-test (POST). */
        const void*               _shared_in;
        const void*               _shared_out;
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
        PlcIO::PlcSyncHandler<In, Out> _plc_handler;
        PlcIO::PlcLogReader       _plc_log_reader;
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
              _shared_in(shared_in),
              _shared_out(shared_out),
              _console_serial(HAL::get_serial(HAL::CONSOLE)),
              _monitor_serial(HAL::get_serial(HAL::MONITOR)),
              _logger(),
              _monitor(_monitor_serial),
              _scheduler(Config::TASK_PERIOD_MS, &_logger),
              _plc_handler(shared_in, shared_out, &_scheduler.context()->timestamp_ms, &_logger),
              _plc_log_reader(_plc_handler.context(), &_logger),
              _health(_alarm_table, (uint8_t)_alarm_count, &_logger,
                      _scheduler.context(), _plc_handler.context()),
              _sys_ctx(_scheduler.context(), _plc_handler.context()),
              _terminal(_console_serial, cols, rows, &_scheduler.context()->timestamp_ms,
                        _baud, _sys_ctx, &_health.model()),
              _shell_page(_terminal),
              _logs_page(_terminal),
              _alarms_page(_terminal, &_health.model())
        {}

        virtual ~BotanikFirmware() {}

        /** @brief Bring the firmware up: begin the UARTs, register the core sinks and
         *  pages, select the terminal type, show the boot page + banner, and arm the
         *  hardware watchdog. A site overrides this, calls the base first, then adds
         *  its own sink pages and splices its pages in via terminal().add_page(p, i). */
        virtual void setup() {
            set_assert_logger(&_logger);   // route BOTANIK_ASSERT failures to the log
            HAL::get_serial(HAL::CONSOLE)->begin(_baud);
            HAL::get_serial(HAL::MONITOR)->begin(_baud);

            // Core log sinks: diagnostic monitor + the shell/logs pages, plus the
            // console TX-drop watch. A site adds its own sink pages in its override.
            _logger.add_writer(&_monitor);
            _logger.add_writer(&_shell_page);
            _logger.add_writer(&_logs_page);
            _logger.watch_tx(&_console_serial);

            // Core pages, appended in tab order. A site splices its own in by index
            // (add_page(p, i)) from its setup() override - e.g. its home page at 0.
            _terminal.add_page(&_shell_page);
            _terminal.add_page(&_logs_page);
            _terminal.add_page(&_alarms_page);

            _terminal.setup_vt102();
            // Records the boot page (deferred: the screen isn't ready until the first
            // step()), so a site inserting its home page at index 0 afterwards makes
            // that the boot page.
            _terminal.switch_to(0);

            _logger.info(PlcIO::BOOT, "Botanik v%s on %s",
                         Config::FIRMWARE_VERSION, HAL::hardware_description());

            // Boot self-test (after the sinks register, so its result is logged).
            _health.post(_shared_in, _shared_out);

            HAL::watchdog_start(_hw_watchdog_ms);
        }

        /** @brief One main-loop iteration. tick() gates a beat (returns early until
         *  the next period boundary) and republishes the loop timestamp; the steps
         *  below then share one "now". The order is load-bearing - see CONCURRENCY.md.
         *    tick -> receive -> plc -> health -> logger -> terminal -> drain
         *         -> transmit -> watchdog_kick */
        virtual void loop() {
            if (!_scheduler.tick()) return;

            _console_serial.receive();
            _monitor_serial.receive();

            _plc_handler.step();
            _plc_log_reader.step();

            // Health step after the PLC payload is fresh and before the logger drains,
            // so any alarm START/END lands this beat. HealthMonitor asserts the
            // platform-health alarms, updates the watchdog verdict, and advances the
            // alarm model with the freshly-read remote bitset.
            uint64_t now_ms = HAL::uptime_ms();
            _health.step(plc_output_data()->alarms, _plc_handler.rx_torn_count(), now_ms);

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

        /** @name Component access (for the site subclass / harness)
         *  @{ */

        Logs::Logger&                   logger()          { return _logger; }
        Scheduler::LoopScheduler&       scheduler()       { return _scheduler; }
        PlcIO::PlcSyncHandler<In, Out>& plc_handler()     { return _plc_handler; }
        const Out*                      plc_output_data() { return _plc_handler.output_data(); }
        Health::HealthMonitor&          health()          { return _health; }
        Alarms::AlarmModel&             alarm_model()     { return _health.model(); }
        Terminal::AnsiTerminal&         terminal()        { return _terminal; }
        Pages::ShellPage&               shell_page()      { return _shell_page; }
        Pages::LogsPage&                logs_page()       { return _logs_page; }
        Pages::AlarmsPage&              alarms_page()     { return _alarms_page; }

        /** @} */
    };

}  // namespace Botanik

#endif // LIBBOTANIK_FIRMWARE_H
