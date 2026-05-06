#include "libbotanik.h"


namespace Botanik {

    namespace {
        using namespace Botanik::Scheduler;
        using namespace Botanik::Logs;
        using namespace Botanik::PlcIO;
        using namespace Botanik::Utils;
        using namespace Botanik::Terminal;
    }

    class BotanikApp {
    private:
        uint32_t                        _period_ms          = 100;
        uint16_t                        _terminal_cols      = 80;
        uint16_t                        _terminal_rows      = 25;

        uint32_t                        _timestamp_ms       = 0;

        Scheduler::LoopScheduler        _scheduler;

        Utils::BufferedSerial           _console_serial;
        Utils::BufferedSerial           _monitor_serial;

        Logs::Logger                    _logger;
        Logs::SerialLogWriter           _monitor;

        PlcIO::PlcSyncHandler           _plc_handler;
        PlcIO::PlcLogReader             _plc_log_reader;
        #ifdef TARGET_SIMULATOR
        PlcIO::PlcEmulator              _plc_emulator;
        #endif

        Terminal::BotanikTerminal       _terminal;


        void setup() {
            _logger = Logger();
            
            _scheduler = LoopScheduler(_period_ms);

            _console_serial = BufferedSerial(HAL::get_serial(HAL::CONSOLE));
            _monitor_serial = BufferedSerial(HAL::get_serial(HAL::MONITOR));

            _monitor = SerialLogWriter(_monitor_serial);
            _logger.add_writer(&_monitor);

            _plc_handler = PlcSyncHandler(&_timestamp_ms);
            _plc_log_reader = PlcLogReader(&_plc_handler, &_logger, &_timestamp_ms);
            #ifdef TARGET_SIMULATOR
            _plc_emulator = PlcEmulator(&_timestamp_ms);
            #endif

            // (BotanikTerminal is not written yet)
            //_terminal = BotanikTerminal();
        }

        void step() {
            if (!_scheduler.tick()) return;

            _console_serial.step();
            _monitor_serial.step();

            _plc_handler.step();
            _plc_log_reader.step();

            #ifdef TARGET_SIMULATOR
            _plc_emulator.step();
            #endif


        }
    };

}