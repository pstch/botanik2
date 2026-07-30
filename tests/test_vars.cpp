/**
 * @file test_vars.cpp
 * @brief The variable model (vars/): default seeding, the invalid-reads-as-
 *        default rule, staleness expiry and the fail-safe sweep.
 */
#include <cstdint>

#include "doctest.h"

#include "libbotanik/vars/access.h"
#include "libbotanik/vars/io.h"

using namespace Botanik;
using namespace Botanik::Vars;

namespace {
    const char* ups_str(uint8_t v) {
        switch (v) { case 1: return "ONLINE"; case 2: return "BATTERY"; }
        return "OFFLINE";
    }

    // A miniature site: two analog inputs, one analog output, one digital in,
    // one digital out. Descriptors are const, values are not - the whole point.
    const AnalogInVar AI[] = {
        { {"ext_temp", "C", "outside", "", "20.5", 1, nullptr},
          {IO_MODBUS_RTU, 3, 0x10, 3}, SCALAR_I16, 0.1f, 0.0f, -40.0f, 85.0f, 5000 },
        { {"no_timeout", "C", "", "", "1.5", 1, nullptr},
          {IO_LOCAL, 0, 1, 0}, SCALAR_I16, 1.0f, 0.0f, 0.0f, 100.0f, 0 },
    };
    const AnalogOutVar AO[] = {
        { {"window", "%", "roof window", "", "0", 0, nullptr},
          {IO_MODBUS_RTU, 4, 0x20, 6}, SCALAR_U16, 1.0f, 0.0f },
    };
    const DigitalInVar DI[] = {
        { {"tray_full", "", "float switch", "", "0", 0, nullptr}, {IO_LOCAL, 0, 2, 0}, false, 5000 },
    };
    const DigitalOutVar DO[] = {
        { {"valve", "", "drain valve", "", "1", 0, nullptr}, {IO_LOCAL, 0, 3, 0}, false },
    };
    // Computed globals: the descriptor is the bare Variable.
    const Variable IA[] = {
        { "setpoint", "C", "target temp", "", "18.0", 1, nullptr },
        { "ups",      "",  "UPS state",   "", "0",    0, &ups_str },
    };
    const Variable ID[] = {
        { "alarm_hot", "", "over temperature", "", "0", 0, nullptr },
    };

    struct Fixture {
        AnalogValue  aiv[2]{}; AnalogValue  aov[1]{};
        DigitalValue div[1]{}; DigitalValue dov[1]{};
        AnalogValue  iav[2]{}; DigitalValue idv[1]{};
        Io io;
        Fixture() {
            IoTables t = {};
            t.ai = AI;   t.ai_v = aiv; t.ai_n = 2;
            t.ao = AO;   t.ao_v = aov; t.ao_n = 1;
            t.di = DI;   t.di_v = div; t.di_n = 1;
            t.dout = DO; t.do_v = dov; t.do_n = 1;
            t.ia = IA;   t.ia_v = iav; t.ia_n = 2;
            t.id = ID;   t.id_v = idv; t.id_n = 1;
            io = Io(t);
            io.init();
        }
    };
}

TEST_CASE("vars: defaults are parsed once at init and start invalid") {
    Fixture f;
    CHECK(f.io.ai(0).value() == doctest::Approx(20.5f));
    CHECK_FALSE(f.io.ai(0).valid());          // nothing has read it yet
    CHECK(f.io.dout(0).value());              // "1" -> true
    CHECK_FALSE(f.io.dout(0).valid());
}

TEST_CASE("vars: an invalid variable reads as its default") {
    Fixture f;
    f.aiv[0].value = 31.7f; f.aiv[0].valid = true;
    CHECK(f.io.ai(0).value() == doctest::Approx(31.7f));
    f.aiv[0].valid = false;                   // driver lost it
    CHECK(f.io.ai(0).value() == doctest::Approx(20.5f));   // back to default
}

TEST_CASE("vars: writing an output marks it valid; safe() restores the default") {
    Fixture f;
    f.io.ao(0).set(75.0f);
    CHECK(f.io.ao(0).valid());
    CHECK(f.io.ao(0).value() == doctest::Approx(75.0f));
    f.io.ao(0).safe();
    CHECK_FALSE(f.io.ao(0).valid());
    CHECK(f.io.ao(0).value() == doctest::Approx(0.0f));
}

TEST_CASE("vars: stale inputs expire, and timeout 0 never does") {
    Fixture f;
    f.aiv[0].value = 30.0f; f.aiv[0].valid = true; f.aiv[0].timestamp = 1000;
    f.aiv[1].value = 9.0f;  f.aiv[1].valid = true; f.aiv[1].timestamp = 1000;
    f.div[0].value = true;  f.div[0].valid = true; f.div[0].timestamp = 1000;

    f.io.expire_stale(5000);                  // 4 s old, under the 5 s timeout
    CHECK(f.io.ai(0).valid());
    CHECK(f.io.di(0).valid());

    f.io.expire_stale(7000);                  // 6 s old, past it
    CHECK_FALSE(f.io.ai(0).valid());
    CHECK_FALSE(f.io.di(0).valid());
    CHECK(f.io.ai(0).value() == doctest::Approx(20.5f));   // reads default again

    CHECK(f.io.ai(1).valid());                // timeout 0 = never expires
}

TEST_CASE("vars: all_outputs_safe drops every output to its default") {
    Fixture f;
    f.io.ao(0).set(75.0f);
    f.io.dout(0).set(false);
    f.io.all_outputs_safe();
    CHECK(f.io.ao(0).value() == doctest::Approx(0.0f));
    CHECK(f.io.dout(0).value());              // default "1"
    CHECK_FALSE(f.io.ao(0).valid());
}

TEST_CASE("vars: an out-of-range index resolves to an inert sink") {
    Fixture f;
    // NOT clamped to the last entry: clamping aliased a typo onto a real
    // signal, so a bad index would have driven an actual relay.
    CHECK(f.io.ai(99).value() == doctest::Approx(0.0f));
    f.io.dout(99).set(true);                       // absorbed, drives nothing
    CHECK_FALSE(f.io.dout(0).value() == false);    // the real coil is untouched
}

TEST_CASE("vars: indexing an EMPTY table is safe") {
    // gh_mlb41 really is in this shape: the AI-1 / AO-1 modules are wired but
    // unmapped, so its analog-output and digital-input tables are null. Reaching
    // into one must not dereference null.
    IoTables t = {};
    Io io(t);
    io.init();
    CHECK(io.ao(0).value() == doctest::Approx(0.0f));
    CHECK_FALSE(io.di(0).value());
    io.ao(0).set(50.0f);                            // absorbed
    io.all_outputs_safe();                          // and sweeps cleanly
}

TEST_CASE("vars: a malformed default reads as zero, not a parsed prefix") {
    // "12abc" must not become 12: a broken table entry should be obviously
    // wrong, not subtly plausible.
    const AnalogInVar bad[] = {
        { {"bad", "", "", "", "12abc"}, {IO_LOCAL, 0, 0, 0},
          SCALAR_I16, 1.0f, 0.0f, 0.0f, 100.0f, 0 },
    };
    AnalogValue v[1]{};
    IoTables t = {}; t.ai = bad; t.ai_v = v; t.ai_n = 1;
    Io io(t); io.init();
    CHECK(io.ai(0).value() == doctest::Approx(0.0f));
}

TEST_CASE("vars: internals seed from their default and are read/write") {
    Fixture f;
    CHECK(f.io.ia(0).value() == doctest::Approx(18.0f));
    CHECK_FALSE(f.io.ia(0).valid());
    f.io.ia(0).set(21.5f);
    CHECK(f.io.ia(0).valid());
    CHECK(f.io.ia(0).value() == doctest::Approx(21.5f));
    f.io.id(0).set(true);
    CHECK(f.io.id(0).value());
}

TEST_CASE("vars: a fail-safe sweep leaves computed internals alone") {
    // Outputs are actuators and must drop; an internal is derived state (an
    // alarm condition, a state flag) and clearing it would erase the very
    // diagnosis that put the firmware into safe mode.
    Fixture f;
    f.io.ao(0).set(75.0f);
    f.io.ia(0).set(21.5f);
    f.io.id(0).set(true);
    f.io.all_outputs_safe();
    CHECK(f.io.ao(0).value() == doctest::Approx(0.0f));
    CHECK(f.io.ia(0).value() == doctest::Approx(21.5f));
    CHECK(f.io.id(0).value());
}

TEST_CASE("vars: a VarRef resolves against every table") {
    Fixture f;
    f.aiv[0].value = 31.7f; f.aiv[0].valid = true;
    f.io.dout(0).set(false);
    f.io.ia(0).set(21.5f);

    VarRef ai_ref = { VAR_ANALOG_IN, 0 };
    VarRef do_ref = { VAR_DIGITAL_OUT, 0 };
    VarRef ia_ref = { VAR_INTERNAL_ANALOG, 0 };

    CHECK(var_number(f.io, ai_ref) == doctest::Approx(31.7f));
    CHECK(var_number(f.io, ia_ref) == doctest::Approx(21.5f));
    CHECK_FALSE(var_bool(f.io, do_ref));           // digitals read as 0/1
    CHECK(var_view(f.io, do_ref).digital);
    CHECK(var_view(f.io, ai_ref).valid);
}

TEST_CASE("vars: rendering follows the descriptor, not the caller") {
    Fixture f;
    char buf[32];
    f.aiv[0].value = 31.75f; f.aiv[0].valid = true;

    VarRef ai_ref = { VAR_ANALOG_IN, 0 };
    CHECK(Utils::compare_str(var_format(f.io, ai_ref, buf, sizeof(buf)), "31.8"));

    // digits 0 renders as an integer, even though the value is a float
    VarRef ao_ref = { VAR_ANALOG_OUT, 0 };
    f.io.ao(0).set(42.9f);
    CHECK(Utils::compare_str(var_format(f.io, ao_ref, buf, sizeof(buf)), "42"));

    // an enum converter wins over the number
    VarRef ups_ref = { VAR_INTERNAL_ANALOG, 1 };
    f.io.ia(1).set(2.0f);
    CHECK(Utils::compare_str(var_format(f.io, ups_ref, buf, sizeof(buf)), "BATTERY"));
}

TEST_CASE("vars: an unresolvable VarRef renders inertly") {
    // A stale generated table, or a reference to a parameter (which still
    // lives in its own store), must not read a neighbouring table.
    Fixture f;
    char buf[32];
    VarRef bad   = { VAR_DIGITAL_IN, 99 };
    VarRef param = { VAR_PARAM, 0 };
    CHECK(var_number(f.io, bad) == doctest::Approx(0.0f));
    CHECK(var_number(f.io, param) == doctest::Approx(0.0f));
    CHECK(var_view(f.io, param).meta != nullptr);
    CHECK(Utils::compare_str(var_format(f.io, param, buf, sizeof(buf)), "0"));
}
