// REVIEW_35: TODO
#ifndef PLCIO_LOGS_H
#define PLCIO_LOGS_H

/**
 * @file logs.h
 * @brief PlcLogReader - drains the PLC's log ring buffer into the local Logger,
 *        detecting and reporting dropped entries via sequence numbers.
 */

#include "../logs/logger.h"
#include "sync.h"

namespace Botanik { namespace PlcIO {

    /** @name PLC log reader
     *  @{ */

    /** @class PlcLogReader
     *  @brief Drains the PLC log ring buffer into the local Logger. */
    class PlcLogReader : public Component {
        using Logger = Logs::Logger;

        /** @name State
         *  @{ */

        PlcSyncContext*     _ctx;
        uint32_t            _last_seq = 0;  ///< only meaningful once _have_seq
        /** @brief True once a first entry has been seen (so seq 0 is a real
         *  value, not a "first entry" sentinel - keeps gap detection correct
         *  across the 2^32 wrap). */
        bool                _have_seq = false;
        uint32_t            _dropped  = 0;
        const Logs::LogEntry*   _logBuffer;
        const uint32_t*     _writeIndex;    ///< PLC-owned write cursor
        uint32_t*           _readIndex;     ///< host-owned read cursor

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Bind the reader to a sync context and local logger. */
        PlcLogReader(PlcSyncContext* ctx, Logger* logger);
        /** @brief Drain all pending PLC log entries. */
        void drain();

        /** @} */

    protected:
        /** @brief Drain all pending PLC log entries (driven by Component::step;
         *  @p now_ms unused). */
        void on_step(uint64_t now_ms) override;

    private:
        /** @name Internals
         *  @{ */

        /** @brief Detect and report sequence-number gaps as dropped entries. */
        void _detect_dropped_entries(const Logs::LogEntry* entry);
        /** @brief Forward one PLC entry into the local Logger. */
        void _push_plc_entry(const Logs::LogEntry* plcEntry);

        /** @} */
    };

    /** @} */

}}

#endif // PLCIO_LOGS_H