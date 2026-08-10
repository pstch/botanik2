// REVIEW_35: TODO
#ifndef PLC_BLOCKS_BLOCK_H
#define PLC_BLOCKS_BLOCK_H

/**
 * @file block.h
 * @brief Block - the base every function block derives from, and the compile-time
 *        check that each one carries the family's interface.
 *
 * @section blk_contract The contract
 *
 * Every FUNCTION_BLOCK provides `reset()`: back to a cold block, as if it had
 * just been constructed. `Scan::Driver` calls `Plc::Program::reset()` on every
 * control-authority withdrawal, and a program's reset is the sum of its blocks'
 * - so a block without one is a piece of state that survives an outage it should
 * not have survived. That is the whole reason this is enforced rather than
 * documented.
 *
 * `preset()` is deliberately NOT part of the contract. It means "declare the
 * state without a reading", and its argument is whatever the block holds - a
 * bool for a threshold, a float for a ramp, a double for a totaliser, a position
 * for an actuator. A uniform signature would have to lie about at least one of
 * them. Blocks that can be preset say so individually.
 *
 * @section blk_novirtual Why the interface is checked, not dispatched
 *
 * `reset()` is not virtual and `Block` has no vtable. Nothing ever holds a
 * `Block*`: blocks are value members of a `Plc::Program`, named directly by the
 * control law, and a greenhouse carries dozens of them. A virtual would add a
 * pointer to every instance and an indirect call to a scan-rate path, to buy a
 * polymorphism no caller wants.
 *
 * So the base carries the family membership and BOTANIK_PLC_BLOCK() carries the
 * interface, as three static_asserts per block. A new block that forgets
 * `reset()` fails to compile with a message saying why, which is what a pure
 * virtual would have bought at a cost this tree should not pay.
 */

#include <type_traits>   // std::is_base_of / std::is_polymorphic

namespace Botanik { namespace Plc {

    /** @brief Base of every function block. Carries no state and no vtable: it
     *  marks the family and gives BOTANIK_PLC_BLOCK() something to check.
     *
     *  Empty, so the empty-base optimisation applies and a derived block is the
     *  size of its own members - `tests/test_plc.cpp` pins that. The special
     *  members are protected: `Block` is not a thing to instantiate, and the
     *  non-virtual destructor is not a thing to delete through. */
    class Block {
    protected:
        Block() = default;
        ~Block() = default;
    };

    namespace detail {
        /** @brief Whether `T` has a callable `reset()`. The C++11 detection
         *  idiom: the first overload survives substitution only when the
         *  expression is well formed. */
        template <class T>
        struct has_reset {
            template <class U>
            static char probe(decltype(static_cast<void>(
                                  static_cast<U*>(nullptr)->reset()), 0));
            template <class U>
            static long probe(...);
            static const bool value = sizeof(probe<T>(0)) == sizeof(char);
        };
    }  // namespace detail

}}  // namespace Botanik::Plc

/** @brief Assert that @p T is a well-formed function block. Place it at
 *  namespace scope directly after the class.
 *
 *  Three claims: it is in the family, it can be reset, and it costs no vptr.
 *  The third is not pedantry - it is the property that lets a program hold
 *  twenty blocks by value without paying for a polymorphism nobody uses. */
#define BOTANIK_PLC_BLOCK(T)                                                   \
    static_assert(::std::is_base_of< ::Botanik::Plc::Block, T >::value,        \
                  #T " must derive from Plc::Block");                          \
    static_assert(::Botanik::Plc::detail::has_reset< T >::value,               \
                  #T " must provide reset(): the scan driver resets every "    \
                  "program, and a program's reset is the sum of its blocks'"); \
    static_assert(!::std::is_polymorphic< T >::value,                          \
                  #T " must not be polymorphic: a vptr on every block instance")

#endif // PLC_BLOCKS_BLOCK_H
