// REVIEW_35: TODO
#ifndef UTILS_BYTE_SINK_H
#define UTILS_BYTE_SINK_H

/**
 * @file byte_sink.h
 * @brief ByteSink - the abstract byte destination the escape-code layer and the
 *        transaction queue emit through.
 *
 * The VT device (AnsiTerminal), the stateless Ansi helpers, and the TxScheduler
 * write to a ByteSink rather than the concrete BufferedSerial, so their output
 * can be redirected: to the TX ring in normal operation, or to a scratch buffer
 * while a transaction is being recorded. BufferedSerial implements it; the
 * terminal owns a small record-buffer sink for tx_begin()/tx_commit().
 */

#include <cstdint>

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
