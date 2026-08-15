// REVIEW_35: TODO
#ifndef CONSOLE_KILL_RING_H
#define CONSOLE_KILL_RING_H

/**
 * @file kill_ring.h
 * @brief The console's clipboard: one buffer every editor and table cuts into.
 *
 * ONE per Console (Console::kill_ring()), not per TextEditor and not global, so
 * text moves between the places that hold it. See @ref dev_editor.
 *
 * Single entry, replace on kill. What does not fit is not taken (see set()), so
 * a yank restores exactly what the kill removed.
 */

#include <cstdint>

#include "libbotanik/config/terminal.h"

namespace Botanik { namespace Terminal {

    /** @brief The console-wide clipboard (see file header). */
    class KillRing {

        /** @name State
         *  @{ */

        char     _buf[Config::KILL_RING_SIZE]; /**< The killed text, no NUL. */
        uint16_t _len = 0;                     /**< Bytes held. */

        /** @} */

    public:

        /** @brief Replace the contents with the first bytes of @p buf that fit.
         *  @return how many were stored, which is what the caller may remove
         *  from its own buffer. A zero-length store empties the ring. */
        uint16_t set(const char* buf, uint16_t len);

        /** @brief The killed text (not NUL-terminated - pair with len()). */
        const char* text() const { return _buf; }

        /** @brief Bytes held. */
        uint16_t len() const { return _len; }

        /** @brief Drop the contents. */
        void clear() { _len = 0; }
    };

}}

#endif // CONSOLE_KILL_RING_H
