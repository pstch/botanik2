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

    /** @name Typed reference
     *  @{ */

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
        Ref() { r.index = VAR_NONE; }
        explicit Ref(VarRef v) : r(v) {}
    };

    template <typename T> using RefIn    = Ref<T, Dir::IN>;
    template <typename T> using RefOut   = Ref<T, Dir::OUT>;
    template <typename T> using RefInOut = Ref<T, Dir::INOUT>;

    /** @} */

    /** @brief Whether two types are the same, so a mismatched drive() names the
     *  problem instead of reporting a deduction conflict. Hand-rolled: three
     *  lines against a whole `<type_traits>`. */
    template <typename A, typename B> struct SameType      { static const bool value = false; };
    template <typename A>             struct SameType<A, A> { static const bool value = true; };

}}  // namespace Botanik::Vars

#endif // VARS_REF_H
