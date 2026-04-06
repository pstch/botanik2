#ifndef PLCIO_EMULATOR_H
#define PLCIO_EMULATOR_H

#include <cstdint>

#include "libbotanik_config.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/plcio/sync.h"
#include "libbotanik/plcio/vars.h"

using namespace Botanik::Config;

namespace Botanik { namespace PlcIO {

    class PlcEmulator {
    private:
        // Parameters
        uint32_t        _heartbeat_ms       = PLC_HEARTBEAT_MS;     // heartbeat commit interval
        // Current timestamp
        uint32_t*       _timestamp_ms       = 0;         // pointer to the current cycle's timestamp
        // Shared PLC variables
        PlcInput_t*     _plcin;                         // PLC input memory (read-only)
        PlcOutput_t*    _plcout;                        // PLC output memory (write-only)
        // Local buffers
        PlcInputData_t  _rx_buffer;                     // local receive buffer for _plcin
        PlcOutputData_t _tx_buffer;                     // local transmit buffer for _plcout
        // Local state
        PlcRxState      _rx_state           = RX_IDLE;  // reader state
        PlcTxState      _tx_state           = TX_IDLE;  // writer state
        uint32_t        _tx_last_ms         = 0;        // last successful write
        bool            _tx_write           = false;    // schedule immediate write
        // Transition triggers
        inline bool _rx_new_message();
        inline bool _tx_acknowledged();
        inline bool _tx_heartbeat_elapsed();
        // Transition actions
        inline void _rx_memcpy();
        inline void _tx_memcpy();
        inline void _rx_acknowledge();
        inline void _tx_commit();
        // Reader state machine (reads messages from the PLC's writer)
        PlcRxState _rx_step();
        // Writer state machine (send messages to the PLC's reader)
        PlcTxState _tx_step();
    public:
        // Constructor
        PlcEmulator(uint32_t* timestamp_ms);
        // Get a pointer to the local copy of our output buffer
        PlcOutputData_t* output();
        // Get a pointer to the local copy of our input buffer
        const PlcInputData_t* input();
        // Schedule a write for the next time the handler is stepped through
        void write();
        // Step through the input/output synchronization state machines
        void step();
    };

    class PlcLogWriter {
    private:
        // Parameters
                PlcEmulator*    _emulator;      // pointer to the PLC emulator
        const   uint32_t*       _timestamp_ms;  // pointer to the current cycle's timestamp
        // Local state
                uint32_t        _next_seq = 1;  // next log entry sequence number 
        // PLC I/O pointers
                LogEntry_t*     _logBuffer;     // pointer to the PLC log buffer
                uint32_t*       _writeIndex;    // pointer to the PLC's log entry write index
        const   uint32_t*       _readIndex;     // pointer to the PLC's log entry read index
    public:
        // Constructor
        PlcLogWriter(PlcEmulator* emulator, uint32_t* timestamp_ms);
        // Push a new log message to the PlcLogReader on the other side
        void push_log(LogLevel level, LogTopic topic, const char* msg);
    };


}}

#endif // PLCIO_EMULATOR_H