#include <cstring>
#include <algorithm>

#include "libbotanik/plcio/emulator.h"

namespace Botanik { namespace PlcIO {

    // Transition triggers
    inline bool PlcEmulator::_rx_new_message() {
        return _plcin->sync.rxSeqNum != _plcout->sync.rxAckNum;
    }
    inline bool PlcEmulator::_tx_acknowledged() {
        bool result = _plcin->sync.txAckNum == _plcout->sync.txSeqNum;
        if (result) { _tx_last_ms = *_timestamp_ms; _ever_ackd = true; }
        return result;
    }
    inline bool PlcEmulator::_tx_heartbeat_elapsed() {
        return *_timestamp_ms - _tx_last_ms > _heartbeat_ms;
    }

    // Transition actions
    inline void PlcEmulator::_rx_memcpy() {
        HAL::ordered_copy(&_rx_buffer, &_plcin->data);
    }
    inline void PlcEmulator::_tx_memcpy() {
        HAL::ordered_copy(&_plcout->data, &_tx_buffer);
        _tx_write = false;
    }
    inline void PlcEmulator::_rx_acknowledge() {
        // PLC acknowledges by matching Opta's sequence number
        uint32_t seq = _plcin->sync.rxSeqNum;
        HAL::ordered_copy(&_plcout->sync.rxAckNum, &seq);
    }
    inline void PlcEmulator::_tx_commit() {
        // PLC commits by incrementing its own sequence number
        uint32_t next_seq = _plcout->sync.txSeqNum + 1;
        HAL::ordered_copy(&_plcout->sync.txSeqNum, &next_seq);
    }

    // Reader state machine (handle messages from PlcSyncHandler)
    PlcRxState PlcEmulator::_rx_step() {
        switch (_rx_state) {
            case RX_IDLE: if (_rx_new_message())        return RX_READ; break;
            case RX_READ: _rx_memcpy();                 return RX_ACK;  break;
            case RX_ACK:  _rx_acknowledge();            return RX_IDLE; break;
        }
        return _rx_state;
    }

    PlcTxState PlcEmulator::_tx_step() {
        switch (_tx_state) {
            case TX_IDLE: if (_tx_write)                return TX_WRITE;
                if (_tx_heartbeat_elapsed())            return TX_COMMIT;   break;
            case TX_WRITE:  _tx_memcpy();               return TX_COMMIT;   break;
            case TX_COMMIT: _tx_commit();               return TX_WAIT;     break;
            case TX_WAIT:   if (_tx_acknowledged())     return TX_IDLE;     break;
        }
        return _tx_state;
    }

    PlcEmulator::PlcEmulator(uint32_t* timestamp_ms) : _timestamp_ms(timestamp_ms) {
        _plcin  = static_cast<PlcInput_t*>(HAL::get_plc_input());
        _plcout = static_cast<PlcOutput_t*>(HAL::get_plc_output());
        std::memset(&_rx_buffer, 0, sizeof(PlcInputData_t));
        std::memset(&_tx_buffer, 0, sizeof(PlcOutputData_t));
        _tx_last_ms = *timestamp_ms;
    }

    PlcOutputData_t* PlcEmulator::output() {
        return &_tx_buffer;
    }

    const PlcInputData_t* PlcEmulator::input() {
        return &_rx_buffer;
    }

    void PlcEmulator::write() {
        _tx_write = true;
    }

    PlcStatus PlcEmulator::status() {
        uint32_t elapsed = *_timestamp_ms - _tx_last_ms;
        if (!_ever_ackd)                return PLC_UNKNOWN;
        if (elapsed > Config::PLC_WATCHDOG_MS)  return PLC_FAULT;
        if (elapsed > Config::PLC_TIMEOUT_MS)   return PLC_TIMEOUT;
                                                return PLC_RUNNING;
    }

    void PlcEmulator::step() {
        _rx_state = _rx_step();
        _tx_state = _tx_step();
    }

    PlcLogWriter::PlcLogWriter(PlcEmulator* emulator, uint32_t* timestamp_ms) :
        _emulator(emulator), _timestamp_ms(timestamp_ms)
    {
        // For the Emulator side:
        // Output (TX) contains the logBuffer and writeIndex
        // Input (RX) contains the readIndex (feedback from the Opta)
        _logBuffer  = emulator->output()->logBuffer;
        _writeIndex = &emulator->output()->writeIndex;
        _readIndex  = &emulator->input()->readIndex;
    }

    void PlcLogWriter::push_log(LogLevel level, LogTopic topic, const char* msg) {
        uint32_t writeIndex = *_writeIndex % Config::LOG_PLC_BUFFER_SIZE;
        LogEntry_t& entry = _logBuffer[writeIndex];

        entry.timestamp = *_timestamp_ms;
        entry.level     = level;
        entry.topic     = topic;
        entry.seqNum    = _next_seq++;

        uint16_t len = (uint16_t)std::min(
            std::strlen(msg), (size_t)(Config::LOG_PLC_MESSAGE_LEN - 1));
        std::memcpy(entry.message, msg, len);
        entry.message[len] = '\0';
        entry.length = len;

        *_writeIndex = (*_writeIndex + 1) % Config::LOG_PLC_BUFFER_SIZE;
        _emulator->write();
    }
}}