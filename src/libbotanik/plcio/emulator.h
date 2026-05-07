#ifndef PLCIO_EMULATOR_H
#define PLCIO_EMULATOR_H

#include <cstdint>
#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/plcio/sync.h"
#include "libbotanik/plcio/vars.h"

namespace Botanik { namespace PlcIO {

    // Non-template base class (mirror of PlcSyncHandlerBase). Holds the fixed
    // log-protocol buffers from the emulator's perspective. PlcLogWriter takes a
    // PlcEmulatorBase* so it does not need to be a template itself.
    class PlcEmulatorBase {
    protected:
        bool             _tx_write       = false;
        PlcInputLogs_t   _rx_logs_buffer = {};  // emulator RX ← what host transmitted
        PlcOutputLogs_t  _tx_logs_buffer = {};  // emulator TX → what host will receive
    public:
        void                  write()        { _tx_write = true; }
        PlcOutputLogs_t*      output_logs()        { return &_tx_logs_buffer; }
        const PlcInputLogs_t* input_logs()  const  { return &_rx_logs_buffer; }
    };

    // Simulator-side mirror of PlcSyncHandler. Simulates the PLC coprocessor: reads
    // from the input range (what the host wrote) and writes to the output range (what
    // the host reads). InputData / OutputData must match those of the paired handler.
    template<typename InputData, typename OutputData>
    class PlcEmulator : public PlcEmulatorBase {
    private:
        uint32_t               _heartbeat_ms  = Config::PLC_HEARTBEAT_MS;
        uint32_t*              _timestamp_ms  = nullptr;
        PlcInput<InputData>*   _plcin;                  // PLC input memory  (read-only  for emulator)
        PlcOutput<OutputData>* _plcout;                 // PLC output memory (write-only for emulator)
        InputData              _rx_data_buffer;         // local receive buffer for _plcin->data
        OutputData             _tx_data_buffer;         // local transmit buffer for _plcout->data
        PlcRxState             _rx_state      = RX_IDLE;
        PlcTxState             _tx_state      = TX_IDLE;
        uint32_t               _tx_last_ms    = 0;
        bool                   _ever_ackd     = false;

        // Transition triggers
        inline bool _rx_new_message()       { return _plcin->sync.rxSeqNum != _plcout->sync.rxAckNum; }
        inline bool _tx_heartbeat_elapsed() { return *_timestamp_ms - _tx_last_ms > _heartbeat_ms; }
        inline bool _tx_acknowledged() {
            bool result = _plcin->sync.txAckNum == _plcout->sync.txSeqNum;
            if (result) { _tx_last_ms = *_timestamp_ms; _ever_ackd = true; }
            return result;
        }

        // Transition actions
        inline void _rx_memcpy() {
            HAL::ordered_copy(&_rx_logs_buffer, &_plcin->logs);
            HAL::ordered_copy(&_rx_data_buffer, &_plcin->data);
        }
        inline void _tx_memcpy() {
            HAL::ordered_copy(&_plcout->logs, &_tx_logs_buffer);
            HAL::ordered_copy(&_plcout->data, &_tx_data_buffer);
            _tx_write = false;
        }
        inline void _rx_acknowledge() {
            uint32_t seq = _plcin->sync.rxSeqNum;
            HAL::ordered_copy(&_plcout->sync.rxAckNum, &seq);
        }
        inline void _tx_commit() {
            uint32_t next_seq = _plcout->sync.txSeqNum + 1;
            HAL::ordered_copy(&_plcout->sync.txSeqNum, &next_seq);
        }

        PlcRxState _rx_step() {
            switch (_rx_state) {
                case RX_IDLE: if (_rx_new_message())        return RX_READ; break;
                case RX_READ: _rx_memcpy();                 return RX_ACK;  break;
                case RX_ACK:  _rx_acknowledge();            return RX_IDLE; break;
            }
            return _rx_state;
        }
        PlcTxState _tx_step() {
            switch (_tx_state) {
                case TX_IDLE:   if (_tx_write)              return TX_WRITE;
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
            : _timestamp_ms(timestamp_ms), _plcin(plcin), _plcout(plcout)
        {
            memset(&_rx_data_buffer, 0, sizeof(InputData));
            memset(&_tx_data_buffer, 0, sizeof(OutputData));
            _tx_last_ms = *timestamp_ms;
        }

        OutputData*        output_data()       { return &_tx_data_buffer; }
        const InputData*   input_data()  const { return &_rx_data_buffer; }

        PlcStatus status() const {
            uint32_t elapsed = *_timestamp_ms - _tx_last_ms;
            if (!_ever_ackd)                        return PLC_UNKNOWN;
            if (elapsed > Config::PLC_WATCHDOG_MS)  return PLC_FAULT;
            if (elapsed > Config::PLC_TIMEOUT_MS)   return PLC_TIMEOUT;
                                                    return PLC_RUNNING;
        }

        void step() {
            _rx_state = _rx_step();
            _tx_state = _tx_step();
        }
    };

    // Pushes log entries into the emulator's output log buffer, simulating the PLC
    // coprocessor sending logs to the host. Takes a PlcEmulatorBase* so it does not
    // need to be a template.
    class PlcLogWriter {
    private:
                PlcEmulatorBase* _emulator;
        const   uint32_t*        _timestamp_ms;
                uint32_t         _next_seq    = 1;
                LogEntry_t*      _logBuffer;
                uint32_t*        _writeIndex;
        const   uint32_t*        _readIndex;
    public:
        PlcLogWriter(PlcEmulatorBase* emulator, uint32_t* timestamp_ms);
        void push_log(LogLevel level, LogTopic topic, const char* msg);
    };

}}

#endif // PLCIO_EMULATOR_H
