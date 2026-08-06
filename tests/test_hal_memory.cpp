/**
 * @file test_hal_memory.cpp
 * @brief Type-check for HAL::Memory's ordering primitives.
 *
 * This exists mainly to *instantiate* ordered_copy() / ordered_move(). Both are
 * templates with no caller in the firmware - they are kept for publishing state
 * to the M4 core - and an uninstantiated template is only parsed, never
 * type-checked. Without this file a typo in either would sit undetected until
 * the day someone needs them.
 *
 * The barrier itself is unobservable on the host (the simulator backend maps it
 * to a seq_cst fence), so the only thing worth asserting is the data contract:
 * the payload lands. One case calls every entry point, which is all the
 * instantiation needs - the empty-type and cache paths used to have cases of
 * their own, asserting respectively that sizeof(Empty) >= 1 and that a pair of
 * host no-ops changed nothing.
 */
#include <cstdint>                    // uint32_t - the word-sized payloads ordered_copy moves

#include "libbotanik/hal/platform.h"  // HAL::Memory::ordered_copy / cache_* - under test

#include "doctest.h"                  // the test framework (vendored single header); last so it sees our types

using namespace Botanik;

namespace {

    // Word-multiple, as both backends' static_assert demands.
    struct Block {
        uint32_t seq;
        uint32_t a;
        uint32_t b;
        uint32_t c;
    };

    struct Empty {};

}  // namespace

TEST_CASE("HAL::Memory: every ordering primitive instantiates and moves its payload") {
    const Block src = { 7, 0x11111111u, 0x22222222u, 0x33333333u };
    Block dst = { 0, 0, 0, 0 };
    HAL::Memory::ordered_copy(&dst, &src);
    CHECK(dst.seq == 7u);
    CHECK(dst.a == 0x11111111u);
    CHECK(dst.c == 0x33333333u);

    // The `if (is_empty<T>)` early-out must compile: a zero-word
    // ARM_MPU_OrderedMemcpy would be a pointless call at best.
    const Empty esrc; Empty edst;
    HAL::Memory::ordered_copy(&edst, &esrc);

    uint32_t word = 0;
    HAL::Memory::ordered_move(&word, (uint32_t)0xDEADBEEFu);
    CHECK(word == 0xDEADBEEFu);

    // The struct form: pass-by-value, so the source may be a temporary.
    Block bdst = { 0, 0, 0, 0 };
    HAL::Memory::ordered_move(&bdst, Block{ 1, 2, 3, 4 });
    CHECK(bdst.seq == 1u);
    CHECK(bdst.c == 4u);

    volatile uint32_t cell = 0xA5A5A5A5u;
    CHECK(HAL::Memory::ordered_read(&cell) == 0xA5A5A5A5u);

    // On the OPTA a clean/invalidate pair around a region we own is
    // value-neutral; on the host both are no-ops. Called for the type-check.
    alignas(32) Block region = { 5, 6, 7, 8 };
    HAL::Memory::cache_clean(&region, sizeof(region));
    HAL::Memory::cache_invalidate(&region, sizeof(region));
    CHECK(region.seq == 5u);
    CHECK(region.c == 8u);
}
