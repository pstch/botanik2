#ifndef LOGS_WRITER_H
#define LOGS_WRITER_H

#include <cstdint>
#include <cstring>

#include "libbotanik_config.h"
#include "libbotanik/utils/serial.h"
#include "libbotanik/plcio/vars.h"

namespace Botanik { namespace Logs {

    // Base class for logging output channels. They are then typically
    // implemented through `BufferedLogWriter`
    class LogWriter {
    private:
        using LogEntry_t = PlcIO::LogEntry_t;
    protected:
        // Byte output handler, implemented by derived classes
        virtual size_t  write(const char* buf, size_t len) = 0;
        // Message formatting handler, implemented by derived classes.
        virtual size_t  format(const LogEntry_t* entry, char* buf, size_t len) = 0;
    public:
        // Push a new message to the writer's queue
        virtual void    push(const LogEntry_t* entry) = 0;
        // Drain messages in the writer's queue
        virtual void    drain() = 0;
    };

    // TODO
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
        // Add a new entry to the writer's queue
        void push(const LogEntry_t* entry) override;
        // Attempt to drain entries from the queue and print them
        void drain() override;
    };

    // TODO
    class SerialLogWriter : public LogWriter {
    private:
        using BufferedSerial = Utils::BufferedSerial;
        using LogEntry_t = PlcIO::LogEntry_t;
        BufferedSerial& _serial;
        size_t format(const LogEntry_t* entry, char* buf, size_t len);
        size_t write(const char* buf, size_t len);

    public:
        SerialLogWriter(BufferedSerial& serial);
    };

    // TODO
    class DebugLogWriter : public SerialLogWriter {
    private:
        using BufferedSerial = Utils::BufferedSerial;
    public:
        DebugLogWriter(BufferedSerial& serial);
    };

    // TODO
    class MonitorLogWriter : public SerialLogWriter {
    private:
        using BufferedSerial = Utils::BufferedSerial;
        using LogEntry_t = PlcIO::LogEntry_t;
    protected:
        void push(const LogEntry_t* entry) override;
    public:
        MonitorLogWriter(BufferedSerial& serial): SerialLogWriter(serial) {}
    };

}}

#endif // LOGS_WRITER_H