#ifndef PLCIO_EMULATOR_H
#define PLCIO_EMULATOR_H

/**
 * @file emulator.h
 * @brief PlcEmulator - simulator-side stand-in that drives the shared-memory
 *        protocol from the PLC's side, mirroring PlcSyncHandler.
 */

#include <cstdint>
#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/plcio/sync.h"

#ifdef TARGET_SIMULATOR
#include <cstdio>
#include <unistd.h>
#include <pthread.h>
#include "libbotanik/scheduler/scheduler.h"  // make_thread_realtime for the emulator thread

/// Time-dilates a PLC link timeout; defined header-only in the site's sim.h.
inline uint32_t plc_dilated_timeout(uint32_t base_ms);
#endif

namespace Botanik { namespace PlcIO {

    /** @brief Non-template context (mirror of PlcSyncContext) for the emulator side. */
    struct PlcEmulatorContext {
        template<typename, typename> friend class PlcEmulator;
    private:
        /** @name State
         *  @{ */

        bool             _tx_write       = false;
        /** @brief Emulator RX <- what the host transmitted. */
        PlcInputLogs_t   _rx_logs_buffer = {};
        /** @brief Emulator TX -> what the host will receive. */
        PlcOutputLogs_t  _tx_logs_buffer = {};
        uint32_t*        _timestamp_ms   = nullptr;
        /** @brief Time of the last acknowledged TX (wrap-safe stamp, like the handler). */
        Utils::Millis32  _tx_last_ms;
        uint32_t         _timeout_ms     = Config::PLC_TIMEOUT_MS;  ///< elapsed before PLC_TIMEOUT
        uint32_t         _watchdog_ms    = Config::PLC_WATCHDOG_MS; ///< elapsed before PLC_FAULT
        bool             _ever_ackd      = false;

        Utils::Millis32  _now() const { return Utils::Millis32(*_timestamp_ms); }

        /** @} */

    public:
        /** @name Interface
         *  @{ */

        void                  write()              { _tx_write = true; }
        /** @brief Emulator output log buffer (host receives from here). */
        PlcOutputLogs_t*      output_logs()        { return &_tx_logs_buffer; }
        /** @brief Emulator input log buffer (host wrote here). */
        const PlcInputLogs_t* input_logs() const   { return &_rx_logs_buffer; }

        /** @brief Current PLC link status derived from the ack timing. */
        PlcStatus status() const {
            uint32_t elapsed = _now() - _tx_last_ms;
            if (!_ever_ackd)             return PLC_UNKNOWN;
            if (elapsed > _watchdog_ms)  return PLC_FAULT;
            if (elapsed > _timeout_ms)   return PLC_TIMEOUT;
                                         return PLC_RUNNING;
        }

        /** @} */
    };

    /**
     * @brief Simulator-side mirror of PlcSyncHandler.
     *
     * Simulates the PLC coprocessor: reads from the input range (what the host
     * wrote) and writes to the output range (what the host reads). InputData /
     * OutputData must match those of the paired handler.
     */
    template<typename InputData, typename OutputData>
    class PlcEmulator {
    private:
        /** @name State
         *  @{ */

        PlcEmulatorContext     _ctx;
        uint32_t               _heartbeat_ms  = Config::PLC_HEARTBEAT_MS;
        PlcInput<InputData>*   _plcin;        ///< PLC input memory (read-only for the emulator)
        PlcOutput<OutputData>* _plcout;       ///< PLC output memory (write-only for the emulator)
        InputData              _rx_data_buffer;
        OutputData             _tx_data_buffer;
        PlcRxState             _rx_state      = RX_IDLE;
        PlcTxState             _tx_state      = TX_IDLE;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Bind the emulator to PLC shared memory and a timestamp. */
        PlcEmulator(PlcInput<InputData>* plcin, PlcOutput<OutputData>* plcout,
                    uint32_t* timestamp_ms)
            : _plcin(plcin), _plcout(plcout)
        {
            _ctx._timestamp_ms = timestamp_ms;
            _ctx._tx_last_ms   = Utils::Millis32(*timestamp_ms);
            memset(&_rx_data_buffer, 0, sizeof(InputData));
            memset(&_tx_data_buffer, 0, sizeof(OutputData));
        }

        /** @} */

        /** @name Data access
         *  @{ */

        /** @brief Payload-independent context for the log writer. */
        PlcEmulatorContext*  context()           { return &_ctx; }
        /** @brief Transmit buffer staged for the PLC output payload. */
        OutputData*          output_data()       { return &_tx_data_buffer; }
        /** @brief Most recently received PLC input payload. */
        const InputData*     input_data() const  { return &_rx_data_buffer; }

        /** @} */

        /** @name Step
         *  @{ */

        /** @brief Advance both state machines once; call per tick. */
        inline void step() {
            _rx_state = _rx_step();
            _tx_state = _tx_step();
        }

        /** @} */

        /** @name Simulator timeout overrides
         *  Set by the emulator thread to the time-dilated link timeouts.
         *  @{ */
        void set_heartbeat_ms(uint32_t ms) { _heartbeat_ms     = ms; }
        void set_timeout_ms  (uint32_t ms) { _ctx._timeout_ms  = ms; }
        void set_watchdog_ms (uint32_t ms) { _ctx._watchdog_ms = ms; }
        /** @} */

    private:
        /** @name Transition triggers
         *  @{ */

        inline bool _rx_new_message()       { return HAL::ordered_read(&_plcin->sync.rxSeqNum) != _plcout->sync.rxAckNum; }
        inline bool _tx_heartbeat_elapsed() { return _ctx._now() - _ctx._tx_last_ms > _heartbeat_ms; }
        inline bool _tx_acknowledged() {
            bool result = HAL::ordered_read(&_plcin->sync.txAckNum) == _plcout->sync.txSeqNum;
            if (result) { _ctx._tx_last_ms = _ctx._now(); _ctx._ever_ackd = true; }
            return result;
        }

        /** @} */

        /** @name Transition actions
         *  @{ */

        inline void _rx_memcpy() {
            HAL::ordered_copy(&_ctx._rx_logs_buffer, &_plcin->logs);
            HAL::ordered_copy(&_rx_data_buffer, &_plcin->data);
        }
        inline void _tx_memcpy() {
            HAL::ordered_copy(&_plcout->logs, &_ctx._tx_logs_buffer);
            HAL::ordered_copy(&_plcout->data, &_tx_data_buffer);
            _ctx._tx_write = false;
        }
        inline void _rx_acknowledge() {
            uint32_t seq = _plcin->sync.rxSeqNum;
            HAL::ordered_copy(&_plcout->sync.rxAckNum, &seq);
        }
        inline void _tx_commit() {
            uint32_t next_seq = _plcout->sync.txSeqNum + 1;
            HAL::ordered_copy(&_plcout->sync.txSeqNum, &next_seq);
        }

        /** @} */

        /** @name State machines
         *  @{ */

        inline PlcRxState _rx_step() {
            switch (_rx_state) {
                case RX_IDLE: if (_rx_new_message())        return RX_READ; break;
                case RX_READ: _rx_memcpy();                 return RX_ACK;  break;
                case RX_ACK:  _rx_acknowledge();            return RX_IDLE; break;
            }
            return _rx_state;
        }
        inline PlcTxState _tx_step() {
            switch (_tx_state) {
                case TX_IDLE:   if (_ctx._tx_write)         return TX_WRITE;
                        else if (_tx_heartbeat_elapsed())   return TX_COMMIT;   break;
                case TX_WRITE:  _tx_memcpy();               return TX_COMMIT;   break;
                case TX_COMMIT: _tx_commit();               return TX_WAIT;     break;
                case TX_WAIT:   if (_tx_acknowledged())     return TX_IDLE;     break;
            }
            return _tx_state;
        }

        /** @} */
    };

    /** @brief Pushes log entries into the emulator's output buffer, simulating the
     *  PLC coprocessor sending logs to the host. */
    class PlcLogWriter {
    private:
        /** @name State
         *  @{ */

        PlcEmulatorContext* _ctx;
        const uint32_t*     _timestamp_ms;
        uint32_t            _next_seq   = 1;
        LogEntry_t*         _logBuffer;
        uint32_t*           _writeIndex;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Bind the writer to an emulator context and timestamp. */
        PlcLogWriter(PlcEmulatorContext* ctx, uint32_t* timestamp_ms);
        /** @brief Push one log entry into the emulator's output buffer. */
        void push_log(LogLevel level, LogTopic topic, const char* msg);

        /** @} */
    };

#ifdef TARGET_SIMULATOR

    // Host-side driver that runs a PlcEmulator on its own POSIX thread, simulating
    // the PLC coprocessor executing independently of the main loop. Simulator-only:
    // on OPTA the real coprocessor plays this role, so this section is compiled out.

    template<typename InputData, typename OutputData>
    struct EmulatorThreadArgs {
        PlcInput<InputData>*   shared_in;
        PlcOutput<OutputData>* shared_out;
        /** @brief Optional site hook filling the output payload with mock values;
         *  null leaves the payload zeroed. */
        void (*fill_output)(OutputData*, uint32_t now_ms);
    };

    /** @brief Run flag/handle for the single emulator thread. Single-writer (lifecycle
     *  controller) / single-reader (thread loop): volatile + HAL::dsb() to publish - the
     *  same lock-free pattern as the serial ring buffers, no std::atomic needed. */
    inline volatile bool& _emu_running() { static volatile bool running = false; return running; }
    inline pthread_t&      _emu_thread()  { static pthread_t thread; return thread; }
    /** @brief Set true by the thread as its final act (after its last shared-memory
     *  access), so a stopping caller can confirm the region is released without relying
     *  solely on pthread_join's internal synchronization. */
    inline volatile bool& _emu_stopped() { static volatile bool stopped = false; return stopped; }

    /** @brief Cooperatively stop the emulator thread and join it, releasing its
     *  hold on the shared PLC memory before the caller (HAL::reset) re-execs.
     *  A no-op if the thread is not running. */
    inline void stop_emulator_thread() {
        if (!_emu_running()) return;  // not running / already stopped
        _emu_running() = false;
        HAL::dsb();                   // publish the stop before joining / re-exec
        // When the watchdog trips, reset() runs *on* the emulator thread, so this
        // hook would be joining itself (EDEADLK). Skip the join in that case - the
        // following execve reaps the thread anyway, and we're synchronously inside
        // reset() so the emulator isn't touching shared memory concurrently.
        if (pthread_equal(pthread_self(), _emu_thread())) return;
        // Wait for the thread to release the shared region (it sets _emu_stopped as its
        // last act) before joining - an explicit handoff that doesn't assume the caller
        // is the main loop. Bounded by one emulator iteration.
        while (!_emu_stopped()) { /* spin */ }
        HAL::dsb();
        pthread_join(_emu_thread(), nullptr);
    }

    /** @brief Thread entry point: owns the emulator's timestamp, PlcEmulator and
     *  PlcLogWriter, steps them every TASK_PERIOD_MS, and emits a heartbeat log. */
    template<typename InputData, typename OutputData>
    inline void* _emulator_thread_main(void* arg) {
        auto* args = static_cast<EmulatorThreadArgs<InputData, OutputData>*>(arg);

        // Run realtime on its own core, mirroring the OPTA coprocessor. Per-thread,
        // so it must run here; null logger (the main thread reports any failure).
        Scheduler::make_thread_realtime(Config::SIM_EMULATOR_CPU_CORE, nullptr);

        uint32_t emu_timestamp_ms = 0;
        PlcEmulator<InputData, OutputData> plc_emulator(
            args->shared_in, args->shared_out, &emu_timestamp_ms);
        // Dilate the link timeouts, mirroring the host handler (main.cpp).
        plc_emulator.set_heartbeat_ms(plc_dilated_timeout(Config::PLC_HEARTBEAT_MS));
        plc_emulator.set_timeout_ms  (plc_dilated_timeout(Config::PLC_TIMEOUT_MS));
        plc_emulator.set_watchdog_ms (plc_dilated_timeout(Config::PLC_WATCHDOG_MS));
        PlcLogWriter plc_log_writer(plc_emulator.context(), &emu_timestamp_ms);

        uint32_t emu_tick = 0;
        _emu_stopped() = false;
        while (_emu_running()) {
            emu_timestamp_ms = (uint32_t)HAL::uptime_ms();
            plc_emulator.step();
            // serviced here so a wedged main loop still trips the watchdog
            HAL::watchdog_poll();
            // Refresh the mock payload + request a write at ~1 Hz (DATA_TICKS loop
            // iterations, the rate the site animator is tuned for).
            constexpr uint32_t DATA_TICKS = 1000 / Config::TASK_PERIOD_MS;
            if (args->fill_output != nullptr && (emu_tick % DATA_TICKS) == 0) {
                args->fill_output(plc_emulator.output_data(), emu_timestamp_ms);
                plc_emulator.context()->write();
            }
            emu_tick++;
            HAL::sleep_ms(Config::TASK_PERIOD_MS);
        }
        // Released the shared region: signal the stopping caller (see stop_emulator_thread).
        HAL::dsb();
        _emu_stopped() = true;
        return nullptr;
    }

    /** @brief Spawn the emulator thread against shared PLC memory and register its
     *  teardown with HAL::reset(). The argument block is a static (process lifetime),
     *  so no heap. Kept joinable (not detached) so reset() can stop it cleanly. */
    template<typename InputData, typename OutputData>
    inline void start_emulator_thread(PlcInput<InputData>* shared_in,
                                      PlcOutput<OutputData>* shared_out,
                                      void (*fill_output)(OutputData*, uint32_t) = nullptr,
                                      Logs::Logger* logger = nullptr) {
        static EmulatorThreadArgs<InputData, OutputData> args{ shared_in, shared_out, fill_output };
        _emu_running() = true;
        // memory barrier to make `_emu_running` visible before the thread reads it
        HAL::dsb();
        // Start the thread. Never let this fail silently: without the emulator the
        // PLC link never acks and the handler is stuck in UNK forever, with no hint
        // why. EAGAIN here usually means the stack mmap couldn't be locked under
        // RLIMIT_MEMLOCK - see the start-order note in the sketch's main().
        int rc = pthread_create(&_emu_thread(), nullptr,
                                &_emulator_thread_main<InputData, OutputData>, &args);
        if (rc != 0) {
            _emu_running() = false;
            if (logger != nullptr)
                logger->error(PLC_IO, "emulator thread failed to start: %s", strerror(rc));
            return;
        }
        // register a way to stop the thread on system resets
        HAL::on_reset(&stop_emulator_thread);
    }

#endif // TARGET_SIMULATOR

}}

#endif // PLCIO_EMULATOR_H
