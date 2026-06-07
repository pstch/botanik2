#ifndef PLCIO_LOGS_H
#define PLCIO_LOGS_H

/**
 * @file logs.h
 * @brief PlcLogReader — drains the PLC's log ring buffer into the local Logger,
 *        detecting and reporting dropped entries via sequence numbers.
 */

#include "libbotanik/logs/logger.h"
#include "libbotanik/plcio/sync.h"

namespace Botanik { namespace PlcIO {

    /** @brief Drains the PLC log ring buffer into the local Logger. */
    class PlcLogReader {
    private:
        using Logger = Logs::Logger;
                Logger*         _logger;
                PlcSyncContext* _ctx;
                uint32_t _last_seq = 0;     /**< Last seen sequence number */
                uint32_t _dropped  = 0;     /**< Count of detected dropped entries */
        const   LogEntry_t*     _logBuffer; /**< Shared PLC log ring buffer */
        const   uint32_t*       _writeIndex;/**< PLC-owned write cursor */
                uint32_t*       _readIndex; /**< Host-owned read cursor */
        /** @brief Detect and report sequence-number gaps as dropped entries. */
        void _detect_dropped_entries(const LogEntry_t* entry);
        /** @brief Forward one PLC entry into the local Logger. */
        void _push_plc_entry(const LogEntry_t* plcEntry);

    public:
        PlcLogReader(PlcSyncContext* ctx, Logger* logger);
        /** @brief Drain all pending PLC log entries; call once per tick. */
        void step();

    };

}}

#endif // PLCIO_LOGS_H