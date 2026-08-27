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

    /** @brief Which IEC 61131-3 declaration section a program reads this row
     *  through. Read-only, produced, or shared.
     *  @note A PROGRAM POU may connect a formal parameter to a global or name
     *  the global directly; both spellings land on the same three modes, so
     *  VAR_EXTERNAL CONSTANT is IN and VAR_EXTERNAL is OUT. */
    enum class Dir : uint8_t {
        IN,      ///< VAR_INPUT / VAR_EXTERNAL CONSTANT: the program may not write it
        OUT,     ///< VAR_OUTPUT / VAR_EXTERNAL: this program is the row's one producer
        INOUT    ///< VAR_IN_OUT: read-modify-write, and shared by declaration
    };

    /** @brief A typed row reference: @p T is the row's value type, @p D its
     *  declaration section.
     *
     *  The index still resolves at bind time. Bank::freeze() may compact a
     *  redeclared row out, so a reference fixed at compile time would be wrong
     *  by construction; what is compile-time here is the TYPE and the
     *  DIRECTION. Default-constructs to VAR_NONE, which reads the inert sink.
     *  @see Bank::resolve_in() */
    template <typename T, Dir D>
    struct Ref {
        VarRef r;
        /** @brief The table this row lives in, remembered at bind time so a
         *  program reads and writes its interface the way ST does - `x.get()`,
         *  `y.set(v)` - rather than routing every access through the bank.
         *  Borrowed and never null after resolve; null on a default-constructed
         *  reference, which reads the sink. */
        Bank* io;

        Ref() : io(nullptr) { r.index = VAR_NONE; }
        Ref(VarRef v, Bank* b) : r(v), io(b) {}

        /** @name Reading and writing
         *  Defined out of line in model.h, where Bank is complete.
         *  @{ */

        /** @brief The value in force. Every direction reads: a POU may read
         *  its own output. */
        T get() const;
        /** @brief Drive it, saying whether the value is CURRENT. There is no
         *  one-argument form: the law runs the same either way, and the
         *  quality is the half a reader cannot recover from the number.
         *  VAR_INPUT does not compile.
         *  @note CONST: writing through a reference changes the TABLE, not the
         *  reference, exactly as writing through a pointer does. */
        void set(T v, bool current) const;

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

        /** @brief EDIT a parameter row: checked against its band, marked
         *  unsaved, and written to storage. What a program uses for the state
         *  it has to keep - a watering date, a spent one-shot force - where
         *  set() writes the value and leaves flash owing nothing.
         *  @return false if the band refused it. VAR_IN_OUT only: an edit is a
         *  read-modify-write of a row somebody else also writes. */
        bool edit(T v) const;
        /** @brief Say the value is not knowable AND that there is nothing
         *  behind it: the row falls back to its declared default. For a row
         *  that has never had a value at all. VAR_INPUT does not compile. */
        void invalidate() const;

        /** @brief Say the value is not current while KEEPING it: the law had
         *  nothing new to derive, so the last answer stands, marked
         *  not-believable. To keep deriving and mark the RESULT, drive it with
         *  @c set(v,false) instead.
         *
         *  What a law does when its inputs go: a reader that checks quality
         *  learns the number is not to be acted on, and one that does not gets
         *  the last real answer rather than a declared default - which reads
         *  like a measurement of zero and is how a dead probe becomes a
         *  greenhouse at 0 degC. */
        void hold() const;

        /** @} */
    };

    template <typename T> using RefIn    = Ref<T, Dir::IN>;
    template <typename T> using RefOut   = Ref<T, Dir::OUT>;
    template <typename T> using RefInOut = Ref<T, Dir::INOUT>;

    /** @brief Whether two types are the same, so a mismatched drive() names the
     *  problem instead of reporting a deduction conflict. Hand-rolled: three
     *  lines against a whole `<type_traits>`. */
    template <typename A, typename B> struct SameType      { static const bool value = false; };
    template <typename A>             struct SameType<A, A> { static const bool value = true; };

}}  // namespace Botanik::Vars

#endif // VARS_REF_H
