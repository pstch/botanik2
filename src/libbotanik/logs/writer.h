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
    protected:
        /** @name Channel implementation (provided by subclasses)
         *  @{ */

        /** @brief Serialize bytes to the output medium (never blocks) */
        virtual size_t  write(const char* buf, size_t len) = 0;
        /** @brief Format a log entry into @p buf; return the number of bytes written */
        virtual size_t  format(const LogEntry_t* entry, char* buf, size_t len) = 0;

        /** @} */
    public:
        /** @name Queue interface
         *  @{ */

        /** @brief Enqueue a new log entry for deferred output */
        virtual void    push(const LogEntry_t* entry) = 0;
        /** @brief Attempt to flush enqueued entries to the output medium */
        virtual void    drain() = 0;

        /** @} */
    };

    /**
     * @brief LogWriter with an in-memory FIFO queue and incremental drain.
     *
     * Derived classes supply format() and write(). This class handles queueing,
     * drop counting, and partial-send resumption across multiple drain() calls.
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

    private:
        /** @name Channel implementation
         *  @{ */

        /** @brief Format a log entry into @p buf; returns the number of bytes written */
        size_t format(const LogEntry_t* entry, char* buf, size_t len);
        /** @brief Write @p len bytes to the bound serial port (never blocks) */
        size_t write(const char* buf, size_t len);

        /** @} */
    };

}}

#endif // LOGS_WRITER_H
