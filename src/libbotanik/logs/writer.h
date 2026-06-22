#ifndef LOGS_WRITER_H
#define LOGS_WRITER_H

/**
 * @file writer.h
 * @brief Logging output channels (the sinks the Logger fans entries out to).
 *
 * Defines the LogWriter interface and two implementations: BufferedLogWriter
 * (in-memory FIFO with incremental, non-blocking drain) and SerialLogWriter
 * (formats entries and writes them to a BufferedSerial port).
 */

#include <cstdint>
#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/utils/serial.h"
#include "libbotanik/plcio/vars.h"

namespace Botanik { namespace Logs {

    /** @brief Base class for logging output channels. Derived classes supply
     *  format() and write(); the standard implementation is @ref BufferedLogWriter. */
    class LogWriter {
    private:
        using LogEntry_t = PlcIO::LogEntry_t;
    public:
        /** @name Queue interface
         *  @{ */

        /** @brief Enqueue a new log entry for deferred output */
        virtual void    push(const LogEntry_t* entry) = 0;
        /** @brief Attempt to flush enqueued entries to the output medium */
        virtual void    drain() = 0;
        /** @brief Cumulative entries lost at this sink, for loss reporting. */
        virtual uint32_t loss_count() const { return 0; }

        /** @} */
    protected:
        /** @name Channel implementation (provided by subclasses)
         *  @{ */

        /** @brief Serialize bytes to the output medium (never blocks) */
        virtual size_t  write(const char* buf, size_t len) = 0;
        /** @brief Format a log entry into @p buf; return the number of bytes written */
        virtual size_t  format(const LogEntry_t* entry, char* buf, size_t len) = 0;

        /** @} */
    };

    /**
     * @brief LogWriter with an in-memory FIFO queue and incremental drain. Handles
     * queueing, drop counting, and partial-send resumption across drain() calls.
     *
     * Main-loop only (not ISR-safe): push() and drain() share the plain queue cursors.
     */
    class BufferedLogWriter : public LogWriter {
    private:
        using LogEntry_t = PlcIO::LogEntry_t;

        /** @name Entry queue
         *  @{ */

        LogEntry_t _queue[Config::LOG_QUEUE_SIZE];   ///< ring of pending entries
        uint8_t _readIndex = 0;
        uint8_t _writeIndex = 0;

        /** @} */

        /** @name Outgoing message
         *  @{ */

        char _message[Config::LOG_FORMATTED_LEN];   ///< entry currently being sent
        size_t _length = 0;   ///< total bytes in _message
        size_t _sent = 0;     ///< bytes already written

        /** @} */

        /** @name Counters
         *  @{ */

        uint32_t _dropped = 0;   ///< dropped (queue full)
        uint32_t _failed = 0;    ///< failed to format

        /** @} */

    public:
        /** @name Queue interface
         *  @{ */

        /** @brief Enqueue @p entry. The last LOG_QUEUE_CRITICAL_RESERVE slots are kept
         *  for LOG_CRITICAL: a normal that would intrude is dropped (counted); a critical
         *  seats into the reserve, or evicts the oldest only if the queue is wholly full. */
        void push(const LogEntry_t* entry) override;
        /** @brief Flush queued entries to the output medium (call from the main loop) */
        void drain() override;
        /** @brief Entries dropped because the queue was full, plus format failures. */
        uint32_t loss_count() const override { return _dropped + _failed; }

        /** @} */

    private:
        /** @name Internals
         *  @{ */

        /** @brief Drain bytes from the currently outgoing message. */
        bool _drain_message();

        /** @} */
    };

    /** @brief BufferedLogWriter writing formatted entries (`...message\n`) to a
     *  BufferedSerial port, respecting the TX buffer's available capacity per drain(). */
    class SerialLogWriter : public BufferedLogWriter {
    protected:
        using BufferedSerial = Utils::BufferedSerial;
        using LogEntry_t = PlcIO::LogEntry_t;

    private:
        /** @name State
         *  @{ */

        BufferedSerial* _serial;   ///< bound output port

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Bind the writer to the given serial port */
        SerialLogWriter(BufferedSerial& serial);

        /** @} */

        /** @name Stepping
         *  @{ */

        /** @brief Drain queued entries into the bound serial's TX ring. Call once per
         *  tick - this writer has no render path (unlike the page writers), so it must be
         *  stepped here. The serial itself is flushed separately by the main loop. */
        void step();

        /** @} */

    private:
        /** @name Channel implementation
         *  @{ */

        /** @brief Format a log entry into @p buf; returns the number of bytes written */
        size_t format(const LogEntry_t* entry, char* buf, size_t len);
        /** @brief Write @p len bytes to the bound serial port (never blocks) */
        size_t write(const char* buf, size_t len);

        /** @} */
    };

    /**
     * @brief SerialLogWriter for a diagnostic monitor port that self-suppresses
     * when its output is an interactive terminal.
     *
     * The console UI and this sink can share a terminal (sim: UI on stdout, monitor
     * on stderr; both land on the same screen unless stderr is redirected). Log lines
     * would garble the display, so when the bound port is a tty
     * (BufferedSerial::is_output_tty()) every entry is dropped at push() - redirect to
     * a file/pipe to capture the log. The monitor UART is never a tty on hardware.
     */
    class MonitorLogWriter : public SerialLogWriter {
    private:
        /** @name State
         *  @{ */

        bool _suppressed;   ///< output is a tty: drop so it can't garble a shared display

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Bind to @p serial; suppression decided once from is_output_tty(). */
        MonitorLogWriter(BufferedSerial& serial);

        /** @} */

        /** @name Queue interface
         *  @{ */

        /** @brief Enqueue @p entry, or drop it when the output is a shared terminal. */
        void push(const LogEntry_t* entry) override;

        /** @} */
    };

}}

#endif // LOGS_WRITER_H
