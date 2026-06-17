/**
 * @file firmware.cpp
 * @brief BotanikFirmware construction (the setup() work) and the per-tick step().
 */

#include "botanik_fw/firmware.h"

namespace Botanik { namespace Firmware {

// USB-CDC / UART console baud. The diagnostic monitor (Serial1) runs at the same
// rate. Plain constant for now; promote to libbotanik_config.h if a site needs it.
static const unsigned long CONSOLE_BAUD = 115200;

BotanikFirmware::BotanikFirmware()
    : _console_serial(HAL::get_serial(HAL::CONSOLE)),
      _monitor_serial(HAL::get_serial(HAL::MONITOR)),
      _logger(),
      _monitor(_monitor_serial),
      _scheduler(Config::TASK_PERIOD_MS, &_logger),
      _shared_in (static_cast<PlcIO::PlcInput<PlcInputData_t>*  >(HAL::get_plc_input())),
      _shared_out(static_cast<PlcIO::PlcOutput<PlcOutputData_t>*>(HAL::get_plc_output())),
      _plc_handler(_shared_in, _shared_out, &_scheduler.context()->timestamp_ms, &_logger),
      _plc_log_reader(_plc_handler.context(), &_logger),
      _sys_ctx(_scheduler.context(), _plc_handler.context()),
      _terminal(_console_serial, 80, 24, &_scheduler.context()->timestamp_ms,
                (uint32_t)CONSOLE_BAUD, _sys_ctx),
      _shell_page(_terminal),
      _logs_page(_terminal),
      _home_page(_terminal, _plc_handler.output_data())
{
    // Bring up the UARTs: console drives the terminal UI, monitor the diagnostic log.
    HAL::get_serial(HAL::CONSOLE)->begin(CONSOLE_BAUD);
    HAL::get_serial(HAL::MONITOR)->begin(CONSOLE_BAUD);

    // Logging sinks: the diagnostic monitor plus the page writers (Shell/Logs/Home
    // each surface the log stream in their own view).
    _logger.add_writer(&_monitor);
    _logger.add_writer(&_shell_page);
    _logger.add_writer(&_logs_page);
    _logger.add_writer(&_home_page);

    // Register pages, pick the terminal type, and show the first one.
    _terminal.add_page(&_home_page);
    _terminal.add_page(&_shell_page);
    _terminal.add_page(&_logs_page);
    _terminal.setup_vt102();
    _terminal.switch_to(0);

    _logger.info(PlcIO::BOOT, "Botanik v%s on %s",
                 Config::FIRMWARE_VERSION, HAL::hardware_description());

    // Arm the hardware watchdog; the loop kicks it every healthy tick and stops
    // once the PLC link faults, so the board reboots HW_WATCHDOG_MS later.
    HAL::watchdog_start(Config::HW_WATCHDOG_MS);
}

void BotanikFirmware::step() {
    // tick() opens the work window and republishes the loop timestamp; bail until
    // the next period boundary so the steps below share a single "now".
    if (!_scheduler.tick()) return;

    _console_serial.receive();
    _monitor_serial.receive();

    _plc_handler.step();
    _plc_log_reader.step();

    _logger.step();

    _terminal.step();
    _monitor.drain();

    _console_serial.transmit();
    _monitor_serial.transmit();

    // Only kick the watchdog while the PLC link is healthy.
    if (!_plc_handler.context()->faulted()) HAL::watchdog_kick();
}

}}  // namespace Botanik::Firmware
