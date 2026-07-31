// REVIEW_35: TODO
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

#include "../../libbotanik_config.h"
#include "../utils/serial.h"
#include "entry.h"

namespace Botanik { namespace Logs {

    /** @brief Base class for logging output channels. Derived classes supply
     *  format() and write(); the standard implementation is
     *  @ref BufferedLogWriter. */
    class LogWriter {
    private:
    public:
        /** @name Queue interface
         *  @{ */

        /** @brief Enqueue a new log entry for deferred output */
        virtual void    push(const Logs::LogEntry* entry) = 0;
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
        /** @brief Format a log entry into @p buf; return the number of bytes
         *  written */
        virtual size_t  format(const Logs::LogEntry* entry, char* buf, size_t len) = 0;

        /** @} */
    };

    /**
     * @brief LogWriter with an in-memory FIFO queue and incremental drain.
     * Handles queueing, drop counting, and partial-send resumption across
     * drain() calls.
     *
     * Main-loop only (not ISR-safe): push() and drain() share the plain queue
     * cursors.
     */
    class BufferedLogWriter : public LogWriter {
    private:

        /** @name Entry queue
         *  @{ */

        /** ring of pending entries */
        Logs::LogEntry _queue[Config::LOG_QUEUE_SIZE];
        uint8_t _readIndex = 0;
        uint8_t _writeIndex = 0;

        /** @} */

        /** @name Outgoing message
         *  @{ */

        /** entry currently being sent */
        char _message[Config::LOG_FORMATTED_LEN];
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

        /** @brief Enqueue @p entry. The last LOG_QUEUE_CRITICAL_RESERVE slots
         *  are kept for LOG_CRITICAL: a normal that would intrude is dropped
         *  (counted); a critical seats into the reserve, or evicts the oldest
         *  only if the queue is wholly full. */
        void push(const Logs::LogEntry* entry) override;
        /** @brief Flush queued entries to the output medium (call from the main
         *  loop) */
        void drain() override;
        /** @brief Entries dropped because the queue was full, plus format
         *  failures. */
        uint32_t loss_count() const override { return _dropped + _failed; }

        /** @} */

    protected:
        /** @brief The standard entry rendering (Logs::format_log_entry). A sink
         *  that needs its own wire shape overrides this; the page-side sinks all
         *  want exactly this, so it is the default rather than three copies. */
        size_t format(const Logs::LogEntry* entry, char* buf, size_t len) override;

    private:
        /** @name Internals
         *  @{ */

        /** @brief Drain bytes from the currently outgoing message. */
        bool _drain_message();
        /** @brief Occupied queue slots (0..LOG_QUEUE_SIZE-1). */
        uint8_t _used() const;

        /** @} */
    };

    /** @brief BufferedLogWriter writing formatted entries (`...message\n`) to a
     *  BufferedSerial port, respecting the TX buffer's available capacity per
     *  drain(). Also a Component: step(now_ms) drains the queue into the port's
     *  TX ring (the port is flushed to hardware separately). */
    class SerialLogWriter : public BufferedLogWriter, public Botanik::Component {
    protected:
        using BufferedSerial = Utils::BufferedSerial;

    private:
        /** @name State
         *  @{ */

        BufferedSerial* _serial;   ///< bound output port
        /** @brief drop every entry when the port is an interactive terminal: a diagnostic
         *  monitor port can share a tty with the console UI (sim: UI on stdout, monitor on
         *  stderr, same screen unless redirected), where log lines would garble the display.
         *  Decided once at construction; the monitor UART is never a tty on hardware. */
        bool _suppressed;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Bind the writer to @p serial. With @p drop_if_tty, the writer self-
         *  suppresses (drops every entry at push()) when @p serial is an interactive
         *  terminal - for a diagnostic monitor port that may share the console's tty.
         *  @p logger / @p name feed the Component base (announce; logger may be
         *  null to stay silent). */
        SerialLogWriter(BufferedSerial& serial, bool drop_if_tty = false,
                        Logger* logger = nullptr, const char* name = "SerialLogWriter");

        /** @} */

        /** @name Queue interface
         *  @{ */

        /** @brief Enqueue @p entry, or drop it when suppressed. */
        void push(const Logs::LogEntry* entry) override;

        /** @} */

    protected:
        /** @name Stepping
         *  @{ */

        /** @brief Drain queued entries into the serial's TX ring (driven by
         *  Component::step; @p now_ms unused). */
        void on_step(uint64_t now_ms) override;

        /** @} */

    private:
        /** @name Channel implementation
         *  @{ */

        /** @brief Format a log entry into @p buf; returns the bytes written */
        size_t format(const Logs::LogEntry* entry, char* buf, size_t len);
        /** @brief Write @p len bytes to the bound serial port (never blocks) */
        size_t write(const char* buf, size_t len);

        /** @} */
    };

}}

#endif // LOGS_WRITER_H
