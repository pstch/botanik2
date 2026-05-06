#ifndef LIBBOTANIK_CONFIG_H 
#define LIBBOTANIK_CONFIG_H

#include <cstdint>
#include <cstring>

namespace Botanik { namespace Config {

    /**
     * @section Performance monitoring
     */


    static constexpr uint32_t TASK_PERIOD_MS = 10;

    static constexpr uint32_t STACK_SCAN_LIMIT = 65536;
    static constexpr uint8_t  STACK_PAINT_BYTE = 0xCD;

    static constexpr float CPU_LOAD_EMA_ATTACK = 0.95f; // fast rise
    static constexpr float CPU_LOAD_EMA_DECAY  = 0.05f; // slow fall

    static constexpr float STACK_USAGE_EMA_ATTACK = 0.95f; // fast rise
    static constexpr float STACK_USAGE_EMA_DECAY  = 0.05f; // slow fall


    /**
     * @section PLC synchronization
     */

    // These definitions should match the PLC log buffer's layout
    static constexpr size_t LOG_PLC_BUFFER_SIZE = 32;
    static constexpr size_t LOG_PLC_MESSAGE_LEN = 255;

    // Interval between unsolicited TX frames sent to keep the PLC link alive
    static constexpr uint32_t PLC_HEARTBEAT_MS = 1000;
    // Time without a TX acknowledgement before the link is considered timed out
    static constexpr uint32_t PLC_TIMEOUT_MS = 5000;
    // Time without any PLC activity before a watchdog fault is declared
    static constexpr uint32_t PLC_WATCHDOG_MS = 30000;

    /**
     * @section Logging parameters
     */

    /**
     * @subsection Global/per-topic rate-Limiting
     */
    
    // Maximum burst size (number of logs allowed in a quick succession)
    static constexpr uint16_t LOG_GLOBAL_BUCKET_BURST = 20;
    // Refill period in milliseconds (determines the steady-state log rate)
    static constexpr uint32_t LOG_GLOBAL_BUCKET_PERIOD_MS = 1000;
    // Maximum burst size per individual topic
    static constexpr uint16_t LOG_PERTOPIC_BUCKET_BURST = 20;
    // Refill period in milliseconds for per-topic buckets
    static constexpr uint32_t LOG_PERTOPIC_BUCKET_PERIOD_MS = 5000;

    /**
     * @subsection Logging structure sizes
     */

    // Maximum number of unique log topics.
    // NOTE: each topic requires ~8 bytes of RAM (255 * 8 = 2040 bytes)
    static constexpr uint16_t LOG_TOPIC_MAX = 255;
    // Maximum number of pointers to registered log writers (Serial, SD, Ethernet, etc.)
    static constexpr uint8_t LOG_WRITERS_MAX = 8;

    // Per-writer in-flight message queue depth
    static constexpr size_t LOG_QUEUE_SIZE = 64;
    // Maximum length of a single formatted log line (bytes)
    static constexpr size_t LOG_FORMATTED_LEN = 255;

    /**
     * @subsection ANSI terminal parameters
     */
    
    static constexpr size_t   ANSI_INPUT_BATCH_SIZE = 32;
    static constexpr uint32_t ANSI_INPUT_BATCH_TIMEOUT_MS = 1000;

} }

#endif // LIBBOTANIK_CONFIG_H 