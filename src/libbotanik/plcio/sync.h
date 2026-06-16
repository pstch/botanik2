#ifndef PLCIO_SYNC_H
#define PLCIO_SYNC_H

/**
 * @file sync.h
 * @brief PlcSyncHandler - independent RX and TX state machines for shared-memory
 *        communication with the PLC coprocessor, with DSB-barriered transactional
 *        reads/writes and heartbeat/timeout/watchdog tracking.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/plcio/vars.h"
#include "libbotanik/logs/logger.h"

namespace Botanik { namespace PlcIO {

    /** @name Status & state types
     *  @{ */

    /** @brief PLC connection status */
    enum PlcStatus {
        PLC_UNKNOWN,                                // no successful acknowledgment yet
        PLC_RUNNING,                                // communicating normally
        PLC_TIMEOUT,                                // no acknowledgment within PLC_TIMEOUT_MS
        PLC_FAULT                                   // no acknowledgment within PLC_WATCHDOG_MS
    };

    /** @brief Reader state enumeration */
    enum PlcRxState {
        RX_IDLE,                                    // wait for a new write message from the other side
        RX_READ,                                    // read data from the PLC's output shared memory
        RX_ACK                                      // acknowledge the write message from the other side
    };

    /** @brief Writer state enumeration */
    enum PlcTxState {
        TX_WRITE,                                   // write data to the PLC's input shared memory
        TX_COMMIT,                                  // commit a write (or send heartbeat pulse)
        TX_WAIT,                                    // wait for the last commit to be acknowledged
        TX_IDLE                                     // wait for a new write to be requested
                                                    //  (or for the heartbeat interval to elapse)
    };

    /** @} */

    // Non-template context object. Holds the parts of PlcSyncHandler that are
    // independent of the payload types: log buffers, TX-write flag, timing state,
    // and the stable public interface consumed by PlcLogReader and Page classes.
    /** @brief Payload-independent PLC sync state (log buffers, link timing, status),
     *  shared with log readers and pages without exposing the templated handler. */
    struct PlcSyncContext {
        template<typename, typename> friend class PlcSyncHandler;
    private:
        /** @name State
         *  @{ */

        /** @brief A TX write has been requested by the application. */
        bool             _tx_write       = false;
        /** @brief Local copy of the PLC output log buffer. */
        PlcOutputLogs_t  _rx_logs_buffer = {};
        /** @brief Local copy of the PLC input log buffer. */
        PlcInputLogs_t   _tx_logs_buffer = {};
        /** @brief Shared wall-clock timestamp (ms). */
        uint32_t*        _timestamp_ms   = nullptr;
        /** @brief Time of the last acknowledged TX. */
        uint32_t         _tx_last_ms     = 0;
        /** @brief Elapsed threshold (ms) before PLC_TIMEOUT is reported. */
        uint32_t         _timeout_ms     = Config::PLC_TIMEOUT_MS;
        /** @brief True once any TX has ever been acknowledged. */
        bool             _ever_ackd      = false;
        /** @brief Logger for link status changes (may be null). */
        Logs::Logger*    _logger         = nullptr;
        /** @brief Last reported status, for change detection. */
        PlcStatus        _last_status    = PLC_UNKNOWN;

        /** @} */

    public:
        /** @name Interface
         *  @{ */

        /** @brief Request a TX write on the next handler step. */
        void                   write()             { _tx_write = true; }
        /** @brief Local copy of the PLC's output log buffer. */
        const PlcOutputLogs_t* output_logs() const { return &_rx_logs_buffer; }
        /** @brief Local copy of the PLC's input log buffer. */
        PlcInputLogs_t*        input_logs()        { return &_tx_logs_buffer; }

        /** @brief Current PLC link status derived from the ack timing. */
        PlcStatus status() const {
            uint32_t elapsed = *_timestamp_ms - _tx_last_ms;
            if (!_ever_ackd)                        return PLC_UNKNOWN;
            if (elapsed > Config::PLC_WATCHDOG_MS)  return PLC_FAULT;
            if (elapsed > _timeout_ms)              return PLC_TIMEOUT;
                                                    return PLC_RUNNING;
        }

        /** @brief True once PLC activity has been silent for PLC_WATCHDOG_MS. */
        bool faulted() const { return status() == PLC_FAULT; }

        /** @} */

    private:
        /** @name Internals
         *  @{ */

        /** @brief Emit a log entry when the link status transitions. */
        void _log_status_change() {
            if (_logger == nullptr) return;
            PlcStatus current = status();
            if (current == _last_status) return;
            switch (current) {
                case PLC_RUNNING:
                    if (_last_status == PLC_UNKNOWN) _logger->info  (PLC_IO, "PLC link established");
                    else                             _logger->notice(PLC_IO, "PLC link recovered");
                    break;
                case PLC_TIMEOUT: _logger->warning(PLC_IO, "PLC link timeout");  break;
                case PLC_FAULT:   _logger->error  (PLC_IO, "PLC watchdog fault"); break;
                default:          _last_status = current; return;
            }
            _last_status = current;
        }

        /** @} */
    };

    // This class provides logic to safely interact with the PLC's shared memory,
    // even in the case where we cannot assume that the PLC runtime uses ordered
    // memory accesses to interact with the input/output shared memory ranges.
    //
    // The synchronization handler is implemented as two distinct state machines,
    // one handles reading data from the PLC's output range, and one handles
    // writing data to the PLC's input range. Both the reader and the writer use
    // explicit in-order operations and a transactional model implemented using
    // sequence/acknowledgment counters.
    //
    // InputData and OutputData are the user-supplied site-specific payload types.
    // They must be trivially copyable. The combined PlcInput<InputData> /
    // PlcOutput<OutputData> memory layout must match the PLC IDE's generated types
    // (verified on OPTA by a static_assert in the user's sketch).
    /** @brief Transactional RX/TX state machines over PLC shared memory, templated
     *  on the site payloads. See @ref md_docs_2PLC__SYNC. */
    template<typename InputData, typename OutputData>
    class PlcSyncHandler {
    private:
        /** @name State
         *  @{ */

        // NOTE: seq/ack naming is from the PLC's point of view:
        //   TxSeqNum/TxAckNum -> our RX (PLC output data)
        //   RxSeqNum/RxAckNum -> our TX (PLC input data)

        /** @brief Payload-independent context (logs, timing, status). */
        PlcSyncContext         _ctx;
        /** @brief Heartbeat/commit interval (ms). */
        uint32_t               _heartbeat_ms  = Config::PLC_HEARTBEAT_MS;
        /** @brief Last commit/heartbeat pulse (for re-commit on stall). */
        uint32_t               _tx_commit_ms  = 0;
        /** @brief PLC input memory (write-only). */
        PlcInput<InputData>*   _plcin;
        /** @brief PLC output memory (read-only). */
        PlcOutput<OutputData>* _plcout;
        /** @brief Local receive buffer for _plcout->data. */
        OutputData             _rx_data_buffer;
        /** @brief Local transmit buffer for _plcin->data. */
        InputData              _tx_data_buffer;
        /** @brief RX state-machine state. */
        PlcRxState             _rx_state      = RX_IDLE;
        /** @brief TX state-machine state. */
        PlcTxState             _tx_state      = TX_WRITE;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Bind the handler to PLC shared memory and a timestamp. */
        PlcSyncHandler(PlcInput<InputData>* plcin, PlcOutput<OutputData>* plcout,
                       uint32_t* timestamp_ms, Logs::Logger* logger = nullptr)
            : _plcin(plcin), _plcout(plcout)
        {
            _ctx._timestamp_ms = timestamp_ms;
            _ctx._tx_last_ms   = *timestamp_ms;
            _tx_commit_ms      = *timestamp_ms;
            _ctx._logger       = logger;
            memset(&_tx_data_buffer, 0, sizeof(InputData));
            memset(&_rx_data_buffer, 0, sizeof(OutputData));
        }

        /** @} */

        /** @name Data access
         *  @{ */

        /** @brief Payload-independent context for log readers and pages. */
        PlcSyncContext*   context()           { return &_ctx; }
        /** @brief Most recently received PLC output payload. */
        const OutputData* output_data() const { return &_rx_data_buffer; }
        /** @brief Transmit buffer staged for the PLC input payload. */
        InputData*        input_data()        { return &_tx_data_buffer; }

        /** @} */

        /** @name Step
         *  @{ */

        /** @brief Advance both state machines once; call per tick. */
        inline void step() {
            _rx_state = _rx_step();
            _tx_state = _tx_step();
            _ctx._log_status_change();
        }

        /** @} */

    private:
        /** @name Transition triggers
         *  @{ */

        inline bool _rx_new_message()      { return _plcout->sync.txSeqNum != _plcin->sync.txAckNum; }
        inline bool _tx_heartbeat_elapsed(){ return *_ctx._timestamp_ms - _ctx._tx_last_ms > _heartbeat_ms; }
        // Re-pulse a commit left unacked for a whole heartbeat (TX_WAIT recovery).
        inline bool _tx_recommit_elapsed() { return *_ctx._timestamp_ms - _tx_commit_ms > _heartbeat_ms; }
        inline bool _tx_acknowledged() {
            bool result = _plcout->sync.rxAckNum == _plcin->sync.rxSeqNum;
            if (result) { _ctx._tx_last_ms = *_ctx._timestamp_ms; _ctx._ever_ackd = true; }
            return result;
        }

        /** @} */

        /** @name Transition actions
         *  @{ */

        inline void _rx_memcpy() {
            HAL::dsb();  // acquire: order payload reads after the txSeqNum read
            HAL::ordered_copy(&_ctx._rx_logs_buffer, &_plcout->logs);
            HAL::ordered_copy(&_rx_data_buffer, &_plcout->data);
        }
        inline void _tx_memcpy() {
            HAL::ordered_copy(&_plcin->logs, &_ctx._tx_logs_buffer);
            HAL::ordered_copy(&_plcin->data, &_tx_data_buffer);
            _ctx._tx_write = false;
        }
        inline void _rx_acknowledge() {
            HAL::ordered_copy(&_plcin->sync.txAckNum, &_plcout->sync.txSeqNum);
        }
        inline void _tx_commit() {
            uint32_t src = _plcin->sync.rxSeqNum + 1;
            HAL::ordered_copy(&_plcin->sync.rxSeqNum, &src);
            _tx_commit_ms = *_ctx._timestamp_ms;
        }

        /** @} */

        /** @name State machines
         *  @{ */

        inline PlcRxState _rx_step() {
            switch (_rx_state) {
                case RX_IDLE:   if (_rx_new_message())      return RX_READ; break;
                case RX_READ:   _rx_memcpy();               return RX_ACK;  break;
                case RX_ACK:    _rx_acknowledge();          return RX_IDLE; break;
            }
            return _rx_state;
        }
        inline PlcTxState _tx_step() {
            switch (_tx_state) {
                case TX_WRITE:  _tx_memcpy();               return TX_COMMIT; break;
                case TX_COMMIT: _tx_commit();               return TX_WAIT;   break;
                case TX_WAIT:   if (_tx_acknowledged())     return TX_IDLE;
                        else if (_tx_recommit_elapsed())    return TX_COMMIT; break;
                case TX_IDLE:   if (_ctx._tx_write)         return TX_WRITE;
                        else if (_tx_heartbeat_elapsed())   return TX_COMMIT; break;
            }
            return _tx_state;
        }

        /** @} */
    };

}}

#endif // PLCIO_SYNC_H
