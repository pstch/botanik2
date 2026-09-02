#ifndef VARS_REF_H
#define VARS_REF_H

/**
 * @file ref.h
 * @brief How a row is addressed: the bare index, and the typed reference a
 *        control program declares its interface with.
 *
 * `Ref<T, D>` carries the row's C++ type and its IEC 61131-3 declaration
 * section. See docs/dev/scan.md for the mapping.
 */

#include <cstdint>

namespace Botanik { namespace Vars {

    class Bank;

    /** @name Untyped reference
     *  @{ */

    /** @brief A reference to any variable: a bare index into the one table.
     *
     *  A struct rather than a typedef so a stray integer cannot become a
     *  reference by accident. */
    struct VarRef { uint16_t index; };

    /** @brief The "no variable" index. Deliberately NOT 0: a zero-initialised
     *  VarRef - which is how the platform-health alarms spell "this one reads no
     *  variable" - must not alias the first real row. */
    static const uint16_t VAR_NONE = 0xFFFFu;

    /** @} */

    /** @brief A typed row reference: @p T is the row's value type.
     *
     *  The index still resolves at bind time. Bank::freeze() may compact a
     *  redeclared row out, so a reference fixed at compile time would be wrong
     *  by construction; what is compile-time here is the TYPE.
     *
     *  It carried a DIRECTION too - IN, OUT, INOUT, off the IEC declaration
     *  section - which guarded a write to a read-only row at compile time.
     *  Every ST row resolves VAR_IN_OUT, so the guard could not fire: the
     *  section a program declares a row in is checked at generate time now,
     *  against what matiec says the body writes. Default-constructs to
     *  VAR_NONE, which reads the inert sink. @see Bank::resolve() */
    template <typename T>
    struct Ref {
        VarRef r;
        /** @brief The table this row lives in, remembered at bind time so a
         *  program reads and writes its interface the way ST does - `x.get()`,
         *  `y.set(v)` - rather than routing every access through the bank.
         *  Borrowed, and NULL until the reference is bound - which every
         *  accessor checks, so an unbound one reads what an unwritten row
         *  reads and drives nothing. */
        Bank* io;

        Ref() : io(nullptr) { r.index = VAR_NONE; }
        Ref(VarRef v, Bank* b) : r(v), io(b) {}

        /** @name Reading and writing
         *  Defined out of line in model.h, where Bank is complete.
         *  @{ */

        /** @brief The value in force. Every direction reads: a POU may read
         *  its own output. */
        T get() const;
        /** @brief Whether the row carries a believable value. */
        bool valid() const;

        /** @brief The row's OWN value, believable or not - get() without the
         *  declared substitute.
         *
         *  For the one shape the substitute cannot serve: a running absolute,
         *  where a law subtracts two readings of the SAME counter. A constant
         *  put in place of one of them makes the difference arithmetic about
         *  nothing, and a large negative volume is worse than a stale one.
         *  Ask valid() and say so; do not reach for this to dodge the cascade. */
        T held() const;

        /** @brief EDIT a parameter row: checked against its band and OWED to
         *  the retain cadence, which carries it to storage. What a program uses
         *  for the state it has to keep - a watering date, a spent one-shot
         *  force - where set() writes the value and leaves flash owing nothing.
         *  @return false if the band refused it. VAR_IN_OUT only: an edit is a
         *  read-modify-write of a row somebody else also writes. */
        bool edit(T v) const;

        /** @brief Write it, whatever kind of row it turns out to be: set() on
         *  an ordinary row, an EDIT owed to the retain cadence on a parameter.
         *  For a writer with no way to say which it meant - ST has one
         *  assignment and no second argument. A value already in force writes
         *  nothing, so restating one every scan costs no storage. */
        void drive(T v) const;
        /** @brief Say the value is not knowable AND that there is nothing
         *  behind it: the row falls back to its declared default. For a row
         *  that has never had a value at all. VAR_INPUT does not compile. */
        void invalidate() const;


        /** @} */
    };


    /** @brief Whether two types are the same, so a mismatched drive() names the
     *  problem instead of reporting a deduction conflict. Hand-rolled: three
     *  lines against a whole `<type_traits>`. */
    template <typename A, typename B> struct SameType      { static const bool value = false; };
    template <typename A>             struct SameType<A, A> { static const bool value = true; };

}}  // namespace Botanik::Vars

#endif // VARS_REF_H
