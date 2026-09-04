#ifndef VARS_REF_H
#define VARS_REF_H

/**
 * @file ref.h
 * @brief How a row is addressed: the index, and the typed reference a control
 *        program declares its interface with.
 *
 * A row index is a bare `uint16_t`, generated. `Ref<T>` adds the row's C++ type
 * and reads the runtime's one bank. See docs/dev/scan.md.
 */

#include <cstdint>

namespace Botanik { namespace Vars {

    class Bank;

    /** @brief The "no variable" index. Deliberately NOT 0: a zero-initialised
     *  index - which is how the platform-health alarms spell "this one reads no
     *  variable" - must not alias the first real row.
     *  @warning Zero IS a real row, so never read a resolved index back out of
     *  the flash table it was declared in. */
    static const uint16_t VAR_NONE = 0xFFFFu;

    /** @brief The bank a Ref reads: the firmware's one table, set before
     *  anything generated runs. Null in a build with no bank, which every
     *  accessor below tolerates. */
    extern Bank* runtime_bank;

    /** @brief A typed row reference: @p T is the row's value type.
     *
     *  Two bytes, and the index is a generated constant - `rows.gen.h` gives one
     *  per row, so a name no table declares fails to COMPILE rather than
     *  resolving to a sink at bring-up.
     *
     *  Which SECTION a program declares a row in is checked at generate time,
     *  against what matiec says the body writes: every ST row resolves
     *  VAR_IN_OUT, so a type could not carry the direction. */
    template <typename T>
    struct Ref {
        uint16_t i;

        /** @name Reading and writing
         *  Defined out of line in model.h, where Bank is complete.
         *  @{ */

        /** @brief The value in force, or the row's DECLARED value when nothing
         *  usable is behind it. Every direction reads: a POU may read its own
         *  output. */
        T get() const;
        /** @brief Whether something MEASURED the row. */
        bool valid() const;

        /** @brief Write it, whatever kind of row it turns out to be: this scan's
         *  answer on an ordinary row, an EDIT owed to the retain cadence on a
         *  parameter. ST has one assignment and no second argument, so the ROW
         *  is what decides. A value already in force writes nothing, so
         *  restating one every scan costs no storage. */
        void drive(T v) const;

        /** @} */
    };

    /** @brief Whether two types are the same, so a mismatched drive() names the
     *  problem instead of reporting a deduction conflict. Hand-rolled: three
     *  lines against a whole `<type_traits>`. */
    template <typename A, typename B> struct SameType      { static const bool value = false; };
    template <typename A>             struct SameType<A, A> { static const bool value = true; };

}}  // namespace Botanik::Vars

#endif // VARS_REF_H
