/**
 * @file test_plc.cpp
 * @brief The ST binding: what a program gets for a name it declares, and what
 *        Plant::wire() can see about it.
 */

#include "doctest.h"

#include "libbotanik.h"
#include "libbotanik/plc/runtime/resolve.h"

#include "site_bank.h"

using namespace Botanik;

TEST_CASE("plc: a name no table declares counts as unresolved") {
    Vars::Bank bank;
    TestSite::build(bank);
    const uint16_t before = bank.unresolved_count();

    // matiec upper-cases every identifier, so this is the spelling that
    // reaches resolve_ci().
    Vars::Ref<float> r = Plc::Runtime::resolve_ci<float>(bank, "NO_SUCH_ROW");

    INFO("the count is the ONLY thing Plant::wire() sees a bad binding by, so "
         "a miss that does not move it leaves the program HEALTHY and running");
    CHECK(bank.unresolved_count() == before + 1);
    CHECK(r.r.index == Vars::VAR_NONE);

    INFO("and the reference carries a bank, so the sink is reachable at all");
    REQUIRE(r.io != nullptr);
    CHECK(r.get() == doctest::Approx(0.0f));
    CHECK_FALSE(r.valid());
    r.drive(1.0f);          // the generated __SET_EXTERNAL, on a row that is absent
}

TEST_CASE("plc: a row bound at the wrong type counts as unresolved too") {
    Vars::Bank bank;
    TestSite::build(bank);
    const uint16_t before = bank.unresolved_count();

    // A real row read as the wrong type: right name, wrong union arm.
    Vars::Ref<bool> r = Plc::Runtime::resolve_ci<bool>(bank, "SYS_NOW_MS");

    CHECK(bank.unresolved_count() == before + 1);
    CHECK(r.r.index == Vars::VAR_NONE);
    REQUIRE(r.io != nullptr);
}

TEST_CASE("plc: a name spelled in another case is the same row") {
    Vars::Bank bank;
    TestSite::build(bank);
    const uint16_t before = bank.unresolved_count();

    Vars::Ref<uint32_t> r = Plc::Runtime::resolve_ci<uint32_t>(bank, "SYS_NOW_MS");

    CHECK(bank.unresolved_count() == before);
    CHECK(r.r.index != Vars::VAR_NONE);
}
