// REVIEW_35: TODO
#ifndef CONSOLE_KILL_RING_H
#define CONSOLE_KILL_RING_H

/**
 * @file kill_ring.h
 * @brief The console's clipboard: one buffer every editor and table cuts into.
 *
 * Deliberately ONE, not one per TextEditor. The point of a kill ring on a
 * terminal with no mouse is to move text between the places that hold it - cut
 * a line at the shell prompt and yank it into NOTES, copy a variable name off
 * the MONITOR table and paste it into a `get`. A per-editor buffer makes each
 * of those a dead end.
 *
 * One per CONSOLE, though, not one per firmware: the Console owns it
 * (Console::kill_ring()) and everything reaches it through the console it is
 * already holding. There is one console, so that is the same single clipboard -
 * without a global that anything including a header could write to, and with
 * test isolation for free, since a fixture's console brings its own.
 *
 * Single entry, replace on kill: no Emacs-style append across consecutive
 * kills. What does NOT fit is not taken (see set()), which is what lets the
 * editor promise that a yank restores exactly what the kill removed.
 */

#include <cstdint>                        // uint16_t

#include "libbotanik/config/terminal.h"   // Config::KILL_RING_SIZE

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
