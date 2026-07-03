// REVIEW_35: TODO
#ifndef PLCIO_VARS_H
#define PLCIO_VARS_H

/**
 * @file vars.h
 * @brief PLC shared-memory layout - log protocol structs, sync counters, and
 *        the PlcInput/PlcOutput templates exchanged with the PLC coprocessor.
 *        Layout must match the PLC IDE's generated types.
 */

#include <cstdint>
#include <cstddef>   // offsetof
#include <type_traits>

#include "libbotanik/hal/platform.h"
// PlcOutputConfig_t - the generic config block
#include "libbotanik/plcio/config.h"
// LogLevel / LogTopic - carried byte-for-byte in the log ring below
#include "libbotanik/logs/topics.h"
#include "libbotanik_config.h"

namespace Botanik { namespace PlcIO {

    // These shared-memory structs use explicit padding so their layout is
    // identical whether packed or not - every field already sits at its natural
    // offset, with no implicit padding. That keeps the uint32 cursors readable
    // through plain pointers (no unaligned-access UB) and lets the PLC side
    // mirror the layout byte-for-byte. The static_asserts below pin the
    // expected sizes/offsets so a config change can't silently shift the
    // layout.

    /** @name Log protocol
     *  @{ */

    // LogLevel / LogTopic / LOG_TOPIC_COUNT now live in logs/topics.h (logging
    // primitives, usable without the PLC); their values remain the wire
    // protocol the log ring below carries byte-for-byte.

    /** @brief One log record in PLC shared memory: 12-byte header + fixed
     *  message buffer. */
    struct LogEntry_t {
        uint32_t        timestamp;    // @0
        Logs::LogLevel  level;        // @4  (uint8_t)
        Logs::LogTopic  topic;        // @5  (uint8_t)
        uint16_t        length;       // @6
        uint32_t        seqNum;       // @8  (4B fills @8..@11; keeps message[] word-aligned)
        char            message[Config::LOG_PLC_MESSAGE_LEN];  // @12 .. @267
    };

    // Expected LogEntry_t size, spelled out: 12-byte header + the message
    // buffer.
    static constexpr size_t LOG_ENTRY_SIZE = 12 + Config::LOG_PLC_MESSAGE_LEN;  // 268
    static_assert(sizeof(LogEntry_t)  == LOG_ENTRY_SIZE, "LogEntry_t layout drift");
    static_assert(sizeof(LogEntry_t)  % 4 == 0,          "LogEntry_t must be a multiple of 4");
    static_assert(alignof(LogEntry_t) == 4,              "LogEntry_t must be 4-byte aligned");

    /** @} */

    /** @name Sync counters
     *  @{ */

    /** @brief PLC input-region sync counters (named from the PLC's point of
     *  view). */
    struct PlcInputCounters_t {
        uint32_t            rxSeqNum;
        uint32_t            txAckNum;
    };

    /** @brief PLC output-region sync counters (named from the PLC's point of
     *  view). */
    struct PlcOutputCounters_t {
        uint32_t            txSeqNum;
        uint32_t            rxAckNum;
    };

    /** @} */

    /** @name Log buffers
     *  @{ */

    /** @brief Host->PLC log-protocol fields (the read cursor). */
    struct PlcInputLogs_t {
        // host's read position in the PLC log buffer
        uint32_t            readIndex;
    };

    /** @brief PLC output log ring: write cursor (kept first) + the fixed log
     *  buffer. */
    struct PlcOutputLogs_t {
        // kept first so its alignment is independent of the buffer size and
        // entry size
        uint32_t            writeIndex;
        LogEntry_t          logBuffer[Config::LOG_PLC_BUFFER_SIZE];
    };

    // The cursors lead their structs, so the host polls them through plain
    // uint32_t* at a word-aligned offset regardless of the log buffer/message
    // sizes.
    static_assert(offsetof(PlcOutputLogs_t, writeIndex) == 0, "writeIndex must lead PlcOutputLogs_t");
    static_assert(offsetof(PlcInputLogs_t,  readIndex)  == 0, "readIndex must lead PlcInputLogs_t");

    /** @} */

    /** @name Combined shared structs
     *  @{ */

    // Combined input/output structs. Data is the user-supplied site-specific
    // payload. No packing: the structs above are explicitly padded so natural
    // alignment already yields the intended layout. Data must be trivially
    // copyable and naturally aligned (and a multiple of 4 for a fully
    // padding-free combined struct) so the PLC IDE's generated types match
    // byte-for-byte; the sketch's static_assert verifies this.
    //
    // alignas(32): the host (Cortex-M7) does D-cache clean/invalidate by
    // address over these regions, so each must own its cache lines end to end.
    // alignas(32) gives a 32-byte-aligned base and rounds sizeof up to a
    // multiple of 32, so no neighbour ever shares a boundary line. The PLC
    // IDE's generated types must match this.
    /** @brief Host->PLC shared region: sync counters, the log read cursor, and
     *  the site payload. */
    template<typename Data>
    struct alignas(32) PlcInput {
        // alignas(32) already rounds sizeof to a 32-byte multiple; the contract
        // worth pinning per-site is that the payload is word-aligned, so the
        // combined struct is padding-free and ordered_copy's word-multiple
        // requirement holds.
        static_assert(std::is_empty<Data>::value || sizeof(Data) % sizeof(uint32_t) == 0,
                      "site Data must be a 4-byte multiple (or empty)");
        PlcInputCounters_t  sync;
        PlcInputLogs_t      logs;
        Data                data;
    };

    /** @brief PLC->host shared region: sync counters, the log ring, the generic
     *  config block, and the site payload.
     *  @note The library-fixed `conf` block (network/NTP/MQTT, plcio/config.h)
     *  sits between the logs and the site `data`. Adding/resizing it shifts the
     *  `data` offset, so it is a coordinated wire-ABI change: the PLC IDE's
     *  generated type must be updated to match
     *  (see @ref md_docs_2PLC__CONFIG). */
    template<typename Data>
    struct alignas(32) PlcOutput {
        static_assert(std::is_empty<Data>::value || sizeof(Data) % sizeof(uint32_t) == 0,
                      "site Data must be a 4-byte multiple (or empty)");
        PlcOutputCounters_t sync;
        PlcOutputLogs_t     logs;
        PlcOutputConfig_t   conf;
        Data                data;
    };

    // On OPTA, add to your sketch to verify the layout matches the PLC IDE's
    // generated types:
    //   static_assert(sizeof(PLCSharedVarsInput_t)   == sizeof(PlcIO::PlcInput<YourInputData_t>));
    //   static_assert(alignof(PLCSharedVarsOutput_t) == alignof(PlcIO::PlcOutput<YourOutputData_t>));

    /** @} */

}}

#endif // PLCIO_VARS_H
