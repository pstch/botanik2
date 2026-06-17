#ifndef PAGES_LINE_BUFFER_H
#define PAGES_LINE_BUFFER_H

/**
 * @file line_buffer.h
 * @brief LineBuffer<L,N> - a fixed-capacity circular buffer of display lines.
 *
 * The storage primitive under @ref Botanik::Pages::Scrollback: holds the most
 * recent @p L lines (each up to @p N bytes), with soft-wrap splitting on store and
 * newest-first access by distance. It owns no rendering; Scrollback layers the
 * throttled replay on top. Instantiate behind a typedef, not at use sites.
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "libbotanik/utils/text.h"

namespace Botanik { namespace Pages {

    /** @brief Circular buffer of up to @p L lines, each up to @p N bytes. */
    template<uint16_t L, uint16_t N>
    class LineBuffer {
    protected:
        /** @name Circular line buffer
         *  @{ */

        /** @brief Stored line bytes (newest at _head-1). */
        char     _buf[L][N];
        /** @brief Per-line lengths, parallel to _buf. */
        uint16_t _lens[L] = {};
        /** @brief Next slot to write. */
        uint16_t _head  = 0;
        /** @brief Number of stored lines (<= L). */
        uint16_t _count = 0;

        /** @} */

    public:
        /** @name Line storage
         *  @{ */

        /** @brief Store a line in the circular buffer (does not display it). */
        void store(const char* buf, size_t len) {
            uint16_t store_len = (uint16_t)std::min(len, (size_t)N);
            memcpy(_buf[_head], buf, store_len);
            _lens[_head] = store_len;
            _head = (uint16_t)((_head + 1) % L);
            if (_count < L) _count++;
        }

        /** @brief Store a message as one or more lines no wider than @p cols,
         *  breaking on embedded newlines and on width. Continuation lines get the
         *  @p prefix marker; each line becomes its own entry (one entry per display
         *  row).
         *  @return The number of lines stored (always >= 1). */
        uint16_t store_wrapped(const char* buf, size_t len, uint16_t cols,
                               const char* prefix, uint16_t plen) {
            const uint16_t seg_cap = N;
            char seg[N];

            uint16_t count = 0;
            for (size_t off = 0; ; ) {
                uint16_t prefix_len = (count == 0 || cols <= plen) ? 0 : plen;
                if (prefix_len >= seg_cap) prefix_len = 0;  // a prefix too big for the segment is dropped, not underflowed
                // Per-row byte budget: fill to the width, or everything left when
                // unbounded (cols == 0), but never overflow the segment buffer.
                uint16_t budget = (cols == 0) ? (uint16_t)(len - off)
                                              : (uint16_t)(cols - prefix_len);
                if (budget > (uint16_t)(seg_cap - prefix_len)) budget = (uint16_t)(seg_cap - prefix_len);

                // Take up to `budget` bytes, stopping early at a newline.
                uint16_t take = Utils::scan_line_segment(buf, off, len, budget);

                if (prefix_len) memcpy(seg, prefix, prefix_len);
                memcpy(seg + prefix_len, buf + off, take);
                store(seg, (size_t)(prefix_len + take));
                count++;

                off += take;
                if (off < len && buf[off] == '\n') off++;  // consume the break, don't print it
                if (off >= len) break;
            }
            return count;
        }

        /** @brief Forget all stored lines. */
        void clear() {
            _head  = 0;
            _count = 0;
        }

        /** @brief Number of stored lines. */
        uint16_t count() const { return _count; }

        /** @brief Access a stored line by distance from newest (1 = most recent).
         *  @param from_end Steps back from the newest line (1-based)
         *  @param len Set to the line's length on success
         *  @return Pointer to the line bytes, or nullptr if @p from_end is out of range */
        const char* line(uint16_t from_end, uint16_t& len) const {
            if (from_end == 0 || from_end > _count) return nullptr;
            uint16_t idx = (uint16_t)((_head + L - from_end) % L);
            len = _lens[idx];
            return _buf[idx];
        }

        /** @} */
    };

}}

#endif // PAGES_LINE_BUFFER_H
