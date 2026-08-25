// REVIEW_35: TODO
#ifndef PLC_BLOCKS_BLOCK_H
#define PLC_BLOCKS_BLOCK_H

/**
 * @file block.h
 * @brief Block - the base every function block derives from, and the compile-time
 *        check that each one carries the family's interface.
 * @note The rationale behind this file is @ref blk_contract, @ref blk_novirtual.
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
