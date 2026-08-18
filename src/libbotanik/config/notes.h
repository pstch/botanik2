#ifndef LIBBOTANIK_CONFIG_NOTES_H
#define LIBBOTANIK_CONFIG_NOTES_H

/** @file notes.h
 *  @brief The operator notes buffer (BOTANIK_FEATURE_NOTES_PAGE).
 *
 *  Its own concern, like every other optional subsystem's: only pages/notes
 *  reads these, and a build without the page pays nothing for them. The two
 *  bounds it shares with the editor are asserted here, at the consumer that
 *  makes the constraint exist.
 */

#include "libbotanik/config/base.h"
#include "libbotanik/config/terminal.h"

// Defined and undefined per file, not in base.h: see base.h.
#define BOTANIK_CFG(type, name) static constexpr type name = BOTANIK_##name

namespace Botanik { namespace Config {

    /** @name Operator notes
     *  @{ */

    /** @brief Capacity of the NOTES buffer (bytes, NUL included) */
#ifndef BOTANIK_NOTES_LEN
#  define BOTANIK_NOTES_LEN 2048
#endif
    BOTANIK_CFG(uint16_t, NOTES_LEN);

    /** @brief Rows the NOTES editor can lay out. Sized past what soft-wrap
     *  needs (see the static_assert below), so only a note made of very short
     *  hard-broken lines can reach it - and an insert that would is refused. */
#ifndef BOTANIK_NOTES_MAX_ROWS
#  define BOTANIK_NOTES_MAX_ROWS 128
#endif
    BOTANIK_CFG(uint16_t, NOTES_MAX_ROWS);

    // Same bound as SHELL_INPUT_LEN: a row carries a prompt, so its worst case
    // is the narrowest screen minus the widest gutter.
    static_assert(NOTES_MAX_ROWS * (EDITOR_MIN_COLS - EDITOR_PROMPT_MAX) >= NOTES_LEN,
                  "NOTES_MAX_ROWS too small to lay out NOTES_LEN at EDITOR_MIN_COLS");

    /** @} */

} }

#undef BOTANIK_CFG

#endif // LIBBOTANIK_CONFIG_NOTES_H
