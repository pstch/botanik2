#ifndef PLCIO_VARS_H
#define PLCIO_VARS_H

#include <cstdint>

#include "libbotanik/hal/platform.h"
#include "libbotanik_config.h"

namespace Botanik { namespace PlcIO {

    #pragma pack(push, 1)
    enum LogLevel : uint8_t {
        LOG_DEBUG = 1,       // debugging messages, only printed when debugging mode is enabled
        LOG_INFO = 2,        // informational messages, they don't need any particular attention
        LOG_NOTICE = 4,      // important messages, but not indicating any kind of problem
        LOG_WARNING = 8,     // important messages that indicate a potential problem
        LOG_ERROR = 16,      // error messages, something is happenning that should never happen
        LOG_CRITICAL = 32,   // critical message, something very bad is going to happen soon
    };

    enum LogTopic : uint8_t {
        NONE = 0,
        BOOT = 2,
        NETWORK = 3,
        TIME = 4,
        STATUS = 5,
        PLC_IO = 1,
    };

    // Check that the defined topic count can fit in the LogTopic enum
    static_assert(Config::LOG_TOPIC_MAX < (1 << (sizeof(LogTopic)*8)));

    struct LogEntry_t {
        uint32_t        timestamp;
        LogLevel        level;
        LogTopic        topic;
        uint16_t        length;
        char            message[Config::LOG_PLC_MESSAGE_LEN];
        uint16_t        seqNum;
    };

    // Sync counters (named from the PLC's point of view)
    struct PlcInputCounters_t {
        uint32_t            rxSeqNum;
        uint32_t            txAckNum;
    };
    struct PlcOutputCounters_t {
        uint32_t            txSeqNum;
        uint32_t            rxAckNum;
    };

    // Fixed log-protocol fields, always present regardless of the user's payload type.
    // These form the library's shared log protocol between host and PLC.
    struct PlcInputLogs_t {
        uint32_t            readIndex;      // host's current read position in the PLC log buffer
    };
    struct PlcOutputLogs_t {
        LogEntry_t          logBuffer[Config::LOG_PLC_BUFFER_SIZE];
        uint32_t            writeIndex;     // PLC's current write position in the log buffer
    };
    #pragma pack(pop)

    // Combined input/output structs. Data is the user-supplied site-specific payload.
    // User-supplied Data types must be trivially copyable and have no internal padding
    // to guarantee the combined layout matches the PLC IDE's generated types.
    // Use __attribute__((packed)) rather than #pragma pack: #pragma pack is applied at
    // the point of template instantiation, not definition, making it unreliable here.
    template<typename Data>
    struct __attribute__((packed)) PlcInput {
        PlcInputCounters_t  sync;
        PlcInputLogs_t      logs;
        Data                data;
    };
    template<typename Data>
    struct __attribute__((packed)) PlcOutput {
        PlcOutputCounters_t sync;
        PlcOutputLogs_t     logs;
        Data                data;
    };

    // On OPTA, add to your sketch to verify layout matches the PLC IDE's generated types:
    //   static_assert(sizeof(PLCSharedVarsInput_t)   == sizeof(PlcIO::PlcInput<YourInputData_t>));
    //   static_assert(alignof(PLCSharedVarsOutput_t) == alignof(PlcIO::PlcOutput<YourOutputData_t>));

}}

#endif // PLCIO_VARS_H
