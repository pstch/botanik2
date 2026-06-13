#ifndef PLCIO_EMULATOR_H
#define PLCIO_EMULATOR_H

/**
 * @file emulator.h
 * @brief PlcEmulator — simulator-side stand-in that drives the shared-memory
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
#endif

namespace Botanik { namespace PlcIO {

    /** @brief Non-template context (mirror of PlcSyncContext) for the emulator side. */
    struct PlcEmulatorContext {
        template<typename, typename> friend class PlcEmulator;
    private:
        /** @name State
         *  @{ */

        /** @brief A TX write has been requested. */
        bool             _tx_write       = false;
        /** @brief Emulator RX ← what the host transmitted. */
        PlcInputLogs_t   _rx_logs_buffer = {};
        /** @brief Emulator TX → what the host will receive. */
        PlcOutputLogs_t  _tx_logs_buffer = {};
        /** @brief Shared wall-clock timestamp (ms). */
        uint32_t*        _timestamp_ms   = nullptr;
        /** @brief Time of the last acknowledged TX. */
        uint32_t         _tx_last_ms     = 0;
        /** @brief True once any TX has ever been acknowledged. */
        bool             _ever_ackd      = false;

        /** @} */

    public:
        /** @name Interface
         *  @{ */

        /** @brief Request a TX write on the next emulator step. */
        void                  write()              { _tx_write = true; }
        /** @brief Emulator output log buffer (host receives from here). */
        PlcOutputLogs_t*      output_logs()        { return &_tx_logs_buffer; }
        /** @brief Emulator input log buffer (host wrote here). */
        const PlcInputLogs_t* input_logs() const   { return &_rx_logs_buffer; }

        /** @brief Current PLC link status derived from the ack timing. */
        PlcStatus status() const {
            uint32_t elapsed = *_timestamp_ms - _tx_last_ms;
            if (!_ever_ackd)                        return PLC_UNKNOWN;
            if (elapsed > Config::PLC_WATCHDOG_MS)  return PLC_FAULT;
            if (elapsed > Config::PLC_TIMEOUT_MS)   return PLC_TIMEOUT;
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

        /** @brief Payload-independent emulator context (logs, timing). */
        PlcEmulatorContext     _ctx;
        /** @brief Heartbeat/commit interval (ms). */
        uint32_t               _heartbeat_ms  = Config::PLC_HEARTBEAT_MS;
        /** @brief PLC input memory (read-only for the emulator). */
        PlcInput<InputData>*   _plcin;
        /** @brief PLC output memory (write-only for the emulator). */
        PlcOutput<OutputData>* _plcout;
        /** @brief Local receive buffer for _plcin->data. */
        InputData              _rx_data_buffer;
        /** @brief Local transmit buffer for _plcout->data. */
        OutputData             _tx_data_buffer;
        /** @brief RX state-machine state. */
        PlcRxState             _rx_state      = RX_IDLE;
        /** @brief TX state-machine state. */
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
            _ctx._tx_last_ms   = *timestamp_ms;
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

    private:
        /** @name Transition triggers
         *  @{ */

        inline bool _rx_new_message()       { return _plcin->sync.rxSeqNum != _plcout->sync.rxAckNum; }
        inline bool _tx_heartbeat_elapsed() { return *_ctx._timestamp_ms - _ctx._tx_last_ms > _heartbeat_ms; }
        inline bool _tx_acknowledged() {
            bool result = _plcin->sync.txAckNum == _plcout->sync.txSeqNum;
            if (result) { _ctx._tx_last_ms = *_ctx._timestamp_ms; _ctx._ever_ackd = true; }
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

        /** @brief Emulator context owning the output log buffer. */
        PlcEmulatorContext* _ctx;
        /** @brief Shared wall-clock timestamp (ms). */
        const uint32_t*     _timestamp_ms;
        /** @brief Next sequence number to stamp on an entry. */
        uint32_t            _next_seq   = 1;
        /** @brief Shared output log ring buffer. */
        LogEntry_t*         _logBuffer;
        /** @brief Emulator-owned write cursor. */
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
    };

    /** @brief Run flag and handle for the single emulator thread. Non-template
     *  (there is only ever one emulator), so stop_emulator_thread() can be a plain
     *  function pointer registered with HAL::on_reset. The flag is single-writer
     *  (the lifecycle controller) / single-reader (the thread loop): volatile keeps
     *  the loop from caching it, HAL::dsb() orders the publish — the same lock-free
     *  pattern as the serial ring buffers, no std::atomic needed. */
    inline volatile bool& _emu_running() { static volatile bool running = false; return running; }
    inline pthread_t&      _emu_thread()  { static pthread_t thread; return thread; }

    /** @brief Cooperatively stop the emulator thread and join it, releasing its
     *  hold on the shared PLC memory before the caller (HAL::reset) re-execs.
     *  A no-op if the thread is not running. */
    inline void stop_emulator_thread() {
        if (!_emu_running()) return;  // not running / already stopped
        _emu_running() = false;
        HAL::dsb();                   // publish the stop before joining / re-exec
        // When the watchdog trips, reset() runs *on* the emulator thread, so this
        // hook would be joining itself (EDEADLK). Skip the join in that case — the
        // following execve reaps the thread anyway, and we're synchronously inside
        // reset() so the emulator isn't touching shared memory concurrently.
        if (pthread_equal(pthread_self(), _emu_thread())) return;
        pthread_join(_emu_thread(), nullptr);
    }

    /** @brief Thread entry point: owns the emulator's timestamp, PlcEmulator and
     *  PlcLogWriter, steps them every TASK_PERIOD_MS, and emits a heartbeat log. */
    template<typename InputData, typename OutputData>
    inline void* _emulator_thread_main(void* arg) {
        auto* args = static_cast<EmulatorThreadArgs<InputData, OutputData>*>(arg);

        uint32_t emu_timestamp_ms = 0;
        PlcEmulator<InputData, OutputData> plc_emulator(
            args->shared_in, args->shared_out, &emu_timestamp_ms);
        PlcLogWriter plc_log_writer(plc_emulator.context(), &emu_timestamp_ms);

        uint32_t emu_tick = 0;
        while (_emu_running()) {
            emu_timestamp_ms = (uint32_t)HAL::uptime_ms();
            plc_emulator.step();
            HAL::watchdog_poll();  // serviced here so a wedged main loop still trips the watchdog
            // Emit a periodic heartbeat log every 30s, simulating the PLC coprocessor.
            constexpr uint32_t HEARTBEAT_TICKS = 30000 / Config::TASK_PERIOD_MS;
            if ((++emu_tick % HEARTBEAT_TICKS) == 0) {
                char buf[40];
                snprintf(buf, sizeof(buf), "periodic log #%lu",
                         (unsigned long)(emu_tick / HEARTBEAT_TICKS));
                plc_log_writer.push_log(LOG_INFO, PLC_IO, buf);
            }
            HAL::sleep_ms(Config::TASK_PERIOD_MS);
        }
        return nullptr;
    }

    /** @brief Spawn the emulator thread against the shared PLC memory and register
     *  its teardown with HAL::reset(). The argument block is a static with process
     *  lifetime, so it stays valid for the thread without any heap allocation. The
     *  thread is kept joinable (not detached) so reset() can stop it cleanly. */
    template<typename InputData, typename OutputData>
    inline void start_emulator_thread(PlcInput<InputData>* shared_in,
                                      PlcOutput<OutputData>* shared_out) {
        static EmulatorThreadArgs<InputData, OutputData> args{ shared_in, shared_out };
        _emu_running() = true;
        // memory barrier to make `_emu_running` visible before the thread reads it
        HAL::dsb();
        // start the thread
        pthread_create(&_emu_thread(), nullptr, &_emulator_thread_main<InputData, OutputData>, &args);
        // register a way to stop the thread on system resets
        HAL::on_reset(&stop_emulator_thread);
    }

#endif // TARGET_SIMULATOR

}}

#endif // PLCIO_EMULATOR_H
