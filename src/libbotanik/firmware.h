#ifndef LIBBOTANIK_FIRMWARE_H
#define LIBBOTANIK_FIRMWARE_H

/**
 * @file firmware.h
 * @brief BotanikFirmware<In, Out> - the generic firmware base.
 *
 * Owns every long-lived generic component (serials, logger, scheduler, PLC sync,
 * alert model/reader, terminal, core pages) and the per-tick loop() order, and
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
#include "libbotanik/alerts/alerts.h"    // AlertModel, AlertCondition, SYS_RST
#include "libbotanik/alerts/notifier.h"  // MqttAlertNotifier
#include "libbotanik/alerts/reader.h"    // AlertReader

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
        /** @brief Site alert table + length, handed to the alert model. */
        Alerts::AlertCondition*   _alert_table;
        size_t                    _alert_count;
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
        Alerts::MqttAlertNotifier _alert_mqtt;
        Alerts::AlertModel        _alert_model;
        Alerts::AlertReader<Out>  _alert_reader;
        SystemContext             _sys_ctx;
        Terminal::AnsiTerminal    _terminal;
        Pages::ShellPage          _shell_page;
        Pages::LogsPage           _logs_page;
        Pages::AlertsPage         _alerts_page;

        /** @brief Last cumulative scheduler-overrun count seen, to detect a new overrun
         *  and pulse CPU_XRUN (the model's latch_s then holds it active). */
        uint32_t                  _xrun_last  = 0;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Wire up the generic components. @p shared_in / @p shared_out are
         *  the PLC region (caller-owned); @p alert_table / @p alert_count is the
         *  site alert table; @p console_baud is the console UART baud. */
        BotanikFirmware(PlcIO::PlcInput<In>*  shared_in,
                        PlcIO::PlcOutput<Out>* shared_out,
                        Alerts::AlertCondition* alert_table, size_t alert_count,
                        uint32_t console_baud)
            : _baud(console_baud),
              _alert_table(alert_table),
              _alert_count(alert_count),
              _console_serial(HAL::get_serial(HAL::CONSOLE)),
              _monitor_serial(HAL::get_serial(HAL::MONITOR)),
              _logger(),
              _monitor(_monitor_serial),
              _scheduler(Config::TASK_PERIOD_MS, &_logger),
              _plc_handler(shared_in, shared_out, &_scheduler.context()->timestamp_ms, &_logger),
              _plc_log_reader(_plc_handler.context(), &_logger),
              _alert_mqtt("botanik/greenhouse"),
              _alert_model(_alert_table, _alert_count, &_logger, &_alert_mqtt),
              _alert_reader(&_alert_model, _plc_handler.output_data()),
              _sys_ctx(_scheduler.context(), _plc_handler.context()),
              _terminal(_console_serial, 80, 24, &_scheduler.context()->timestamp_ms,
                        _baud, _sys_ctx, &_alert_model),
              _shell_page(_terminal),
              _logs_page(_terminal),
              _alerts_page(_terminal, &_alert_model)
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
            _terminal.add_page(&_alerts_page);

            _terminal.setup_vt102();
            // Records the boot page (deferred: the screen isn't ready until the first
            // step()), so a site inserting its home page at index 0 afterwards makes
            // that the boot page.
            _terminal.switch_to(0);

            _logger.info(PlcIO::BOOT, "Botanik v%s on %s",
                         Config::FIRMWARE_VERSION, HAL::hardware_description());

            HAL::watchdog_start(_hw_watchdog_ms);
        }

        /** @brief One main-loop iteration. tick() gates a beat (returns early until
         *  the next period boundary) and republishes the loop timestamp; the steps
         *  below then share one "now". The order is load-bearing - see CONCURRENCY.md.
         *    tick -> receive -> plc -> alerts -> logger -> terminal -> drain
         *         -> transmit -> watchdog_kick */
        virtual void loop() {
            if (!_scheduler.tick()) return;

            _console_serial.receive();
            _monitor_serial.receive();

            _plc_handler.step();
            _plc_log_reader.step();

            // Analyze alerts after the PLC payload is fresh and before the logger
            // drains, so any alarm START/END lands this beat. SYS_RST is a local
            // alert, asserted for the first five minutes after boot.
            uint64_t now_ms = HAL::uptime_ms();
            _alert_model.set_local(Alerts::SYS_RST, now_ms < 300000);
            // Generic platform-health alerts from the scheduler metrics: rolling-peak
            // load / jitter over a percent of the task period, stack usage over a
            // percent, and CPU_XRUN latched for a window after the latest overrun.
            const Scheduler::SchedulerContext* sc = _scheduler.context();
            const uint32_t period_us = Config::TASK_PERIOD_MS * 1000u;
            _alert_model.set_local(Alerts::CPU_LOAD,
                (uint64_t)sc->cpu_loop_us_recent * 100u
                    > (uint64_t)period_us * Config::ALERT_CPU_LOAD_PCT);
            _alert_model.set_local(Alerts::CPU_JITT,
                (uint64_t)sc->cpu_jitter_us_recent * 100u
                    > (uint64_t)period_us * Config::ALERT_CPU_JITTER_PCT);
            _alert_model.set_local(Alerts::STACK,
                sc->stack_usage_ema * 100.0f > (float)Config::ALERT_STACK_PCT);
            // CPU_XRUN pulses on for the beat an overrun is counted; the table's latch_s
            // holds it active afterwards (like CPU_LOAD / CPU_JITT).
            bool xrun = (sc->overruns != _xrun_last);
            _xrun_last = sc->overruns;
            _alert_model.set_local(Alerts::CPU_XRUN, xrun);
            _alert_reader.step(now_ms);

            _logger.step();

            _terminal.step();
            _monitor.drain();

            _console_serial.transmit();
            _monitor_serial.transmit();

            // Only kick the watchdog while the PLC link is healthy.
            if (!_plc_handler.context()->faulted()) HAL::watchdog_kick();
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
        Alerts::AlertModel&             alert_model()     { return _alert_model; }
        Terminal::AnsiTerminal&         terminal()        { return _terminal; }
        Pages::ShellPage&               shell_page()      { return _shell_page; }
        Pages::LogsPage&                logs_page()       { return _logs_page; }
        Pages::AlertsPage&              alerts_page()     { return _alerts_page; }

        /** @} */
    };

}  // namespace Botanik

#endif // LIBBOTANIK_FIRMWARE_H
