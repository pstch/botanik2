#ifndef VARS_HANDLE_H
#define VARS_HANDLE_H

/**
 * @file handle.h
 * @brief The RAM slot: the mutable quarter of a row, parallel to the descriptor
 *        table.
 * @note The const half is descriptor.h. The Bank and the Var handle over the
 *       pair are model.h, which includes this. See @ref dev_vars.
 */

#include <cstdint>

#include "libbotanik/vars/descriptor.h"
#include "libbotanik/vars/scalar.h"

namespace Botanik { namespace Vars {

    /** @brief One value slot, parallel to the descriptor table. No `def`: the
     *  default is a typed literal in flash.
     *  @note While S_OVR_ON, @c ovr SHADOWS @c v, so clearing snaps back to
     *  live. "Read as a dead sensor" is an override on the row's `_qlt`
     *  companion, which is an ordinary force of an ordinary row.
     *  @warning Both halves are PERSISTED - hence the CRITICAL on each restore. */
    struct Slot {
        Scalar   v;
        Scalar   ovr;        ///< forced value, while S_OVR_ON
        /** @brief Low 32 bits of uptime at the last good update. Ages by
         *  UNSIGNED subtraction, so the 49-day wrap costs nothing; what the zero
         *  cannot say is "never written", which is S_WRITTEN. */
        uint32_t stamp_ms;
        /** @brief T_STR only: this row's pool slot, or STR_NO_SLOT when it did
         *  not fit. Laid out at construction, never declared. */
        uint16_t str_off;
        uint8_t  flags;      ///< SlotFlags
        /** @brief Index into the bank's wire array for an analog row, WIRE_NONE
         *  otherwise. */
        uint8_t  wire_off;
    };

    /** @brief This row has no wire value of its own: its value IS what the wire
     *  carries, or it has no binding at all. */
    static const uint8_t WIRE_NONE = 0xFFu;

    // Every field here costs RAM on every row of every site, so the size is
    // pinned rather than left to drift.
    static_assert(sizeof(Slot) == 24, "Slot must stay 24 bytes (RAM per variable)");

    /** @brief A text row that found no room in the pool. Not 0, which is a REAL
     *  slot: an overflowed row parked there keeps its declared capacity and
     *  would be written over the first text row. */
    static const uint16_t STR_NO_SLOT = 0xFFFFu;

    /** @brief What a write attempt did. A failed PERSIST is not a rejected
     *  EDIT: the value is live either way. */
    enum SetResult : uint8_t {
        SET_OK,             ///< applied (set), or written out (save)
        SET_INVALID,        ///< rejected - malformed, out of range, read-only
        SET_NOT_PERSISTED   ///< live in RAM, but storage refused the write
    };

}}  // namespace Botanik::Vars

#endif // VARS_HANDLE_H
