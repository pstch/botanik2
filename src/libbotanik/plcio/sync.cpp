#include <cstdint>
#include <cstring>

#include "libbotanik/hal/platform.h"
#include "libbotanik/plcio/sync.h"


namespace Botanik { namespace PlcIO {

    namespace {
        using namespace Botanik::HAL;
    }

    // Transition triggers
    inline bool PlcSyncHandler::_rx_new_message() {
        return _plcout->sync.txSeqNum != _plcin->sync.txAckNum;
    }
    inline bool PlcSyncHandler::_tx_acknowledged() {
        bool result = _plcout->sync.rxAckNum == _plcin->sync.rxSeqNum;
        if (result) _tx_last_ms = *_timestamp_ms;
        return result;
    }
    inline bool PlcSyncHandler::_tx_heartbeat_elapsed() {
        return *_timestamp_ms - _tx_last_ms > _heartbeat_ms;
    }
    // Transition actions
    inline void PlcSyncHandler::_rx_memcpy() {
        ordered_copy(&_rx_buffer, &_plcout->data);
    }
    inline void PlcSyncHandler::_tx_memcpy() {
        ordered_copy(&_plcin->data, &_tx_buffer);
        _tx_write = false;        
    }
    inline void PlcSyncHandler::_rx_acknowledge() {
        ordered_copy(&_plcin->sync.txAckNum, &_plcout->sync.txSeqNum);
    }
    inline void PlcSyncHandler::_tx_commit() {
        uint32_t src = _plcin->sync.rxSeqNum + 1;
        ordered_copy(&_plcin->sync.rxSeqNum, &src);
    }
    // Reader state machine (handle messages from the PLC's writer)
    PlcRxState PlcSyncHandler::_rx_step() { 
        switch (_rx_state) {
            case RX_IDLE:   if (_rx_new_message())      return RX_READ; break;
            case RX_READ:   _rx_memcpy();               return RX_ACK; break;
            case RX_ACK:    _rx_acknowledge();          return RX_IDLE; break;
        };
        return _rx_state;
    }
    // Writer state machine (send messages to the PLC's reader)
    PlcTxState PlcSyncHandler::_tx_step() {
        switch (_tx_state) {
            case TX_WRITE:  _tx_memcpy();               return TX_COMMIT; break;
            case TX_COMMIT: _tx_commit();               return TX_WAIT; break;
            case TX_WAIT:   if (_tx_acknowledged())     return TX_IDLE; break;
            case TX_IDLE:   if (_tx_write)              return TX_WRITE;
                    else if (_tx_heartbeat_elapsed())   return TX_COMMIT; break;
        }; 
        return _tx_state;
    }

    PlcSyncHandler::PlcSyncHandler(uint32_t* timestamp_ms) : _timestamp_ms(timestamp_ms) {
        _plcin  = static_cast<PlcInput_t*>(get_plc_input());
        _plcout = static_cast<PlcOutput_t*>(get_plc_output());        
        memset(&_tx_buffer, 0, sizeof(PlcInputData_t));
        memset(&_rx_buffer, 0, sizeof(PlcOutputData_t));
        _tx_last_ms = *timestamp_ms;
    };

    const PlcOutputData_t* PlcSyncHandler::output() { 
        return &_rx_buffer;
    }

    PlcInputData_t* PlcSyncHandler::input() {
        return &_tx_buffer;
    }

    void PlcSyncHandler::write() { 
        _tx_write = true;
    }

    bool PlcSyncHandler::timed_out() {
        return *_timestamp_ms - _tx_last_ms > _timeout_ms;
    }

    void PlcSyncHandler::step() {
        _rx_state = _rx_step(); 
        _tx_state = _tx_step();
    }

}}