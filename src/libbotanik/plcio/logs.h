#ifndef PLCIO_LOGS_H
#define PLCIO_LOGS_H

#include "libbotanik/logs/logger.h"
#include "libbotanik/plcio/vars.h"

namespace Botanik { namespace PlcIO {

    class PlcLogReader {
    private:
        using Logger = Logs::Logger;
        // Parameters
                Logger*         _logger;
        const   uint32_t*       _timestamp_ms;
                bool*           _write_flag;
        // Reader state
                uint32_t _last_seq = 0;
                uint32_t _dropped  = 0;
        // PLC I/O pointers
        const   LogEntry_t*     _logBuffer;
        const   uint32_t*       _writeIndex;
                uint32_t*       _readIndex;
        void _detect_dropped_entries(const LogEntry_t* entry);
        void _push_plc_entry(const LogEntry_t* plcEntry);

    public:
        PlcLogReader(const PlcOutputLogs_t* output_logs, PlcInputLogs_t* input_logs,
                     bool* write_flag, Logger* logger, uint32_t* timestamp_ms);
        void step();

    };

}}

#endif // PLCIO_LOGS_H