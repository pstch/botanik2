/**
 * @file test_hal_memory.cpp
 * @brief Tests for HAL::Memory's ordering primitives.
 *
 * These exist mainly to *instantiate* ordered_copy() / ordered_move(). Both are
 * templates with no caller in the firmware - they are kept for publishing state
 * to the M4 core - and an uninstantiated template is only parsed, never
 * type-checked. Without this file a typo in either would sit undetected until
 * the day someone needs them.
 *
 * The barrier itself is unobservable on the host (the simulator backend maps it
 * to a seq_cst fence), so what is asserted is the data contract: the payload
 * lands, and the word-multiple / no-alias preconditions are the ones the OPTA
 * backend's ARM_MPU_OrderedMemcpy actually requires.
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

TEST_CASE("HAL::Memory::ordered_copy publishes the whole block") {
    const Block src = { 7, 0x11111111u, 0x22222222u, 0x33333333u };
    Block dst = { 0, 0, 0, 0 };

    HAL::Memory::ordered_copy(&dst, &src);

    CHECK(dst.seq == 7u);
    CHECK(dst.a == 0x11111111u);
    CHECK(dst.b == 0x22222222u);
    CHECK(dst.c == 0x33333333u);
}

TEST_CASE("HAL::Memory::ordered_copy on an empty type is a no-op") {
    // The `if (is_empty<T>)` early-out must compile and not touch memory: a
    // zero-word ARM_MPU_OrderedMemcpy would be a pointless call at best.
    const Empty src;
    Empty dst;
    HAL::Memory::ordered_copy(&dst, &src);
    CHECK(sizeof(Empty) >= 1);   // the call above compiled; nothing to observe
}

TEST_CASE("HAL::Memory::ordered_move stores a scalar") {
    uint32_t dst = 0;
    HAL::Memory::ordered_move(&dst, (uint32_t)0xDEADBEEFu);
    CHECK(dst == 0xDEADBEEFu);

    // The struct form: pass-by-value, so the source may be a temporary.
    Block bdst = { 0, 0, 0, 0 };
    HAL::Memory::ordered_move(&bdst, Block{ 1, 2, 3, 4 });
    CHECK(bdst.seq == 1u);
    CHECK(bdst.c == 4u);
}

TEST_CASE("HAL::Memory::ordered_read reads through volatile") {
    volatile uint32_t cell = 0xA5A5A5A5u;
    CHECK(HAL::Memory::ordered_read(&cell) == 0xA5A5A5A5u);

    cell = 0u;
    CHECK(HAL::Memory::ordered_read(&cell) == 0u);
}

TEST_CASE("HAL::Memory cache maintenance leaves the data alone") {
    // No-ops on the host, and on the OPTA a clean/invalidate pair around a
    // region we own must be value-neutral. What this pins is that neither call
    // is destructive to the caller's data on the simulator path the tests run -
    // and, like the rest of this file, that both still compile with a caller.
    alignas(32) Block region = { 5, 6, 7, 8 };

    HAL::Memory::cache_clean(&region, sizeof(region));
    CHECK(region.seq == 5u);
    CHECK(region.c == 8u);

    HAL::Memory::cache_invalidate(&region, sizeof(region));
    CHECK(region.seq == 5u);
    CHECK(region.c == 8u);
}
