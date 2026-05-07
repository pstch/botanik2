#ifndef PLCIO_LOGS_H
#define PLCIO_LOGS_H

#include "libbotanik/logs/logger.h"
#include "libbotanik/plcio/sync.h"

namespace Botanik { namespace PlcIO {

    class PlcLogReader {
    private:
        using Logger = Logs::Logger;
                Logger*         _logger;
        const   uint32_t*       _timestamp_ms;
                PlcSyncContext* _ctx;
                uint32_t _last_seq = 0;
                uint32_t _dropped  = 0;
        const   LogEntry_t*     _logBuffer;
        const   uint32_t*       _writeIndex;
                uint32_t*       _readIndex;
        void _detect_dropped_entries(const LogEntry_t* entry);
        void _push_plc_entry(const LogEntry_t* plcEntry);

    public:
        PlcLogReader(PlcSyncContext* ctx, Logger* logger, uint32_t* timestamp_ms);
        void step();

    };

}}

#endif // PLCIO_LOGS_H