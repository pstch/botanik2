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

    /** @name PLC log reader
     *  @{ */

    /** @class PlcLogReader
     *  @brief Drains the PLC log ring buffer into the local Logger. */
    class PlcLogReader {
        using Logger = Logs::Logger;

        /** @name State
         *  @{ */

        /** @brief Local logger that PLC entries are forwarded into. */
        Logger*             _logger;
        /** @brief Shared-memory sync context. */
        PlcSyncContext*     _ctx;
        /** @brief Last seen sequence number. */
        uint32_t            _last_seq = 0;
        /** @brief Count of detected dropped entries. */
        uint32_t            _dropped  = 0;
        /** @brief Shared PLC log ring buffer. */
        const LogEntry_t*   _logBuffer;
        /** @brief PLC-owned write cursor. */
        const uint32_t*     _writeIndex;
        /** @brief Host-owned read cursor. */
        uint32_t*           _readIndex;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Bind the reader to a sync context and local logger. */
        PlcLogReader(PlcSyncContext* ctx, Logger* logger);
        /** @brief Drain all pending PLC log entries */
        void drain();
        /** @brief Drain all pending PLC log entries
         *  @note call once per tick. */
        void step();

        /** @} */

    private:
        /** @name Internals
         *  @{ */

        /** @brief Detect and report sequence-number gaps as dropped entries. */
        void _detect_dropped_entries(const LogEntry_t* entry);
        /** @brief Forward one PLC entry into the local Logger. */
        void _push_plc_entry(const LogEntry_t* plcEntry);

        /** @} */
    };

    /** @} */

}}

#endif // PLCIO_LOGS_H