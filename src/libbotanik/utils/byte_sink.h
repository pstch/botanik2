// REVIEW_35: TODO
#ifndef UTILS_BYTE_SINK_H
#define UTILS_BYTE_SINK_H

/**
 * @file byte_sink.h
 * @brief ByteSink - the minimal "somewhere bytes can go" interface.
 *
 * The stateless Ansi escape helpers write through this rather than naming a
 * concrete port, so the escape-code layer stays independent of the serial
 * implementation. BufferedSerial is the implementer.
 */

#include <cstdint>  // uint16_t

namespace Botanik { namespace Utils {

    /** @brief A destination for bytes: the TX ring, or a capture buffer. */
    class ByteSink {
    public:
        virtual ~ByteSink() {}
        /** @brief Append up to @p len bytes. @return bytes accepted (< len when
         *  the destination filled; never blocks). */
        virtual uint16_t write(const char* buf, uint16_t len) = 0;
        /** @brief Bytes that will be accepted right now. */
        virtual uint16_t availableForWrite() const = 0;
    };

}}

#endif // UTILS_BYTE_SINK_H
