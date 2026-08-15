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

#include "libbotanik/config/logging.h"

#include "libbotanik/core/component.h"
#include "libbotanik/logs/entry.h"
#include "libbotanik/utils/serial.h"

namespace Botanik { namespace Tasks { struct Tick; } }

namespace Botanik { namespace Logs {

    /** @brief Base class for logging output channels. Derived classes supply
     *  format() and write(); the standard implementation is
     *  @ref BufferedLogWriter. */
    class LogWriter {
    protected:
        /** @name Level gate
         *  @{ */

        /** @brief Sink-local floor. Only ever MORE restrictive than the
         *  logger's: an entry the logger dropped never arrives here to be
         *  admitted. LOG_CRITICAL is the enum maximum, so a critical always
         *  passes. Open by default - a sink that wants less says so. */
        Logs::LogLevel _min_level = Logs::LOG_DEBUG;

        /** @} */

        /** @name Identity
         *  @{ */

        /** @brief What this sink is called in a loss report, or null for a sink
         *  that never named itself (the report then says only how many). */
        const char* _sink_name;

        /** @brief The parameter this sink takes its floor from, or null for a
         *  sink whose floor is fixed at construction.
         *
         *  The NAME, not the value: a writer cannot reach the variable bank and
         *  has no business holding a pointer into it. Vars::Params resolves it
         *  and calls set_min_level(), which is the same pass that already
         *  applies the logger's own floor - so one place decides how much this
         *  unit logs, and every sink follows a row an operator can edit. */
        const char* _level_param;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief @p name is what the logger's loss report calls this sink;
         *  null leaves it unnamed. @p level_param names the parameter its floor
         *  follows, or null to keep whatever set_min_level() was last given. */
        explicit LogWriter(const char* name = nullptr,
                           const char* level_param = nullptr)
            : _sink_name(name), _level_param(level_param) {}

        /** @} */

        /** @name Identity
         *  @{ */

        /** @brief The name a loss report uses. NOT `name()`: a sink is also a
         *  Component or a Page in every case that has one, and both already
         *  spell that `name()` - one more would be ambiguous at every call. A
         *  sink holding a name of its own overrides this rather than storing it
         *  twice. */
        virtual const char* sink_name() const { return _sink_name; }

        /** @brief The parameter this sink's floor follows, or null. */
        const char* level_param() const { return _level_param; }

        /** @} */

        /** @name Queue interface
         *  @{ */

        /** @brief Enqueue a new log entry for deferred output */
        virtual void    push(const Logs::LogEntry* entry) = 0;
        /** @brief Attempt to flush enqueued entries to the output medium */
        virtual void    drain() = 0;
        /** @brief Cumulative entries lost at this sink, for loss reporting. */
        virtual uint32_t loss_count() const { return 0; }

        /** @} */

        /** @name Level gate
         *  @{ */

        /** @brief Set this sink's floor. */
        void set_min_level(Logs::LogLevel level) { _min_level = level; }
        /** @brief This sink's floor. */
        Logs::LogLevel min_level() const { return _min_level; }

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
        /** @name Lifecycle
         *  @{ */

        /** @brief @p name is the sink's name in a loss report (@see
         *  LogWriter::sink_name). */
        explicit BufferedLogWriter(const char* name = nullptr,
                                   const char* level_param = nullptr)
            : LogWriter(name, level_param) {}

        /** @} */

        /** @name Queue interface
         *  @{ */

        /** @brief Enqueue @p entry, unless it is below this sink's _min_level.
         *  The last LOG_QUEUE_CRITICAL_RESERVE slots are kept for LOG_CRITICAL:
         *  a normal that would intrude is dropped (counted); a critical seats
         *  into the reserve, or evicts the oldest only if the queue is wholly
         *  full. */
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
         *  that needs its own wire shape overrides this. */
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
     *  drain().
     *
     *  Also a Component, and the one that OWNS the wire: its single task takes
     *  the port's bytes in, drains the queue into the TX ring, and pushes the
     *  ring out. Nothing consumes what the port receives - the read only keeps
     *  the device's FIFO moving - so there is nothing to split across the beat
     *  and no reason for the port to be a task of its own. */
    class SerialLogWriter : public BufferedLogWriter, public Botanik::Component {
    public:
        /** @brief One task: the whole port, in PHASE_PRESENT. Named after the
         *  writer, so the TASKS row and the boot line say the same thing. */
        void register_tasks() override;

    private:
        /** @brief The registration handler. A static MEMBER, so the cast has
         *  access to the protected hook and stays beside the class. */
        static void _present_task(Component* c, const Tasks::Tick& t);

    protected:
        using BufferedSerial = Utils::BufferedSerial;

    private:
        /** @name State
         *  @{ */

        BufferedSerial* _serial;   ///< bound output port
        /** @brief Drop every entry when the port is an interactive terminal, so
         *  log lines cannot garble a console UI sharing the same tty. Decided
         *  once at construction; the monitor UART is never a tty on hardware. */
        bool _suppressed;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Bind the writer to @p serial. @p drop_if_tty self-suppresses
         *  on an interactive terminal (see @c _suppressed). @p logger / @p name
         *  feed the Component base; a null logger stays silent.
         *  @param sink what a loss report calls this writer, which is NOT @p
         *  name: that one is the task's, spelled lowercase_snake beside every
         *  other task, while this is the operator's label for a wire (MONITOR),
         *  spelled like the page sinks it appears next to. Null takes @p name.
         */
        SerialLogWriter(const SetupContext& ctx, BufferedSerial& serial,
                        bool drop_if_tty = false,
                        const char* name = "SerialLogWriter",
                        const char* sink = nullptr,
                        const char* level_param = nullptr);

        /** @} */

        /** @name Queue interface
         *  @{ */

        /** @brief Enqueue @p entry, or drop it when suppressed. */
        void push(const Logs::LogEntry* entry) override;

        /** @} */

    protected:
        /** @name Stepping
         *  @{ */

        /** @brief The whole port for this beat: bytes in, the queue drained into
         *  the TX ring, the ring out (@p now_ms unused). Same order the two
         *  half-tasks ran in, now that there is nothing in between them. */
        void on_present(uint64_t now_ms) override;

        /** @} */

    private:
        /** @name Channel implementation
         *  @{ */

        /** @brief Format a log entry into @p buf; returns the bytes written */
        size_t format(const Logs::LogEntry* entry, char* buf, size_t len) override;
        /** @brief Write @p len bytes to the bound serial port (never blocks) */
        size_t write(const char* buf, size_t len) override;

        /** @} */
    };

}}

#endif // LOGS_WRITER_H
