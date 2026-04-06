#ifndef LIBBOTANIK_CONFIG_H 
#define LIBBOTANIK_CONFIG_H

#include <cstdint>
#include <cstring>

namespace Botanik { namespace Config {

    /**
     * @section PLC synchronization
     */

    // These definitions should match the PLC log buffer's layout
    constexpr size_t LOG_PLC_BUFFER_SIZE = 32;
    constexpr size_t LOG_PLC_MESSAGE_LEN = 255;

    // TODO
    constexpr uint32_t PLC_HEARTBEAT_MS = 1000;
    // TODO
    constexpr uint32_t PLC_TIMEOUT_MS = 5000;
    // TODO
    constexpr uint32_t PLC_WATCHDOG_MS = 30000;

    /**
     * @section Logging parameters
     */

    /**
     * @subsection Global/per-topic rate-Limiting
     */
    
    // Maximum burst size (number of logs allowed in a quick succession)
    constexpr uint16_t LOG_GLOBAL_BUCKET_BURST = 20;
    // Refill period in milliseconds (determines the steady-state log rate)
    constexpr uint32_t LOG_GLOBAL_BUCKET_PERIOD_MS = 1000;
    // Maximum burst size per individual topic
    constexpr uint16_t LOG_PERTOPIC_BUCKET_BURST = 20;
    // Refill period in milliseconds for per-topic buckets
    constexpr uint32_t LOG_PERTOPIC_BUCKET_PERIOD_MS = 5000;    

    /**
     * @subsection Logging structure sizes
     */
    
    // Maximum number of unique log topics. 
    // NOTE: each topic requires ~8 bytes of RAM (255 * 8 = 2040 bytes)
    constexpr uint16_t LOG_TOPIC_MAX = 255; 
    // Maximum number of pointers to registered log writers (Serial, SD, Ethernet, etc.)
    constexpr uint8_t LOG_WRITERS_MAX = 8;
     
    // Check that the defined topic count can fit in the LogTopic enum
    static_assert(LOG_TOPIC_MAX < (1 << (sizeof(LogTopic)*8)));

} }

#endif // LIBBOTANIK_CONFIG_H 