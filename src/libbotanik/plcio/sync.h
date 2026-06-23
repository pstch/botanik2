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
#include "libbotanik/component.h"
#include "libbotanik/utils/assert.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/plcio/vars.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/utils/time.h"

namespace Botanik { namespace PlcIO {

    /** @name Status & state types
     *  @{ */

    /** @brief PLC connection status */
    enum PlcStatus {
        PLC_UNKNOWN,   // no successful acknowledgment yet
        PLC_RUNNING,   // communicating normally
        PLC_TIMEOUT,   // no acknowledgment within PLC_TIMEOUT_MS
        PLC_FAULT      // no acknowledgment within PLC_WATCHDOG_MS
    };

    /** @brief Reader state enumeration */
    enum PlcRxState {
        RX_IDLE,
        RX_READ,
        RX_ACK
    };

    /** @brief Writer state enumeration */
    enum PlcTxState {
        TX_WRITE,
        TX_COMMIT,   // commit a write (or send heartbeat pulse)
        TX_WAIT,
        TX_IDLE      // wait for a write request (or heartbeat interval)
    };

    /** @} */

    /** @brief Payload-independent PLC sync state (log buffers, link timing, status),
     *  shared with log readers and pages without exposing the templated handler. */
    struct PlcSyncContext {
        template<typename, typename> friend class PlcSyncHandler;
    private:
        /** @name State
         *  @{ */

        bool             _tx_write       = false;
        PlcOutputLogs_t  _rx_logs_buffer = {};
        PlcInputLogs_t   _tx_logs_buffer = {};
        /** @brief PLC's generic config (network/NTP/MQTT). */
        PlcOutputConfig_t _rx_config_buffer = {};
        /** @brief Shared loop clock (ms), read-only. */
        const uint32_t*  _timestamp_ms   = nullptr;
        /** @brief Time of the last acknowledged TX (wrap-safe loop-clock stamp). */
        Utils::Millis32  _tx_last_ms;
        /** @brief Elapsed before PLC_TIMEOUT. */
        uint32_t         _timeout_ms     = Config::PLC_TIMEOUT_MS;
        /** @brief Elapsed before PLC_FAULT. */
        uint32_t         _watchdog_ms    = Config::PLC_WATCHDOG_MS;
        bool             _ever_ackd      = false;
        /** @brief Link status changes (may be null). */
        Logs::Logger*    _logger         = nullptr;
        /** @brief For change detection. */
        PlcStatus        _last_status    = PLC_UNKNOWN;

        /** @name Diagnostics (set by the handler ctor; surfaced by the `plc` command)
         *  @{ */
        // PLC input region (we write, PLC reads)
        const void*      _in_region      = nullptr;
        // PLC output region (PLC writes, we read)
        const void*      _out_region     = nullptr;
        uint32_t         _in_size        = 0;
        uint32_t         _out_size       = 0;
        // -> handler's live torn-read counter
        const uint32_t*  _torn_ptr       = nullptr;
        // host-written handshake counters (live)
        const PlcInputCounters_t*  _in_sync  = nullptr;
        // PLC-written handshake counters (live)
        const PlcOutputCounters_t* _out_sync = nullptr;
        /** @} */

        Utils::Millis32  _now() const { return Utils::Millis32(*_timestamp_ms); }

        /** @} */

    public:
        /** @name Interface
         *  @{ */

        /** @brief Request a TX write on the next handler step. */
        void                   write()             { _tx_write = true; }
        /** @brief Local copy of the PLC's output log buffer. */
        const PlcOutputLogs_t* output_logs() const { return &_rx_logs_buffer; }
        /** @brief Local copy of the PLC's generic configuration (network/NTP/MQTT).
         *  Snapshotted in the same RX transaction as the payload. Each block's
         *  `serial` increments when the PLC changes it; a consumer latches the serial
         *  and re-applies only on change (see Network::NetworkHandler). */
        const PlcOutputConfig_t* config() const { return &_rx_config_buffer; }
        /** @brief Local copy of the PLC's input log buffer. */
        PlcInputLogs_t*        input_logs()        { return &_tx_logs_buffer; }
        const PlcInputLogs_t*  input_logs() const  { return &_tx_logs_buffer; }

        /** @brief Current PLC link status derived from the ack timing. */
        PlcStatus status() const {
            uint32_t elapsed = _now() - _tx_last_ms;
            if (!_ever_ackd)             return PLC_UNKNOWN;
            if (elapsed > _watchdog_ms)  return PLC_FAULT;
            if (elapsed > _timeout_ms)   return PLC_TIMEOUT;
                                         return PLC_RUNNING;
        }

        /** @brief True once PLC activity has been silent for PLC_WATCHDOG_MS. */
        bool faulted() const { return status() == PLC_FAULT; }

        /** @name Diagnostics (the `plc` shell command)
         *  @{ */
        const void* in_region()  const { return _in_region; }
        const void* out_region() const { return _out_region; }
        uint32_t    in_size()    const { return _in_size; }
        uint32_t    out_size()   const { return _out_size; }
        /** @brief True once any TX was acknowledged by the PLC. */
        bool        ever_acked()      const { return _ever_ackd; }
        /** @brief Milliseconds since the last acknowledged TX (0 if never acked). */
        uint32_t    last_ack_age_ms() const {
            return _ever_ackd ? (uint32_t)(_now() - _tx_last_ms) : 0;
        }
        /** @brief Live count of torn RX snapshots discarded (PLC bumped seq
         *  mid-copy). */
        uint32_t    torn_count()      const { return _torn_ptr ? *_torn_ptr : 0; }

        /** @name Live sync counters (each region's own handshake fields, read live; the
         *  PLC-POV names match PlcInput/OutputCounters_t in vars.h)
         *  @{ */
        /** @brief IN region RX seq: our latest committed TX sequence, read by
         *  the PLC. */
        uint32_t    in_rx_seq()  const { return _in_sync  ? HAL::ordered_read(&_in_sync->rxSeqNum)  : 0; }
        /** @brief IN region TX ack: our acknowledgment of the PLC's published
         *  output. */
        uint32_t    in_tx_ack()  const { return _in_sync  ? HAL::ordered_read(&_in_sync->txAckNum)  : 0; }
        /** @brief OUT region TX seq: the PLC's latest published output sequence. */
        uint32_t    out_tx_seq() const { return _out_sync ? HAL::ordered_read(&_out_sync->txSeqNum) : 0; }
        /** @brief OUT region RX ack: the PLC's acknowledgment of our input. */
        uint32_t    out_rx_ack() const { return _out_sync ? HAL::ordered_read(&_out_sync->rxAckNum) : 0; }
        /** @} */
        /** @} */

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
                // Entering fault is logged loudly here; HealthMonitor keeps kicking the
                // HW watchdog through a grace window and logs the actual kick-withhold
                // (the reboot) only if the fault persists (see PLC_FAULT_GRACE_MS).
                case PLC_FAULT:   _logger->critical(PLC_IO, "PLC link fault (no PLC heartbeat)"); break;
                default:          _last_status = current; return;
            }
            _last_status = current;
        }

        /** @} */
    };

    // Two state machines (RX from the PLC output range, TX to its input range)
    // using explicit in-order ops and a transactional seq/ack model, so it is
    // safe even when the PLC runtime doesn't order its accesses to the shared
    // ranges.
    //
    // InputData/OutputData are the site payloads, trivially copyable. The
    // combined PlcInput/PlcOutput layout must match the PLC IDE's generated
    // types (verified on OPTA by a static_assert in the user's sketch).
    /** @brief Transactional RX/TX state machines over PLC shared memory, templated
     *  on the site payloads. See @ref md_docs_2PLC__SYNC. */
    template<typename InputData, typename OutputData>
    class PlcSyncHandler : public Component {
    private:
        /** @name State
         *  @{ */

        // NOTE: seq/ack naming is from the PLC's point of view:
        //   TxSeqNum/TxAckNum -> our RX (PLC output data)
        //   RxSeqNum/RxAckNum -> our TX (PLC input data)

        PlcSyncContext         _ctx;
        uint32_t               _heartbeat_ms  = Config::PLC_HEARTBEAT_MS;
        /** @brief Last commit/heartbeat pulse (re-commit on stall). */
        Utils::Millis32        _tx_commit_ms;
        PlcInput<InputData>*   _plcin;         ///< PLC input memory (write-only)
        PlcOutput<OutputData>* _plcout;        ///< PLC output memory (read-only)
        OutputData             _rx_data_buffer;
        InputData              _tx_data_buffer;
        PlcRxState             _rx_state      = RX_IDLE;
        PlcTxState             _tx_state      = TX_WRITE;
        /** @brief RX snapshots discarded as torn (seq bumped mid-copy). */
        uint32_t               _rx_torn       = 0;
        /** @brief PLC's stale rxAckNum read at bind, used to reject a stale-coincidence
         *  ack at first contact (before any real ack is observed). */
        uint32_t               _ack_baseline  = 0;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Bind the handler to PLC shared memory and a timestamp. */
        PlcSyncHandler(PlcInput<InputData>* plcin, PlcOutput<OutputData>* plcout,
                       const uint32_t* timestamp_ms, Logs::Logger* logger = nullptr)
            : _plcin(plcin), _plcout(plcout)
        {
            // The shared-memory pointers must be mapped: a null here means the
            // PLC region wasn't wired (mis-linked image, or constructed before
            // the M4 is up).
            BOTANIK_ASSERT(plcin != nullptr);
            BOTANIK_ASSERT(plcout != nullptr);
            BOTANIK_ASSERT(timestamp_ms != nullptr);
            _ctx._timestamp_ms = timestamp_ms;
            _ctx._tx_last_ms   = Utils::Millis32(*timestamp_ms);
            _tx_commit_ms      = Utils::Millis32(*timestamp_ms);
            _ctx._logger       = logger;
            // Region + torn-counter introspection for the `plc` diagnostic command.
            _ctx._in_region    = plcin;   _ctx._in_size  = (uint32_t)sizeof(*plcin);
            _ctx._out_region   = plcout;  _ctx._out_size = (uint32_t)sizeof(*plcout);
            _ctx._torn_ptr     = &_rx_torn;
            _ctx._in_sync      = &plcin->sync;
            _ctx._out_sync     = &plcout->sync;
            memset(&_tx_data_buffer, 0, sizeof(InputData));
            memset(&_rx_data_buffer, 0, sizeof(OutputData));

            // First-contact baseline: the PLC sync counters may be
            // uninitialized or stale at boot, so don't trust them. Snapshot the
            // PLC's current output seq and ack, then seed our own input region
            // to a known start: rxSeqNum = 0 (our TX seq), and txAckNum = the
            // PLC's current txSeqNum so a stale value isn't mistaken for a fresh
            // RX message. _ack_baseline gates the first TX ack so a stale
            // rxAckNum can't be read as a live PLC (see _tx_acknowledged()).
            HAL::cache_invalidate(_plcout, sizeof(*_plcout));
            uint32_t plc_seq = HAL::ordered_read(&_plcout->sync.txSeqNum);
            _ack_baseline    = HAL::ordered_read(&_plcout->sync.rxAckNum);
            // rxSeqNum=0, txAckNum=plc_seq
            PlcInputCounters_t baseline = { 0u, plc_seq };
            HAL::ordered_copy(&_plcin->sync, &baseline);
            HAL::cache_clean(_plcin, sizeof(*_plcin));
        }

        /** @} */

        /** @name Data access
         *  @{ */

        /** @brief Payload-independent context for log readers and pages. */
        PlcSyncContext*   context()           { return &_ctx; }
        /** @brief Most recently received PLC output payload.
         *  @warning this is a stable interior pointer into the handler's local
         *  RX buffer; readers/pages cache it at construction, so the buffer must
         *  never relocate (no double-buffering/swap). */
        const OutputData* output_data() const { return &_rx_data_buffer; }
        /** @brief Transmit buffer staged for the PLC input payload. */
        InputData*        input_data()        { return &_tx_data_buffer; }
        /** @brief Count of RX snapshots discarded as torn (handshake violations). */
        uint32_t          rx_torn_count() const { return _rx_torn; }

        /** @} */

        /** @name Step
         *  @{ */

        /** @brief Advance both state machines once; call per tick. */
        inline void step() {
            announce_step(_ctx._logger, "PlcSyncHandler");
            // Coherency: drop any stale cached view of the PLC-written region before
            // we poll/copy it, and flush our writes to the host-written region after.
            HAL::cache_invalidate(_plcout, sizeof(*_plcout));
            _rx_state = _rx_step();
            _tx_state = _tx_step();
            _ctx._log_status_change();
            HAL::cache_clean(_plcin, sizeof(*_plcin));
        }

        /** @} */

#ifdef TARGET_SIMULATOR
        /** @name Simulator timeout overrides
         *  The simulator entry point swaps in time-dilated link timeouts; hardware
         *  keeps the Config defaults.
         *  @{ */
        void set_heartbeat_ms(uint32_t ms) { _heartbeat_ms     = ms; }
        void set_timeout_ms  (uint32_t ms) { _ctx._timeout_ms  = ms; }
        void set_watchdog_ms (uint32_t ms) { _ctx._watchdog_ms = ms; }
        /** @} */
#endif

    private:
        /** @name Transition triggers
         *  @{ */

        inline bool _rx_new_message() {
            return HAL::ordered_read(&_plcout->sync.txSeqNum)
                   != _plcin->sync.txAckNum;
        }
        // Pulse at heartbeat cadence from the last commit (not the last ack):
        // both the TX_IDLE keepalive and the TX_WAIT re-pulse of an unacked
        // commit use this, so an unacked link keeps a steady cadence instead of
        // re-firing every step.
        inline bool _tx_heartbeat_elapsed() {
            return _ctx._now() - _tx_commit_ms > _heartbeat_ms;
        }
        inline bool _tx_acknowledged() {
            uint32_t ack = HAL::ordered_read(&_plcout->sync.rxAckNum);
            if (ack != _plcin->sync.rxSeqNum) return false;
            // First-contact gate: a stale rxAckNum that coincidentally equals
            // our first seq must not be read as a live PLC. Until the link is
            // up, require the ack to differ from the bind-time stale value
            // (proof the PLC actually wrote it). A real PLC catches up within a
            // commit or two; after _ever_ackd it's normal.
            if (!_ctx._ever_ackd && ack == _ack_baseline) return false;
            _ctx._tx_last_ms = _ctx._now();
            _ctx._ever_ackd  = true;
            return true;
        }

        /** @} */

        /** @name Transition actions
         *  @{ */

        // Copy the PLC payload into local buffers. Returns false if the PLC bumped
        // its sequence during the copy (it overwrote the payload mid-read, so the
        // snapshot may be torn). The seq_before read also acts as the acquire,
        // ordering the payload copy after it.
        inline bool _rx_memcpy() {
            uint32_t seq_before = HAL::ordered_read(&_plcout->sync.txSeqNum);
            HAL::ordered_copy(&_ctx._rx_logs_buffer, &_plcout->logs);
            HAL::ordered_copy(&_ctx._rx_config_buffer, &_plcout->conf);
            HAL::ordered_copy(&_rx_data_buffer, &_plcout->data);
            // re-read from SRAM
            HAL::cache_invalidate(&_plcout->sync, sizeof(_plcout->sync));
            return HAL::ordered_read(&_plcout->sync.txSeqNum) == seq_before;
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
            _tx_commit_ms = _ctx._now();
        }

        /** @} */

        /** @name State machines
         *  @{ */

        inline PlcRxState _rx_step() {
            switch (_rx_state) {
                case RX_IDLE:   if (_rx_new_message())      return RX_READ; break;
                case RX_READ:
                    if (_rx_memcpy()) return RX_ACK;
                    // torn snapshot: discard, do not ack, re-read next tick
                    _rx_torn++;
                    if (_ctx._logger)
                        _ctx._logger->warning(PLC_IO,
                            "PLC RX torn read discarded (seq changed mid-copy)");
                    return RX_IDLE;
                case RX_ACK:    _rx_acknowledge();          return RX_IDLE; break;
            }
            return _rx_state;
        }
        inline PlcTxState _tx_step() {
            switch (_tx_state) {
                case TX_WRITE:  _tx_memcpy();               return TX_COMMIT; break;
                case TX_COMMIT: _tx_commit();               return TX_WAIT;   break;
                case TX_WAIT:   if (_tx_acknowledged())     return TX_IDLE;
                        else if (_tx_heartbeat_elapsed())   return TX_COMMIT; break;
                case TX_IDLE:   if (_ctx._tx_write)         return TX_WRITE;
                        else if (_tx_heartbeat_elapsed())   return TX_COMMIT; break;
            }
            return _tx_state;
        }

        /** @} */
    };

}}

#endif // PLCIO_SYNC_H
