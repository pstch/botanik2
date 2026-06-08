#ifndef UTILS_TEXT_H
#define UTILS_TEXT_H

/**
 * @file text.h
 * @brief Small text helpers shared by the line-wrapping paths in the pages and
 *        shell layers.
 */

#include <cstddef>
#include <cstdint>

namespace Botanik { namespace Utils {

    /** @brief Length of the next display segment of [buf+off, buf+len): up to
     *  @p budget bytes, stopping before an embedded '\n' (which the caller
     *  consumes). This is the inner scan shared by the scrollback / shell
     *  line-wrapping loops; the callers keep their own outer loop because their
     *  per-row budgets and empty-input handling differ. */
    inline uint16_t scan_line_segment(const char* buf, size_t off,
                                      size_t len, uint16_t budget) {
        uint16_t take = 0;
        while (take < budget && off + take < len && buf[off + take] != '\n') take++;
        return take;
    }

}}

#endif // UTILS_TEXT_H
