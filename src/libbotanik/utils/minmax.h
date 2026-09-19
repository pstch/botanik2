#ifndef UTILS_MINMAX_H
#define UTILS_MINMAX_H

/**
 * @file minmax.h
 * @brief Utils::min / Utils::max - the two <algorithm> entries this tree uses.
 *
 * Not <algorithm>: that header is 11k preprocessed lines, seventeen files here
 * named it, and between them they called two functions. This one includes
 * nothing, so a header may take it without charging its includers.
 */

namespace Botanik { namespace Utils {

    /** @brief The smaller of @p a and @p b; @p a when they compare equal. */
    template <typename T>
    inline const T& min(const T& a, const T& b) { return b < a ? b : a; }

    /** @brief The larger of @p a and @p b; @p a when they compare equal. */
    template <typename T>
    inline const T& max(const T& a, const T& b) { return a < b ? b : a; }

}}  // namespace Botanik::Utils

#endif // UTILS_MINMAX_H
