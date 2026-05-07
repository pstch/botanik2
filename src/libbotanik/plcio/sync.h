#ifndef PLCIO_SYNC_H
#define PLCIO_SYNC_H

#include <cstdint>
#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/plcio/vars.h"

namespace Botanik { namespace PlcIO {

    // PLC connection status
    enum PlcStatus {
        PLC_UNKNOWN,                                // no successful acknowledgment yet
        PLC_RUNNING,                                // communicating normally
        PLC_TIMEOUT,                                // no acknowledgment within PLC_TIMEOUT_MS
        PLC_FAULT                                   // no acknowledgment within PLC_WATCHDOG_MS
    };

    // Reader state enumeration
    enum PlcRxState {
        RX_IDLE,                                    // wait for a new write message from the other side
        RX_READ,                                    // read data from the PLC's output shared memory
        RX_ACK                                      // acknowledge the write message from the other side
    };

    // Writer state enumeration
    enum PlcTxState {
        TX_WRITE,                                   // write data to the PLC's input shared memory
        TX_COMMIT,                                  // commit a write (or send heartbeat pulse)
        TX_WAIT,                                    // wait for the last commit to be acknowledged
        TX_IDLE                                     // wait for a new write to be requested
                                                    //  (or for the heartbeat interval to elapse)
    };

    // This class provides logic to safely interact with the PLC's shared memory,
    // even in the case where we cannot assume that the PLC runtime uses ordered
    // memory accesses to interact with the input/output shared memory ranges.
    //
    // The synchronization handler is implemented as two distinct state machines,
    // one handles reading data from the PLC's output range, and one handles
    // writing data to the PLC's input range. Both the reader and the writer use
    // explicit in-order operations and a transactional model implemented using
    // sequence/acknowledgment counters.
    //
    // InputData and OutputData are the user-supplied site-specific payload types.
    // They must be trivially copyable. The combined PlcInput<InputData> /
    // PlcOutput<OutputData> memory layout must match the PLC IDE's generated types
    // (verified on OPTA by a static_assert in the user's sketch).
    template<typename InputData, typename OutputData>
    class PlcSyncHandler {
    private:
        // Log protocol buffers (local copies updated on each RX/TX cycle)
        bool             _tx_write       = false;
        PlcOutputLogs_t  _rx_logs_buffer = {};
        PlcInputLogs_t   _tx_logs_buffer = {};
        // NOTE: seq/ack naming is from the PLC's point of view:
        //   TxSeqNum/TxAckNum → our RX (PLC output data)
        //   RxSeqNum/RxAckNum → our TX (PLC input data)
        uint32_t               _heartbeat_ms  = Config::PLC_HEARTBEAT_MS;
        uint32_t               _timeout_ms    = Config::PLC_TIMEOUT_MS;
        uint32_t*              _timestamp_ms  = nullptr;
        PlcInput<InputData>*   _plcin;                  // PLC input memory  (write-only)
        PlcOutput<OutputData>* _plcout;                 // PLC output memory (read-only)
        OutputData             _rx_data_buffer;         // local receive buffer for _plcout->data
        InputData              _tx_data_buffer;         // local transmit buffer for _plcin->data
        PlcRxState             _rx_state      = RX_IDLE;
        PlcTxState             _tx_state      = TX_WRITE;
        uint32_t               _tx_last_ms    = 0;
        bool                   _ever_ackd     = false;

        // Transition triggers
        inline bool _rx_new_message()      { return _plcout->sync.txSeqNum != _plcin->sync.txAckNum; }
        inline bool _tx_heartbeat_elapsed(){ return *_timestamp_ms - _tx_last_ms > _heartbeat_ms; }
        inline bool _tx_acknowledged() {
            bool result = _plcout->sync.rxAckNum == _plcin->sync.rxSeqNum;
            if (result) { _tx_last_ms = *_timestamp_ms; _ever_ackd = true; }
            return result;
        }

        // Transition actions
        inline void _rx_memcpy() {
            HAL::ordered_copy(&_rx_logs_buffer, &_plcout->logs);
            HAL::ordered_copy(&_rx_data_buffer, &_plcout->data);
        }
        inline void _tx_memcpy() {
            HAL::ordered_copy(&_plcin->logs, &_tx_logs_buffer);
            HAL::ordered_copy(&_plcin->data, &_tx_data_buffer);
            _tx_write = false;
        }
        inline void _rx_acknowledge() {
            HAL::ordered_copy(&_plcin->sync.txAckNum, &_plcout->sync.txSeqNum);
        }
        inline void _tx_commit() {
            uint32_t src = _plcin->sync.rxSeqNum + 1;
            HAL::ordered_copy(&_plcin->sync.rxSeqNum, &src);
        }

        // State machines
        PlcRxState _rx_step() {
            switch (_rx_state) {
                case RX_IDLE:   if (_rx_new_message())      return RX_READ; break;
                case RX_READ:   _rx_memcpy();               return RX_ACK;  break;
                case RX_ACK:    _rx_acknowledge();          return RX_IDLE; break;
            }
            return _rx_state;
        }
        PlcTxState _tx_step() {
            switch (_tx_state) {
                case TX_WRITE:  _tx_memcpy();               return TX_COMMIT; break;
                case TX_COMMIT: _tx_commit();               return TX_WAIT;   break;
                case TX_WAIT:   if (_tx_acknowledged())     return TX_IDLE;   break;
                case TX_IDLE:   if (_tx_write)              return TX_WRITE;
                        else if (_tx_heartbeat_elapsed())   return TX_COMMIT; break;
            }
            return _tx_state;
        }

    public:
        PlcSyncHandler(PlcInput<InputData>* plcin, PlcOutput<OutputData>* plcout,
                       uint32_t* timestamp_ms)
            : _timestamp_ms(timestamp_ms), _plcin(plcin), _plcout(plcout)
        {
            memset(&_tx_data_buffer, 0, sizeof(InputData));
            memset(&_rx_data_buffer, 0, sizeof(OutputData));
            _tx_last_ms = *timestamp_ms;
        }

        void                   write()                  { _tx_write = true; }
        bool*                  write_flag()             { return &_tx_write; }
        const PlcOutputLogs_t* output_logs() const      { return &_rx_logs_buffer; }
        PlcInputLogs_t*        input_logs()             { return &_tx_logs_buffer; }
        const OutputData*      output_data() const      { return &_rx_data_buffer; }
        InputData*             input_data()             { return &_tx_data_buffer; }

        bool timed_out() const { return *_timestamp_ms - _tx_last_ms > _timeout_ms; }

        PlcStatus status() const {
            uint32_t elapsed = *_timestamp_ms - _tx_last_ms;
            if (!_ever_ackd)                        return PLC_UNKNOWN;
            if (elapsed > Config::PLC_WATCHDOG_MS)  return PLC_FAULT;
            if (elapsed > _timeout_ms)              return PLC_TIMEOUT;
                                                    return PLC_RUNNING;
        }

        void step() {
            _rx_state = _rx_step();
            _tx_state = _tx_step();
        }
    };

}}

#endif // PLCIO_SYNC_H
