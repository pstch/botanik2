/**
 * @file test_scan.cpp
 * @brief The scan cycle driver, and Component announce.
 *
 * What a program itself promises is test_program.cpp; the site's laws are
 * test_site_programs.cpp. Each section is its own namespace, isolating
 * fixtures and helpers.
 */

#include <cstddef>
#include "libbotanik/plc/authority.h"
#include <cstdint>
#include <vector>
#include "libbotanik/tasks/tasks.h"
#include "doctest.h"
#include "recording_writer.h"
#include <cstdio>
#include <string>
#include "libbotanik_site.h"
#include "libbotanik.h"
#include BOTANIK_SITE_HEADER
#include "libbotanik/core/component.h"
#include "scan_fixture.h"
#include "sites/gh_mlb41/generated/programs.gen.h"


// scan

namespace scan {

using TestScan::Cycle;

using Botanik::Vars::Bank;
using Botanik::Plc::Program;
using Botanik::ProgramContext;

using Botanik::Tasks::GROUP_FAST;
using Botanik::Tasks::GROUP_SLOW;
using Botanik::Tasks::GROUP_BACKGROUND;

namespace {
    struct Rec { uint32_t dt; uint16_t missed; uint64_t now; };

    /** @brief A program that records every activation; its state is its
     *  own, so a case owns its recording instead of sharing a file static. */
    class Recorder : public Program {
    public:
        std::vector<Rec> runs_seen;
        int              binds  = 0;
        int              firsts = 0;   ///< activations that said first_scan

        Recorder(const char* name, Botanik::Tasks::GroupId g, Bank& io,
                 const Botanik::SetupContext& ctx)
            : Program(ctx, name, g, io) {}

    protected:
        void bind(Botanik::Vars::Bank& io) override { (void)io; binds++; }
        void run(ProgramContext& c) override {
            if (c.first_scan) firsts++;
            runs_seen.push_back({ c.dt_ms, c.missed, c.now_ms });
        }
    };

    /** @brief Bank with control granted, which is what bring-up does. */
    struct Live {
        Botanik::Tasks::Registry reg;   ///< what the case's programs register in
        Bank io;
        Botanik::Plc::ControlAuthority auth{io};
        Live() { auth.set_health_hold(false); }
    };
}

TEST_CASE("scan: each program runs at its own rate") {
    Live f;
    Recorder fast("fast", GROUP_SLOW, f.io, Botanik::SetupContext(f.reg)), slow("slow", GROUP_BACKGROUND, f.io, Botanik::SetupContext(f.reg));
    Program* const progs[] = { &fast, &slow };
    Cycle d(progs, 2, &f.io, f.reg);
    for (uint64_t t = 0; t <= 2000; t += 10) d.step(t);

    // ~20 runs of a 100 ms program per 2 s against ~4 of a 500 ms one;
    // assert the ratio, not exact counts.
    CHECK(fast.runs_seen.size() >= 19);
    CHECK(fast.runs_seen.size() <= 21);
    CHECK(slow.runs_seen.size() >= 4);
    CHECK(slow.runs_seen.size() <= 5);
}

TEST_CASE("scan: dt_ms is the nominal period when on time") {
    Live f;
    Recorder fast("fast", GROUP_SLOW, f.io, Botanik::SetupContext(f.reg));
    Program* const progs[] = { &fast };
    Cycle d(progs, 1, &f.io, f.reg);
    for (uint64_t t = 0; t <= 1000; t += 10) d.step(t);
    REQUIRE(fast.runs_seen.size() >= 3);
    for (size_t i = 1; i < fast.runs_seen.size(); i++)
        CHECK(fast.runs_seen[i].dt == 100);
}

TEST_CASE("scan: a late program runs once and reports the real elapsed time") {
    Live f;
    Recorder fast("fast", GROUP_SLOW, f.io, Botanik::SetupContext(f.reg));
    Program* const progs[] = { &fast };
    Cycle d(progs, 1, &f.io, f.reg);
    d.step(0);        // first run seeds
    fast.runs_seen.clear();

    // A 950 ms stall: nine 100 ms activations were missed.
    d.step(1000);

    REQUIRE(fast.runs_seen.size() == 1);       // ran ONCE, no catch-up burst
    CHECK(fast.runs_seen[0].dt == 1000);       // and dt is the real gap, not 100
    CHECK(fast.runs_seen[0].missed == 9);
}

TEST_CASE("scan: bind runs once, before the first activation") {
    Live f;
    Recorder p("p", GROUP_FAST, f.io, Botanik::SetupContext(f.reg));
    Program* const progs[] = { &p };
    Cycle d(progs, 1, &f.io, f.reg);   // ctor registers, then runs the setup pass
    INFO("registering is what binds: off the beat, before the watchdog is armed");
    CHECK(p.binds == 1);
    CHECK(p.runs_seen.empty());                // and nothing has run yet
    for (uint64_t t = 0; t < 50; t += 10) d.step(t);
    CHECK(p.binds == 1);                       // once, not once per beat
    CHECK(p.runs_seen.size() == 5);
}

TEST_CASE("scan: same-period programs are spread across beats, not piled up") {
    // Without the stagger, two same-rate programs would share every beat forever.
    Live f;
    Recorder a("a", GROUP_SLOW, f.io, Botanik::SetupContext(f.reg)), b("b", GROUP_SLOW, f.io, Botanik::SetupContext(f.reg));
    Program* const progs[] = { &a, &b };
    Cycle d(progs, 2, &f.io, f.reg);
    for (uint64_t t = 0; t <= 1000; t += 10) d.step(t);

    // Both ran at their declared rate...
    CHECK(a.runs_seen.size() >= 9);
    CHECK(b.runs_seen.size() >= 9);
    // ...but only ever together on the first beat - unavoidable, since that's what seeds each timer.
    size_t shared = 0;
    for (size_t i = 0; i < a.runs_seen.size(); i++)
        for (size_t j = 0; j < b.runs_seen.size(); j++)
            if (a.runs_seen[i].now == b.runs_seen[j].now) shared++;
    CHECK(shared == 1);
    CHECK(a.runs_seen[0].now == 0);
    CHECK(b.runs_seen[0].now == 0);
}

TEST_CASE("scan: an every-beat program is left alone by the stagger") {
    // period 0 has no interval to shift inside; it must keep running every beat.
    Live f;
    Recorder slow("slow", GROUP_SLOW, f.io, Botanik::SetupContext(f.reg)), every("every", GROUP_FAST, f.io, Botanik::SetupContext(f.reg));
    Program* const progs[] = { &slow, &every };
    Cycle d(progs, 2, &f.io, f.reg);
    for (uint64_t t = 0; t < 50; t += 10) d.step(t);

    CHECK(every.runs_seen.size() == 5);
    CHECK(slow.runs_seen.size() == 1);   // just the first-beat run
}

TEST_CASE("scan: programs are inhibited while control is withheld") {
    Botanik::Tasks::Registry reg;
    Bank io;                                   // starts held by the system
    Recorder fast("fast", GROUP_SLOW, io, Botanik::SetupContext(reg));
    Program* const progs[] = { &fast };
    Cycle d(progs, 1, &io, reg);
    for (uint64_t t = 0; t <= 500; t += 10) d.step(t);

    CHECK(fast.runs_seen.empty());             // nothing drives the field
    // ...so there is no activation to have said anything about a first scan.
    CHECK(fast.firsts == 0);
}

TEST_CASE("scan: a control outage makes the next activation a first scan, once") {
    Live f;
    Recorder fast("fast", GROUP_SLOW, f.io, Botanik::SetupContext(f.reg));
    Program* const progs[] = { &fast };
    Cycle d(progs, 1, &f.io, f.reg);
    d.step(0);
    REQUIRE(fast.firsts == 1);               // bring-up is one

    f.auth.set_control_mode(Botanik::Vars::CONTROL_DISABLED);
    for (uint64_t t = 10; t <= 500; t += 10) d.step(t);
    INFO("nothing ran, so nothing was told anything");
    CHECK(fast.firsts == 1);

    // Back: the process moved without us, so what the program remembered is
    // stale - and the flag on its next activation is where it drops it. Once,
    // not on every activation from here.
    f.auth.set_control_mode(Botanik::Vars::CONTROL_ENABLED);
    for (uint64_t t = 510; t <= 1000; t += 10) d.step(t);
    CHECK(fast.firsts == 2);
    CHECK(fast.runs_seen.size() > 3);        // and it is running normally again
}

TEST_CASE("scan: control returning does not hand a program the whole outage") {
    Live f;
    Recorder fast("fast", GROUP_SLOW, f.io, Botanik::SetupContext(f.reg));
    Program* const progs[] = { &fast };
    Cycle d(progs, 1, &f.io, f.reg);
    d.step(0);                                // one activation, seeds the timer
    fast.runs_seen.clear();

    f.auth.set_control_mode(Botanik::Vars::CONTROL_DISABLED);
    for (uint64_t t = 10; t <= 60000; t += 10) d.step(t);
    REQUIRE(fast.runs_seen.empty());          // inhibited throughout

    f.auth.set_control_mode(Botanik::Vars::CONTROL_ENABLED);
    d.step(60010);                            // the reseed beat: nothing runs yet
    d.step(60110);

    REQUIRE(fast.runs_seen.size() == 1);
    // The nominal period, NOT the 60 s outage - a Ton holding 5 s handed an
    // hour of dt fires the moment control returns, which withdrawal prevents.
    CHECK(fast.runs_seen[0].dt == 100);
    CHECK(fast.runs_seen[0].missed == 0);
}

TEST_CASE("scan: a null table or a null program is inert") {
    Live f;
    Cycle none(nullptr, 0, &f.io, f.reg);
    none.step(0);                       // must not fault

    Recorder p("p", GROUP_FAST, f.io, Botanik::SetupContext(f.reg));
    Program* const holey[] = { nullptr, &p };
    Cycle d(holey, 2, &f.io, f.reg);
    d.step(0);
    CHECK(p.runs_seen.size() == 1);     // the live one still runs
}

}  // namespace scan


// component

namespace health_rows {

using TestScan::Cycle;
using TestScan::Live;
using namespace Botanik;
using namespace Botanik::Vars;

namespace {
    /** @brief Publish everything the cooling law reads. Its interface is four
     *  computed rows and a handful of parameters; the parameters are always
     *  valid, the computed ones are not until something has produced them. */
    void live_inputs(Live& f) {
        f.bank.var(f.ref("cli_temp_hi")).publish(from_f32(T_F32, 21.0f), 0);
        f.bank.var(f.ref("cli_temp_lo")).publish(from_f32(T_F32, 20.0f), 0);
        // What the ladder CONTROLS on, which temp_rh_zone derives from the two
        // levels above and this case stands in for.
        f.bank.var(f.ref("cli_temp_in")).publish(from_f32(T_F32, 21.0f), 0);
        f.bank.var(f.ref("cli_temp_cs")).publish(from_f32(T_F32, 18.0f), 0);
        f.bank.var(f.ref("cli_targetTemp_degC")).set(20.0f);
    }
}

TEST_CASE("programs: an input nobody can believe is DEGRADED, whoever reads it") {
    // Not a rule each law remembers: the base walks what the configuration says
    // the program reads, so a reading that has stopped being believable is
    // DEGRADED whether or not this particular run() thought to ask.
    Live f;
    Site::TargetTempController target(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &target };
    Cycle d(progs, 1, &f.bank, f.reg);

    // Its whole interface is one computed row and four parameters, and it does
    // not test any of them: a phase it cannot see still yields the base target.
    f.bank.var(f.ref("cli_phase")).set_i((int64_t)Site::PHASE_DAY);
    f.bank.var(f.ref("cli_coldBankArm")).set(false);
    f.bank.var(f.ref("cli_heatBankArm")).set(false);
    d.step(100);
    REQUIRE(target.health() == Plc::Health::HEALTHY);

    f.bank.var(f.ref("cli_phase")).invalidate(Q_NO_DATA);
    d.step(200);
    CHECK(target.health() == Plc::Health::DEGRADED);
    INFO("and it still produces: DEGRADED is an input it wanted, not an output "
         "it could not make");
    CHECK(f.bank.var(f.ref("cli_targetTemp_degC")).valid());
}

TEST_CASE("programs: an output nobody can derive says so, rather than defaulting") {
    Live f;
    Site::HumidityController hum(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &hum };
    Cycle d(progs, 1, &f.bank, f.reg);

    const VarRef dem = f.ref("cli_humidDem");
    f.bank.var(f.ref("cli_dpMargin_degC")).publish(from_f32(T_F32, 10.0f), 0);
    d.step(100);
    REQUIRE(f.bank.var(dem).valid());

    INFO("this row's default is 0, which the ladder reads as SATURATED: "
         "published as a demand it would call for full dehumidification on a "
         "reading nobody has");
    f.bank.var(f.ref("cli_dpMargin_degC")).invalidate(Q_NO_DATA);
    d.step(200);
    CHECK_FALSE(f.bank.var(dem).valid());
    CHECK(hum.health() == Plc::Health::DEGRADED);
}

TEST_CASE("health rows: a law that has stopped working is a WARNING") {
    Live f;
    Botanik::Logs::Logger log;
    log.set_min_level(Botanik::Logs::LOG_DEBUG);
    Botanik::RecordingWriter w;
    log.add_writer(&w);

    Site::CoolingController cooling(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &cooling };
    Cycle d(progs, 1, &f.bank, f.reg);

    // Every row the law READS, not just the one under test: an input a program
    // is entitled to expect and does not have is DEGRADED, which is a different
    // verdict from the one this case is about.
    live_inputs(f);
    d.step(100);
    Plc::publish_health(f.bank, f.reg, &log);
    REQUIRE(cooling.health() == Plc::Health::HEALTHY);
    w.all_msgs.clear();

    INFO("the house's temperature gone: the law cannot produce a demand at all");
    f.bank.var(f.ref("cli_temp_hi")).invalidate(Q_NO_DATA);
    f.bank.var(f.ref("cli_temp_lo")).invalidate(Q_NO_DATA);
    f.bank.var(f.ref("cli_temp_in")).invalidate(Q_NO_DATA);
    d.step(200);
    Plc::publish_health(f.bank, f.reg, &log);
    CHECK(w.saw("cooling: FAULTED"));
    CHECK(w.last_level == Botanik::Logs::LOG_WARNING);

    INFO("...and coming back is the record closing, at DEBUG");
    w.all_msgs.clear();
    live_inputs(f);
    d.step(300);
    Plc::publish_health(f.bank, f.reg, &log);
    CHECK(w.saw("cooling: HEALTHY"));
    CHECK(w.last_level == Botanik::Logs::LOG_DEBUG);
}

TEST_CASE("health rows: a program's verdict reaches its row, running or not") {
    Live f;
    Site::CoolingController cooling(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &cooling };
    Cycle d(progs, 1, &f.bank, f.reg);

    Vars::RefIn<uint8_t> row = f.bank.resolve_in<uint8_t>("prog_cooling");
    // Every row the law reads: with both interior levels gone it reports
    // FAULTED, and with any input unbelievable it reports DEGRADED - neither of
    // which is the case under test.
    live_inputs(f);

    INFO("before the first activation a program is INHIBITED: reading HEALTHY "
         "there would say the plant was in hand during bring-up");
    Plc::publish_health(f.bank, f.reg, nullptr);
    CHECK(f.bank.get(row) == (uint8_t)Plc::Health::INHIBITED);

    d.step(100);
    Plc::publish_health(f.bank, f.reg, nullptr);
    CHECK(f.bank.get(row) == (uint8_t)Plc::Health::HEALTHY);

    INFO("...and the row keeps up when the program is NOT being activated, "
         "which is exactly when a reader wants it");
    cooling.set_safe();
    Plc::publish_health(f.bank, f.reg, nullptr);
    CHECK(f.bank.get(row) == (uint8_t)Plc::Health::INHIBITED);
}

TEST_CASE("health rows: the worst is on the SEVERITY axis, parked is counted") {
    Live f;
    Site::CoolingController cooling(Botanik::SetupContext(f.reg), f.bank);
    Site::StratSensor       strat(Botanik::SetupContext(f.reg), f.bank);
    Plc::Program* const progs[] = { &cooling, &strat };
    Cycle d(progs, 2, &f.bank, f.reg);

    // A gap computed from a level nothing measures leaves strat's own output
    // invalid, which is FAULTED rather than degraded.
    f.bank.var(f.ref("cli_temp_hi")).invalidate(Q_NO_DATA);
    f.bank.var(f.ref("cli_temp_in")).invalidate(Q_NO_DATA);
    d.step(100);

    uint8_t parked = 0;
    CHECK(Plc::worst_health(f.reg, &parked) == Plc::Health::FAULTED);
    CHECK(parked == 0);

    INFO("a parked plant is not a degraded one, so INHIBITED is counted rather "
         "than compared");
    cooling.set_safe();
    strat.set_safe();
    CHECK(Plc::worst_health(f.reg, &parked) == Plc::Health::HEALTHY);
    CHECK(parked == 2);
}

}  // namespace health_rows

namespace component {

using Botanik::Component;

namespace {
    /** Records the beats it ran on and the phases it saw, in order. */
    class Spy : public Component {
    public:
        std::vector<uint64_t>            ran;      // beats on_service() saw
        std::vector<Botanik::Tasks::Phase> phases; // every hook, in call order
        uint32_t setups = 0;

        Spy() : Component(Botanik::SetupContext(), "spy") {}

    protected:
        void on_setup(uint64_t) override { setups++; }
        void on_input(uint64_t)   override { phases.push_back(Botanik::Tasks::PHASE_INPUT); }
        void on_config(uint64_t)  override { phases.push_back(Botanik::Tasks::PHASE_CONFIG); }
        void on_service(uint64_t now_ms) override {
            phases.push_back(Botanik::Tasks::PHASE_SERVICE);
            ran.push_back(now_ms);
        }
        void on_control(uint64_t) override { phases.push_back(Botanik::Tasks::PHASE_CONTROL); }
        void on_output(uint64_t)  override { phases.push_back(Botanik::Tasks::PHASE_OUTPUT); }
        void on_present(uint64_t) override { phases.push_back(Botanik::Tasks::PHASE_PRESENT); }
    };
}

// The rate is the TASK's, not the component's: a stepped component runs every
// time it is stepped. The due/late/dropped rule is tested against the real
// TaskTimer in test_tasks.cpp.
TEST_CASE("component: a stepped component runs on every beat") {
    Spy s;
    for (uint64_t t = 0; t <= 200; t += 50) s.step(t);

    REQUIRE(s.ran.size() == 5);
    CHECK(s.ran[0] == 0);
    CHECK(s.ran[4] == 200);
}

TEST_CASE("component: setup() runs once, before the first hook") {
    Spy s;
    for (uint64_t t = 0; t < 100; t += 10) s.step(t);

    CHECK(s.setups == 1);
    CHECK(s.ran.size() == 10);
}

TEST_CASE("component: step() dispatches every phase hook in beat order") {
    using namespace Botanik::Tasks;
    Spy s;
    s.step(0);

    const std::vector<Phase> want = { PHASE_INPUT, PHASE_CONFIG, PHASE_SERVICE,
                                      PHASE_CONTROL, PHASE_OUTPUT, PHASE_PRESENT };
    CHECK(s.phases == want);
}

TEST_CASE("component: a phase step runs only that hook") {
    using namespace Botanik::Tasks;
    Spy s;
    s.step(0, PHASE_OUTPUT);

    const std::vector<Phase> want = { PHASE_OUTPUT };
    CHECK(s.phases == want);
}

}  // namespace component
