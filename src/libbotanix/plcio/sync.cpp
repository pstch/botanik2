#include <cstdint>
#include <cstring>

#include "./sync.h"

namespace Botanix { namespace PlcIO {

    // Transition conditions
    inline bool PlcSyncHandler::_rx_new_message() {
        return _plcout->sync.txSeqNum != _plcin->sync.txAckNum;
    }
    inline bool PlcSyncHandler::_tx_acknowledged() {
        return _plcout->sync.rxAckNum == _plcin->sync.rxSeqNum;
    }
    inline bool PlcSyncHandler::_tx_heartbeat_elapsed() {
        return *_timestamp_ms - _tx_last_ms > _heartbeat_ms;
    }
    // Transition actions
    inline void PlcSyncHandler::_rx_memcpy() {
        ORDERED_COPY(&_rx_buffer, &_plcout->data);
    }
    inline void PlcSyncHandler::_tx_memcpy() {
        ORDERED_COPY(&_plcin->data, &_tx_buffer);
    }
    inline void PlcSyncHandler::_rx_acknowledge() {
        ORDERED_MOVE(&_plcin->sync.txAckNum, _plcout->sync.txSeqNum);
    }
    inline void PlcSyncHandler::_tx_commit() {
        ORDERED_MOVE(&_plcin->sync.rxSeqNum, _plcin->sync.rxSeqNum + 1);
    }
    // Reader state machine (handle messages from the PLC's writer)
    PlcRxState PlcSyncHandler::_rx_step() { switch (_rx_state) {
        case RX_IDLE:   if (_rx_new_message())              return RX_READ;
        case RX_READ:   _rx_memcpy();                       return RX_ACK;
        case RX_ACK:    _rx_acknowledge();                  return RX_IDLE;
    }; }
    // Writer state machine (send messages to the PLC's reader)
    PlcTxState PlcSyncHandler::_tx_step() { switch (_tx_state) {
        case TX_WRITE:  _tx_memcpy(); _tx_write = false;    return TX_COMMIT;
        case TX_COMMIT: _tx_commit();                       return TX_WAIT;
        case TX_WAIT:   if (_tx_acknowledged())             return TX_IDLE; break;
        case TX_IDLE:   if (_tx_write)                      return TX_WRITE;
                   else if (_tx_heartbeat_elapsed())        return TX_COMMIT; break;
    }; }
    // Constructor
    PlcSyncHandler::PlcSyncHandler(uint32_t* timestamp_ms) : _timestamp_ms(timestamp_ms) {
        memset(&_tx_buffer, 0, sizeof(PlcInputData_t));
        memset(&_rx_buffer, 0, sizeof(PlcOutputData_t));
    };
    // Get a pointer to the local copy of PLC's output buffer
    const PlcOutputData_t* PlcSyncHandler::output() { return &_rx_buffer; }
    // Get a pointer to the local copy of PLC's input buffer
    PlcInputData_t* PlcSyncHandler::input() { return &_tx_buffer; }
    // Schedule a write for the next time the handler is stepped through
    void PlcSyncHandler::write() { _tx_write = true; }
    // Return whether we have timed out waiting the other side's receiver
    bool PlcSyncHandler::timed_out() { return *_timestamp_ms - _tx_last_ms > _timeout_ms; }
    // Step through the input/output synchronization state machines
    void PlcSyncHandler::step() { _rx_state = _rx_step(); _tx_state = _tx_step(); }

}}