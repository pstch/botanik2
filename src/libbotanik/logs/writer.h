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

    /**
     * @brief Base class for logging output channels.
     *
     * Derived classes supply format() and write(); the standard implementation
     * is @ref BufferedLogWriter.
     */
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
     * @brief LogWriter with an in-memory FIFO queue and incremental drain.
     *
     * Derived classes supply format() and write(). This class handles queueing,
     * drop counting, and partial-send resumption across multiple drain() calls.
     *
     * Main-loop only (not ISR-safe): push() and drain() share the plain queue
     * cursors, so every log call and drain must run on the main loop.
     */
    class BufferedLogWriter : public LogWriter {
    private:
        using LogEntry_t = PlcIO::LogEntry_t;

        /** @name Entry queue
         *  @{ */

        /** @brief Ring buffer of pending entries. */
        LogEntry_t _queue[Config::LOG_QUEUE_SIZE];
        /** @brief Read cursor into _queue. */
        uint8_t _readIndex = 0;
        /** @brief Write cursor into _queue. */
        uint8_t _writeIndex = 0;

        /** @} */

        /** @name Outgoing message
         *  @{ */

        /** @brief The entry currently being sent. */
        char _message[Config::LOG_FORMATTED_LEN];
        /** @brief Total bytes in _message. */
        size_t _length = 0;
        /** @brief Bytes of _message already written. */
        size_t _sent = 0;

        /** @} */

        /** @name Counters
         *  @{ */

        /** @brief Entries dropped because the queue was full. */
        uint32_t _dropped = 0;
        /** @brief Entries that failed to format. */
        uint32_t _failed = 0;

        /** @} */

    public:
        /** @name Queue interface
         *  @{ */

        /** @brief Enqueue @p entry; drops silently if the queue is full */
        void push(const LogEntry_t* entry) override;
        /** @brief Flush queued entries to the output medium (call from the main loop) */
        void drain() override;

        /** @} */

    private:
        /** @name Internals
         *  @{ */

        /** @brief Drain bytes from the currently outgoing message. */
        bool _drain_message();

        /** @} */
    };

    /**
     * @brief BufferedLogWriter that writes formatted entries to a BufferedSerial port.
     *
     * Format: `[timestamp] [level] [topic] message\n`
     * Respects the TX buffer's available capacity on each drain() call.
     */
    class SerialLogWriter : public BufferedLogWriter {
    protected:
        using BufferedSerial = Utils::BufferedSerial;
        using LogEntry_t = PlcIO::LogEntry_t;

    private:
        /** @name State
         *  @{ */

        /** @brief Bound output port. */
        BufferedSerial* _serial;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Bind the writer to the given serial port */
        SerialLogWriter(BufferedSerial& serial);

        /** @} */

        /** @name Stepping
         *  @{ */

        /** @brief Drain queued entries into the bound serial's TX ring. Call once
         *  per main-loop tick - unlike the page writers (drained from their render
         *  path), this writer has no render and must be stepped here. The serial
         *  itself is flushed separately by the main loop. */
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
     * The console UI and this sink can share a terminal (on the simulator the UI
     * renders to stdout while the monitor writes to stderr; run without
     * redirecting stderr and both land on the same screen). Log lines would then
     * garble the display, so when the bound port reports an interactive output
     * (BufferedSerial::is_output_tty()) every entry is dropped at push() time -
     * nothing is queued, formatted, or written. Redirect the port to a file or
     * pipe to capture the log. On hardware the monitor UART is never a tty, so
     * nothing is ever dropped (and the class is a hook for board-specific
     * monitor behaviour).
     */
    class MonitorLogWriter : public SerialLogWriter {
    private:
        /** @name State
         *  @{ */

        /** @brief True when the output is an interactive terminal; entries are
         *  dropped rather than written so they can't garble a shared display. */
        bool _suppressed;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Bind to @p serial; suppression is decided once from whether the
         *  port's output is an interactive terminal. */
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
