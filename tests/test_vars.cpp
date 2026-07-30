/**
 * @file test_vars.cpp
 * @brief The variable model (vars/): default seeding, the invalid-reads-as-
 *        default rule, staleness expiry and the fail-safe sweep.
 */
#include <cstdint>

#include "doctest.h"

#include "libbotanik/vars/io.h"

using namespace Botanik;
using namespace Botanik::Vars;

namespace {
    // A miniature site: two analog inputs, one analog output, one digital in,
    // one digital out. Descriptors are const, values are not - the whole point.
    const AnalogInVar AI[] = {
        { {"ext_temp", "C", "outside", "", "20.5"},
          {IO_MODBUS_RTU, 3, 0x10, 3}, SCALAR_I16, 0.1f, 0.0f, -40.0f, 85.0f, 5000 },
        { {"no_timeout", "C", "", "", "1.5"},
          {IO_LOCAL, 0, 1, 0}, SCALAR_I16, 1.0f, 0.0f, 0.0f, 100.0f, 0 },
    };
    const AnalogOutVar AO[] = {
        { {"window", "%", "roof window", "", "0"},
          {IO_MODBUS_RTU, 4, 0x20, 6}, SCALAR_U16, 1.0f, 0.0f },
    };
    const DigitalInVar DI[] = {
        { {"tray_full", "", "float switch", "", "0"}, {IO_LOCAL, 0, 2, 0}, false, 5000 },
    };
    const DigitalOutVar DO[] = {
        { {"valve", "", "drain valve", "", "1"}, {IO_LOCAL, 0, 3, 0}, false },
    };

    struct Fixture {
        AnalogValue  aiv[2]{}; AnalogValue  aov[1]{};
        DigitalValue div[1]{}; DigitalValue dov[1]{};
        Io io;
        Fixture() {
            IoTables t;
            t.ai = AI;   t.ai_v = aiv; t.ai_n = 2;
            t.ao = AO;   t.ao_v = aov; t.ao_n = 1;
            t.di = DI;   t.di_v = div; t.di_n = 1;
            t.dout = DO; t.do_v = dov; t.do_n = 1;
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

TEST_CASE("vars: an out-of-range index clamps rather than faulting") {
    Fixture f;
    CHECK(f.io.ai(99).value() == doctest::Approx(1.5f));   // clamped to the last
}

TEST_CASE("vars: a malformed default reads as zero, not a parsed prefix") {
    // "12abc" must not become 12: a broken table entry should be obviously
    // wrong, not subtly plausible.
    const AnalogInVar bad[] = {
        { {"bad", "", "", "", "12abc"}, {IO_LOCAL, 0, 0, 0},
          SCALAR_I16, 1.0f, 0.0f, 0.0f, 100.0f, 0 },
    };
    AnalogValue v[1]{};
    IoTables t; t.ai = bad; t.ai_v = v; t.ai_n = 1;
    Io io(t); io.init();
    CHECK(io.ai(0).value() == doctest::Approx(0.0f));
}
