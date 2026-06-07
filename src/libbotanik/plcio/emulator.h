#ifndef PLCIO_EMULATOR_H
#define PLCIO_EMULATOR_H

/**
 * @file emulator.h
 * @brief PlcEmulator — simulator-side stand-in that drives the shared-memory
 *        protocol from the PLC's side, mirroring PlcSyncHandler.
 */

#include <cstdint>
#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/plcio/sync.h"

#ifdef TARGET_SIMULATOR
#include <cstdio>
#include <unistd.h>
#include <pthread.h>
#endif

namespace Botanik { namespace PlcIO {

    // Non-template context object (mirror of PlcSyncContext) for the emulator side.
    struct PlcEmulatorContext {
        template<typename, typename> friend class PlcEmulator;
    private:
        bool             _tx_write       = false;
        PlcInputLogs_t   _rx_logs_buffer = {};  // emulator RX ← what host transmitted
        PlcOutputLogs_t  _tx_logs_buffer = {};  // emulator TX → what host will receive
        uint32_t*        _timestamp_ms   = nullptr;
        uint32_t         _tx_last_ms     = 0;
        bool             _ever_ackd      = false;
    public:
        void                  write()              { _tx_write = true; }
        PlcOutputLogs_t*      output_logs()        { return &_tx_logs_buffer; }
        const PlcInputLogs_t* input_logs() const   { return &_rx_logs_buffer; }

        PlcStatus status() const {
            uint32_t elapsed = *_timestamp_ms - _tx_last_ms;
            if (!_ever_ackd)                        return PLC_UNKNOWN;
            if (elapsed > Config::PLC_WATCHDOG_MS)  return PLC_FAULT;
            if (elapsed > Config::PLC_TIMEOUT_MS)   return PLC_TIMEOUT;
                                                    return PLC_RUNNING;
        }
    };

    // Simulator-side mirror of PlcSyncHandler. Simulates the PLC coprocessor: reads
    // from the input range (what the host wrote) and writes to the output range (what
    // the host reads). InputData / OutputData must match those of the paired handler.
    template<typename InputData, typename OutputData>
    class PlcEmulator {
    private:
        PlcEmulatorContext     _ctx;
        uint32_t               _heartbeat_ms  = Config::PLC_HEARTBEAT_MS;
        PlcInput<InputData>*   _plcin;                  // PLC input memory  (read-only  for emulator)
        PlcOutput<OutputData>* _plcout;                 // PLC output memory (write-only for emulator)
        InputData              _rx_data_buffer;         // local receive buffer for _plcin->data
        OutputData             _tx_data_buffer;         // local transmit buffer for _plcout->data
        PlcRxState             _rx_state      = RX_IDLE;
        PlcTxState             _tx_state      = TX_IDLE;

        // Transition triggers
        inline bool _rx_new_message()       { return _plcin->sync.rxSeqNum != _plcout->sync.rxAckNum; }
        inline bool _tx_heartbeat_elapsed() { return *_ctx._timestamp_ms - _ctx._tx_last_ms > _heartbeat_ms; }
        inline bool _tx_acknowledged() {
            bool result = _plcin->sync.txAckNum == _plcout->sync.txSeqNum;
            if (result) { _ctx._tx_last_ms = *_ctx._timestamp_ms; _ctx._ever_ackd = true; }
            return result;
        }

        // Transition actions
        inline void _rx_memcpy() {
            HAL::ordered_copy(&_ctx._rx_logs_buffer, &_plcin->logs);
            HAL::ordered_copy(&_rx_data_buffer, &_plcin->data);
        }
        inline void _tx_memcpy() {
            HAL::ordered_copy(&_plcout->logs, &_ctx._tx_logs_buffer);
            HAL::ordered_copy(&_plcout->data, &_tx_data_buffer);
            _ctx._tx_write = false;
        }
        inline void _rx_acknowledge() {
            uint32_t seq = _plcin->sync.rxSeqNum;
            HAL::ordered_copy(&_plcout->sync.rxAckNum, &seq);
        }
        inline void _tx_commit() {
            uint32_t next_seq = _plcout->sync.txSeqNum + 1;
            HAL::ordered_copy(&_plcout->sync.txSeqNum, &next_seq);
        }

        inline PlcRxState _rx_step() {
            switch (_rx_state) {
                case RX_IDLE: if (_rx_new_message())        return RX_READ; break;
                case RX_READ: _rx_memcpy();                 return RX_ACK;  break;
                case RX_ACK:  _rx_acknowledge();            return RX_IDLE; break;
            }
            return _rx_state;
        }
        inline PlcTxState _tx_step() {
            switch (_tx_state) {
                case TX_IDLE:   if (_ctx._tx_write)         return TX_WRITE;
                        else if (_tx_heartbeat_elapsed())   return TX_COMMIT;   break;
                case TX_WRITE:  _tx_memcpy();               return TX_COMMIT;   break;
                case TX_COMMIT: _tx_commit();               return TX_WAIT;     break;
                case TX_WAIT:   if (_tx_acknowledged())     return TX_IDLE;     break;
            }
            return _tx_state;
        }

    public:
        PlcEmulator(PlcInput<InputData>* plcin, PlcOutput<OutputData>* plcout,
                    uint32_t* timestamp_ms)
            : _plcin(plcin), _plcout(plcout)
        {
            _ctx._timestamp_ms = timestamp_ms;
            _ctx._tx_last_ms   = *timestamp_ms;
            memset(&_rx_data_buffer, 0, sizeof(InputData));
            memset(&_tx_data_buffer, 0, sizeof(OutputData));
        }

        PlcEmulatorContext*  context()           { return &_ctx; }
        OutputData*          output_data()       { return &_tx_data_buffer; }
        const InputData*     input_data() const  { return &_rx_data_buffer; }

        inline void step() {
            _rx_state = _rx_step();
            _tx_state = _tx_step();
        }
    };

    // Pushes log entries into the emulator's output log buffer, simulating the PLC
    // coprocessor sending logs to the host.
    class PlcLogWriter {
    private:
        PlcEmulatorContext* _ctx;
        const uint32_t*     _timestamp_ms;
        uint32_t            _next_seq   = 1;
        LogEntry_t*         _logBuffer;
        uint32_t*           _writeIndex;
    public:
        PlcLogWriter(PlcEmulatorContext* ctx, uint32_t* timestamp_ms);
        void push_log(LogLevel level, LogTopic topic, const char* msg);
    };

#ifdef TARGET_SIMULATOR

    // Host-side driver that runs a PlcEmulator on its own POSIX thread, simulating
    // the PLC coprocessor executing independently of the main loop. Simulator-only:
    // on OPTA the real coprocessor plays this role, so this section is compiled out.

    template<typename InputData, typename OutputData>
    struct _EmulatorThreadArgs {
        PlcInput<InputData>*   shared_in;
        PlcOutput<OutputData>* shared_out;
    };

    /** @brief Thread entry point: owns the emulator's timestamp, PlcEmulator and
     *  PlcLogWriter, steps them every TASK_PERIOD_MS, and emits a heartbeat log. */
    template<typename InputData, typename OutputData>
    inline void* _emulator_thread_main(void* arg) {
        auto* args = static_cast<_EmulatorThreadArgs<InputData, OutputData>*>(arg);

        uint32_t emu_timestamp_ms = 0;
        PlcEmulator<InputData, OutputData> plc_emulator(
            args->shared_in, args->shared_out, &emu_timestamp_ms);
        PlcLogWriter plc_log_writer(plc_emulator.context(), &emu_timestamp_ms);

        uint32_t emu_tick = 0;
        while (true) {
            emu_timestamp_ms = (uint32_t)HAL::uptime_ms();
            plc_emulator.step();
            // Emit a periodic heartbeat log every 30s, simulating the PLC coprocessor.
            constexpr uint32_t HEARTBEAT_TICKS = 30000 / Config::TASK_PERIOD_MS;
            if ((++emu_tick % HEARTBEAT_TICKS) == 0) {
                char buf[40];
                snprintf(buf, sizeof(buf), "periodic log #%lu",
                         (unsigned long)(emu_tick / HEARTBEAT_TICKS));
                plc_log_writer.push_log(LOG_INFO, PLC_IO, buf);
            }
            usleep(Config::TASK_PERIOD_MS * 1000);
        }
        return nullptr;
    }

    /** @brief Spawn the detached emulator thread against the shared PLC memory.
     *  The argument block is a static with process lifetime, so it stays valid
     *  for the detached thread without any heap allocation. */
    template<typename InputData, typename OutputData>
    inline void start_emulator_thread(PlcInput<InputData>* shared_in,
                                      PlcOutput<OutputData>* shared_out) {
        static _EmulatorThreadArgs<InputData, OutputData> args{ shared_in, shared_out };
        pthread_t thread;
        pthread_create(&thread, nullptr, &_emulator_thread_main<InputData, OutputData>, &args);
        pthread_detach(thread);
    }

#endif // TARGET_SIMULATOR

}}

#endif // PLCIO_EMULATOR_H
