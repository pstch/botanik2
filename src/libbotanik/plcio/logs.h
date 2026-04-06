#ifndef PLCIO_LOGS_H
#define PLCIO_LOGS_H

#include "libbotanik/logs/logger.h"
#include "libbotanik/plcio/sync.h"

using namespace Botanik::Logs;

namespace Botanik { namespace PlcIO {

    class PlcLogReader {
    private:
        // Parameters
                Logger*         _logger;         // pointer to the output logger
                PlcSyncHandler* _handler;        // pointer to the PLC synchronization handler
        const   uint32_t*       _timestamp_ms;   // pointer to the current cycle's timestamp
        // Reader state
                uint32_t _last_seq = 0;          // last sequence number (to detect dropped entries)
                uint32_t _dropped = 0;           // dropped entry count
        // PLC I/O pointers
        const   LogEntry_t*     _logBuffer;      // pointer to the PLC log buffer
        const   uint32_t*       _writeIndex;     // pointer to the PLC's log entry write index
                uint32_t*       _readIndex;      // pointer to the PLC's log entry read index
        // handle sequence number to detect dropped PLC log entries
        void _detect_dropped_entries(const LogEntry_t* entry);
        // build an actual log entry, and push it to the logger instance
        void _push_plc_entry(const LogEntry_t* plcEntry);

    public:
        // Constructor
        PlcLogReader(PlcSyncHandler* handler, Logger* logger, uint32_t* timestamp_ms);
        // drain the PLC log buffer
        void drain();

    };

}}

#endif // PLCIO_LOGS_H