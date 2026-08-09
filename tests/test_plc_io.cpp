/**
 * @file test_plc_io.cpp
 * @brief The program-facing view (plc/io.h): what a control program can reach,
 *        and what it deliberately cannot.
 *
 * Most of the value of Io is in what it does NOT have, and a missing method is
 * not something a runtime test can assert - deleting one would stop this file
 * compiling rather than fail it. So what is checked here is the behaviour of the
 * surface that remains: that reads go through the same override-aware path every
 * other reader uses, and that a write lands on the row and is owed to the field.
 * The narrowing itself is guarded by the compiler.
 */
#include <cstdint>
#include <string>

#include "doctest.h"

#include "libbotanik_site.h"
#include "libbotanik.h"
#include "libbotanik/plc/io.h"

#include BOTANIK_SITE_HEADER

using namespace Botanik;
using namespace Botanik::Vars;
using Botanik::Plc::Io;

namespace {

/** @brief A bank over the site's real tables, running. */
struct Live {
    Bank bank;
    Live() : bank(Site::g_site.vars) {
        bank.init();
        bank.enable_control(Bank::INHIBIT_HEALTH);
        bank.enable_control(Bank::INHIBIT_OPERATOR);
    }
    VarRef ref(const char* name) { return bank.find(name); }
};

}  // namespace

TEST_CASE("plc io: it costs one pointer, so passing it by value is free") {
    // The Context is built per activation. If this grew, every program would
    // pay for the narrowing on every beat.
    CHECK(sizeof(Io) == sizeof(void*));
}

TEST_CASE("plc io: a program reads what the operator forced, not what the wire said") {
    // The whole point of applying an override in Var::raw(): a forced reading
    // has to reach control logic, or forcing one proves nothing about the logic
    // that watches it.
    Live f;
    Io io(f.bank);
    const VarRef r = f.ref("cli_temp_ext1");
    REQUIRE(f.bank.var(r).meta()->name != nullptr);

    f.bank.var(r).publish(from_f32(T_F32, 20.0f), 1000);
    CHECK(io.value(r) == doctest::Approx(20.0f));
    CHECK(io.valid(r));

    f.bank.override_set(f.bank.var(r), from_f32(T_F32, 55.0f), true);
    INFO("the program must see the forced value");
    CHECK(io.value(r) == doctest::Approx(55.0f));

    // ...and "force this to read as a dead sensor" is expressible too.
    f.bank.override_set(f.bank.var(r), from_f32(T_F32, 55.0f), false);
    CHECK_FALSE(io.valid(r));

    f.bank.override_clear(f.bank.var(r));
    CHECK(io.value(r) == doctest::Approx(20.0f));
}

TEST_CASE("plc io: an invalid row reads as its declared default") {
    // So logic that does not care about quality still gets a sane number
    // instead of whatever was in the slot.
    Live f;
    Io io(f.bank);
    const VarRef r = f.ref("cli_temp_ext1");

    f.bank.var(r).invalidate(Q_STALE);
    CHECK_FALSE(io.valid(r));
    CHECK(io.value(r) == doctest::Approx(f.bank.var(r).meta()->def.f));
}

TEST_CASE("plc io: a write lands on the row and is owed to the field") {
    Live f;
    Io io(f.bank);

    // A relay coil: a ROLE_OUTPUT, so the write has to be carried outward.
    VarRef out = { VAR_NONE };
    const VarTables& t = Site::g_site.vars;
    for (uint16_t i = 0; i < t.count; ++i) {
        if (t.var[i].role == ROLE_OUTPUT && t.var[i].type == T_BOOL) {
            out = f.ref(t.var[i].name);
            break;
        }
    }
    REQUIRE(out.index != VAR_NONE);

    f.bank.var(out).clear_field_dirty();
    io.set(out, true);

    INFO("'" << std::string(f.bank.var(out).name()) << "'");
    CHECK(io.on(out));
    CHECK(f.bank.var(out).field_dirty());

    io.set(out, false);
    CHECK_FALSE(io.on(out));
}

TEST_CASE("plc io: a bad reference moves nothing") {
    // A program driving a row that does not exist has to be inert, not land on
    // row zero - which with the parameters leading the table is a real setting.
    Live f;
    Io io(f.bank);
    const VarRef nowhere = f.ref("no_such_row_at_all");

    const Scalar first = f.bank.var(f.bank.find(Site::g_site.vars.var[0].name)).raw();
    io.set(nowhere, 1234.0f);
    const Scalar after = f.bank.var(f.bank.find(Site::g_site.vars.var[0].name)).raw();

    INFO("writing an unresolved reference must not touch the first row");
    CHECK(first.u == after.u);
}
