#ifndef PLCIO_VARS_H
#define PLCIO_VARS_H

/**
 * @file vars.h
 * @brief PLC shared-memory layout — log protocol structs, sync counters, and
 *        the PlcInput/PlcOutput templates exchanged with the PLC coprocessor.
 *        Layout must match the PLC IDE's generated types.
 */

#include <cstdint>
#include <cstddef>   // offsetof

#include "libbotanik/hal/platform.h"
#include "libbotanik_config.h"

namespace Botanik { namespace PlcIO {

    // These shared-memory structs use explicit padding so their layout is identical
    // whether packed or not — every field already sits at its natural offset, with no
    // implicit padding. That keeps the uint32 cursors readable through plain pointers
    // (no unaligned-access UB) and lets the PLC side mirror the layout byte-for-byte.
    // The static_asserts below pin the expected sizes/offsets so a config change can't
    // silently shift the layout.
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
        uint32_t        timestamp;    // @0
        LogLevel        level;        // @4  (uint8_t)
        LogTopic        topic;        // @5  (uint8_t)
        uint16_t        length;       // @6
        uint32_t        seqNum;       // @8  (4B fills @8..@11; keeps message[] word-aligned)
        char            message[Config::LOG_PLC_MESSAGE_LEN];  // @12 .. @267
    };

    // Expected LogEntry_t size, spelled out: 12-byte header + the message buffer.
    static constexpr size_t LOG_ENTRY_SIZE = 12 + Config::LOG_PLC_MESSAGE_LEN;  // 268
    static_assert(sizeof(LogEntry_t)  == LOG_ENTRY_SIZE, "LogEntry_t layout drift");
    static_assert(sizeof(LogEntry_t)  % 4 == 0,          "LogEntry_t must be a multiple of 4");
    static_assert(alignof(LogEntry_t) == 4,              "LogEntry_t must be 4-byte aligned");

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
        uint32_t            writeIndex;     // kept first so its alignment is independent
                                            // of the buffer size and entry size
        LogEntry_t          logBuffer[Config::LOG_PLC_BUFFER_SIZE];
    };

    // The cursors lead their structs, so the host polls them through plain uint32_t*
    // at a word-aligned offset regardless of the log buffer/message sizes.
    static_assert(offsetof(PlcOutputLogs_t, writeIndex) == 0, "writeIndex must lead PlcOutputLogs_t");
    static_assert(offsetof(PlcInputLogs_t,  readIndex)  == 0, "readIndex must lead PlcInputLogs_t");

    // Combined input/output structs. Data is the user-supplied site-specific payload.
    // No packing: the structs above are explicitly padded so natural alignment already
    // yields the intended layout. Data must be trivially copyable and naturally aligned
    // (and a multiple of 4 for a fully padding-free combined struct) so the PLC IDE's
    // generated types match byte-for-byte; the sketch's static_assert verifies this.
    template<typename Data>
    struct PlcInput {
        PlcInputCounters_t  sync;
        PlcInputLogs_t      logs;
        Data                data;
    };
    template<typename Data>
    struct PlcOutput {
        PlcOutputCounters_t sync;
        PlcOutputLogs_t     logs;
        Data                data;
    };

    // On OPTA, add to your sketch to verify layout matches the PLC IDE's generated types:
    //   static_assert(sizeof(PLCSharedVarsInput_t)   == sizeof(PlcIO::PlcInput<YourInputData_t>));
    //   static_assert(alignof(PLCSharedVarsOutput_t) == alignof(PlcIO::PlcOutput<YourOutputData_t>));

}}

#endif // PLCIO_VARS_H
