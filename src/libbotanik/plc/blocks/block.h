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
 * just been constructed. A program calls it on the blocks it chooses on a first
 * control-authority withdrawal, and a program's reset is the sum of its blocks',
 * so a block without one is state that survives an outage it should not have.
 * That is why this is enforced rather than documented.
 *
 * `preset()` is deliberately NOT part of the contract. It means "declare the
 * state without a reading", and its argument is whatever the block holds - a
 * bool, a float, a double, a position - so a uniform signature would have to lie
 * about at least one. Blocks that can be preset say so individually.
 *
 * @section blk_novirtual Why the interface is checked, not dispatched
 *
 * `reset()` is not virtual and `Block` has no vtable: nothing ever holds a
 * `Block*`, since blocks are value members of a `Plc::Program` named directly by
 * the control law. So the base carries the family membership and
 * BOTANIK_PLC_BLOCK() carries the interface, as three static_asserts per block.
 */

#include <type_traits>

namespace Botanik { namespace Plc {

    /** @brief Base of every function block. Carries no state and no vtable: it
     *  marks the family and gives BOTANIK_PLC_BLOCK() something to check.
     *
     *  Empty, so the empty-base optimisation applies and a derived block is the
     *  size of its own members (`tests/test_plc.cpp` pins that). The special
     *  members are protected: not a thing to instantiate, and the non-virtual
     *  destructor is not a thing to delete through. */
    class Block {
    protected:
        Block() = default;
        ~Block() = default;
    };

}}  // namespace Botanik::Plc

/** @brief Assert that @p T is a well-formed function block. Place it at
 *  namespace scope directly after the class.
 *
 *  Two claims: it is in the family, and it costs no vptr (@ref blk_novirtual).
 *
 *  It used to claim a third - that T has a callable reset() - through a C++11
 *  detection idiom. That went with the template: a block whose reset is a no-op
 *  is a legitimate block (a stateless one has nothing to drop), so the check
 *  was asserting a convention rather than a requirement. What actually needs
 *  reset() is a program that CALLS it, and that is a compile error at the call
 *  site, where it names the block that is missing one. */
#define BOTANIK_PLC_BLOCK(T)                                                   \
    static_assert(::std::is_base_of< ::Botanik::Plc::Block, T >::value,        \
                  #T " must derive from Plc::Block");                          \
    static_assert(!::std::is_polymorphic< T >::value,                          \
                  #T " must not be polymorphic: a vptr on every block instance")

#endif // PLC_BLOCKS_BLOCK_H
