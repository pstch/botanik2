#ifndef LOGS_WRITER_H
#define LOGS_WRITER_H

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
        /** @brief Serialize bytes to the output medium (never blocks) */
        virtual size_t  write(const char* buf, size_t len) = 0;
        /** @brief Format a log entry into @p buf; return the number of bytes written */
        virtual size_t  format(const LogEntry_t* entry, char* buf, size_t len) = 0;
    public:
        /** @brief Enqueue a new log entry for deferred output */
        virtual void    push(const LogEntry_t* entry) = 0;
        /** @brief Attempt to flush enqueued entries to the output medium */
        virtual void    drain() = 0;
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
        // Internal queue for this specific output channel
        LogEntry_t _queue[Config::LOG_QUEUE_SIZE];
        uint8_t _readIndex = 0;
        uint8_t _writeIndex = 0;
        // Outgoing message
        char _message[Config::LOG_FORMATTED_LEN];
        size_t _length = 0;
        size_t _sent = 0;
        // Counters
        uint32_t _dropped = 0;
        uint32_t _failed = 0;
        // Drain bytes from the currently outgoing message
        bool _drain_message();
    public:
        /** @brief Enqueue @p entry; drops silently if the queue is full */
        void push(const LogEntry_t* entry) override;
        /** @brief Flush queued entries to the output medium (call from the main loop) */
        void drain() override;
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
        BufferedSerial* _serial;
        size_t format(const LogEntry_t* entry, char* buf, size_t len);
        size_t write(const char* buf, size_t len);
    public:
        /** @brief Bind the writer to the given serial port */
        SerialLogWriter(BufferedSerial& serial);
    };

}}

#endif // LOGS_WRITER_H