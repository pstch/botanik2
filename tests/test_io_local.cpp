/**
 * @file test_io_local.cpp
 * @brief The controller's own terminals: LocalInputs and LocalOutputs.
 *
 * The reference site declares NO local rows - everything it has is on the
 * field bus - so nothing in the suite had ever run this code against a real
 * table. These build their own, which is the only way to reach it.
 *
 * The rules being asserted are the ones a mis-wired panel depends on: F_INVERT
 * folded in exactly once per direction, an analog terminal scaled through the
 * same publish_wire() a Modbus row takes, a pulse line counted rather than
 * levelled, and a row that asked for a terminal and got none being COUNTED
 * rather than silently reading its declared value for the life of the run.
 */

#include "doctest.h"

#include "libbotanik.h"
#include "libbotanik/io/local_inputs.h"
#include "libbotanik/io/local_outputs.h"
#include "libbotanik/vars/declare.h"
#include "site_bank.h"

namespace tests { namespace io_local {

using namespace Botanik;
using namespace Botanik::Vars;

#if BOTANIK_FEATURE_VARS

namespace {

/** @brief Calibration for the analog row: 1:1, so the published engineering
 *  value IS the converter count and the assertion is about the path, not the
 *  arithmetic. */
const AnalogCal CAL = {
    SF(0.0f), SF((float)HAL::AnalogIn::FULL_SCALE),   // raw_min, raw_max
    SF(0.0f), SF((float)HAL::AnalogIn::FULL_SCALE),   // eng_min, eng_max
    SF(0.0f), SF((float)HAL::AnalogIn::FULL_SCALE),   // plausibility band
    { VAR_NONE, VAR_NONE, VAR_NONE, VAR_NONE, VAR_NONE, VAR_NONE },
};

/** @brief Device 0 is what makes a row LOCAL, and `reg` is the pin.
 *  @note Every input says `qlt=false`: a companion is the GENERATOR's to emit,
 *  directly after the row it speaks for, and a hand-written table claiming one
 *  would read the next row here as a quality. */
const Descriptor ROWS[] = {
    DI("in_plain",  0, 2, HAL::Pin::IN1, false, 0, /*invert=*/false, "plain contact", "",
       0, /*qlt=*/false),
    DI("in_inv",    0, 2, HAL::Pin::IN2, false, 0, /*invert=*/true,  "active-low contact", "",
       0, /*qlt=*/false),
    AI("in_volts",  &CAL, 0, 4, HAL::Pin::IN3, 1, T_U16, Scalar::F(0.0f), 0, 1, "V",
       "a sensor on a terminal", "", 0, false, /*qlt=*/false),
    PI("in_pulses", /*channel=*/HAL::Pin::IN5, HAL::Pulse::EDGE_RISING, "a meter", "",
       /*qlt=*/false),
    RO("out_relay", 0, HAL::Pin::RELAY_1, /*safe=*/false, /*invert=*/false, "a coil", ""),
    RO("out_inv",   0, HAL::Pin::RELAY_2, /*safe=*/false, /*invert=*/true,  "active-low coil", ""),
    DO("out_lamp",  0, 5, HAL::Pin::LED_1, /*safe=*/true, /*invert=*/false, "a lamp", ""),
};

/** @brief Rows naming terminals that belong to nobody: the refusal path. */
const Descriptor BAD_ROWS[] = {
    DI("in_nowhere",  0, 2, 200, false, 0, false, "not a terminal", "", 0, false),
    AI("volt_nowhere", &CAL, 0, 4, 201, 1, T_U16, Scalar::F(0.0f), 0, 1, "V",
       "not a terminal", "", 0, false, false),
    RO("out_nowhere", 0, 202, false, false, "not a pin", ""),
};

/** @brief A bank over one of the tables above, plus the two components. */
struct Rig {
    Tasks::Registry reg;
    Logs::Logger    log;
    Bank            bank;
    Io::LocalInputs  in;
    Io::LocalOutputs out;

    Rig(const Descriptor* rows, uint16_t n)
        : bank(SetupContext(reg, &log)),
          in(SetupContext(reg, &log), bank),
          out(SetupContext(reg, &log), bank) {
        TestSite::build(bank, rows, n);
        in.setup(0);
        out.setup(0);
    }
    Var var(const char* name) { return bank.var(bank.find(name)); }
};

}  // namespace

TEST_CASE("io in: a level reaches its row, and F_INVERT is folded in once") {
    Rig f(ROWS, (uint16_t)(sizeof(ROWS) / sizeof(ROWS[0])));
    REQUIRE(f.in.refused() == 0);

    HAL::DigitalIn::inject(HAL::Pin::IN1, true);
    HAL::DigitalIn::inject(HAL::Pin::IN2, true);
    f.in.step(1000, Tasks::PHASE_INPUT);

    CHECK(f.var("in_plain").as_bool() == true);
    INFO("the same closed contact reads FALSE on an active-low row");
    CHECK(f.var("in_inv").as_bool() == false);

    HAL::DigitalIn::inject(HAL::Pin::IN1, false);
    HAL::DigitalIn::inject(HAL::Pin::IN2, false);
    f.in.step(2000, Tasks::PHASE_INPUT);
    CHECK(f.var("in_plain").as_bool() == false);
    CHECK(f.var("in_inv").as_bool() == true);
}

TEST_CASE("io in: a terminal with no _qlt companion is valid, always") {
    Rig f(ROWS, (uint16_t)(sizeof(ROWS) / sizeof(ROWS[0])));
    // A local contact has nothing to be unsure about: it is a wire, read
    // directly, with no bus in between that could time out. So these rows get
    // no companion, and the rule for a row without one is that it is valid
    // before the first scan and after it. Asserted because the OPPOSITE would
    // be the surprising thing to inherit from the Modbus side.
    CHECK(f.var("in_plain").valid());
    CHECK(f.var("in_plain").quality_row() == VAR_NONE);

    HAL::DigitalIn::inject(HAL::Pin::IN1, true);
    f.in.step(1000, Tasks::PHASE_INPUT);
    CHECK(f.var("in_plain").valid());
    CHECK(f.var("in_plain").as_bool() == true);
}

TEST_CASE("io in: an analog terminal goes through the row's calibration") {
    Rig f(ROWS, (uint16_t)(sizeof(ROWS) / sizeof(ROWS[0])));

    HAL::AnalogIn::inject(HAL::Pin::IN3, 2048);
    f.in.step(1000, Tasks::PHASE_INPUT);
    // 1:1 calibration, so the engineering value is the count - the point being
    // that it arrived through publish_wire() and not through set().
    CHECK(f.var("in_volts").as_f32() == doctest::Approx(2048.0f));
    INFO("wire() keeps the raw count beside the scaled value");
    CHECK(f.var("in_volts").wire().as_f32() == doctest::Approx(2048.0f));
}

TEST_CASE("io in: a pulse line is counted, not levelled") {
    Rig f(ROWS, (uint16_t)(sizeof(ROWS) / sizeof(ROWS[0])));

    HAL::Pulse::inject(HAL::Pin::IN5, 7);
    f.in.step(1000, Tasks::PHASE_INPUT);
    CHECK(f.var("in_pulses").as<uint32_t>() == 7u);

    // Monotonic across scans: the total is the reading, and turning it into
    // litres is the site's law.
    HAL::Pulse::inject(HAL::Pin::IN5, 5);
    f.in.step(2000, Tasks::PHASE_INPUT);
    CHECK(f.var("in_pulses").as<uint32_t>() == 12u);
}

TEST_CASE("io in: a row naming no terminal is counted, not ignored") {
    Rig f(BAD_ROWS, (uint16_t)(sizeof(BAD_ROWS) / sizeof(BAD_ROWS[0])));
    // Two inputs asked for a terminal nobody owns. Such a row reads its
    // declared value for the life of the run, which is indistinguishable from
    // an open contact - so the count is the only trace it happened.
    CHECK(f.in.refused() == 2);
    CHECK(f.in.count() == 0);
}

TEST_CASE("io out: a row drives its pin, and F_INVERT is folded in once") {
    Rig f(ROWS, (uint16_t)(sizeof(ROWS) / sizeof(ROWS[0])));
    REQUIRE(f.out.refused() == 0);

    f.var("out_relay").set(true);
    f.var("out_inv").set(true);
    f.out.step(1000, Tasks::PHASE_OUTPUT);

    CHECK(HAL::DigitalOut::read(HAL::Pin::RELAY_1) == true);
    INFO("the same demand energises the active-low coil the other way");
    CHECK(HAL::DigitalOut::read(HAL::Pin::RELAY_2) == false);

    f.var("out_relay").set(false);
    f.var("out_inv").set(false);
    f.out.step(2000, Tasks::PHASE_OUTPUT);
    CHECK(HAL::DigitalOut::read(HAL::Pin::RELAY_1) == false);
    CHECK(HAL::DigitalOut::read(HAL::Pin::RELAY_2) == true);
}

TEST_CASE("io out: the pins are at their declared safe state before any law runs") {
    // A valve that must be OPEN when the coil is off declares safe=true, and
    // attach() dropping the pin is not the same thing. on_setup() drives them.
    HAL::DigitalOut::write(HAL::Pin::LED_1, false);
    Rig f(ROWS, (uint16_t)(sizeof(ROWS) / sizeof(ROWS[0])));
    CHECK(HAL::DigitalOut::read(HAL::Pin::LED_1) == true);
    CHECK(HAL::DigitalOut::read(HAL::Pin::RELAY_1) == false);
}

TEST_CASE("io out: a row naming no pin is counted, not ignored") {
    Rig f(BAD_ROWS, (uint16_t)(sizeof(BAD_ROWS) / sizeof(BAD_ROWS[0])));
    // Worse than the input case: the law goes on writing the row and the field
    // never moves, so the only symptom is an actuator that never acts.
    CHECK(f.out.refused() == 1);
    CHECK(f.out.count() == 0);
}

TEST_CASE("io: both components put themselves in the beat, in their own phase") {
    Rig f(ROWS, (uint16_t)(sizeof(ROWS) / sizeof(ROWS[0])));
    f.in.register_tasks();
    f.out.register_tasks();
    bool saw_in = false, saw_out = false;
    for (uint8_t i = 0; i < f.reg.count(); i++) {
        const Tasks::Task& t = f.reg.task(i);
        if (t.phase == Tasks::PHASE_INPUT)  saw_in  = true;
        if (t.phase == Tasks::PHASE_OUTPUT) saw_out = true;
    }
    INFO("input before the laws, output after: the order IS the data flow");
    CHECK(saw_in);
    CHECK(saw_out);
}

#endif // BOTANIK_FEATURE_VARS

}}  // namespace tests::io_local
