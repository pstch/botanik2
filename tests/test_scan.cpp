/**
 * @file test_scan.cpp
 * @brief The scan cycle: the driver, its programs' IO binding, and Component announce.
 *
 * Each section below is the former test_<name>.cpp, kept in its own
 * namespace so their fixtures and helpers cannot collide.
 */

#include <cstddef>                  // size_t
#include <cstdint>                  // uint8_t/uint64_t
#include <vector>                   // std::vector
#include "libbotanik/scan/scan.h"   // Scan::Driver
#include "doctest.h"
#include <cstdio>                   // snprintf - the test log sink
#include <string>
#include "libbotanik_site.h"
#include "libbotanik.h"
#include "libbotanik/plc/io.h"
#include BOTANIK_SITE_HEADER
#include "libbotanik/core/component.h"  // Component

// --------------------------------------------------------------------------
// scan
// --------------------------------------------------------------------------

namespace scan {

using Botanik::Vars::Bank;
using Botanik::Plc::Program;
using Botanik::Plc::Context;

using namespace Botanik::Scan;

namespace {
    struct Rec { uint32_t dt; uint16_t missed; uint64_t now; };

    /** @brief A program that records every activation. Its state is its own -
     *  which is the point of the base class - so a test case owns its
     *  recording instead of sharing a file static with every other case. */
    class Recorder : public Program {
    public:
        std::vector<Rec> runs_seen;
        int              binds  = 0;
        int              resets = 0;

        Recorder(const char* name, uint32_t period_ms) : Program(name, period_ms) {}

        void reset() override { resets++; }

    protected:
        void bind(Botanik::Vars::Bank& io) override { (void)io; binds++; }
        void run(Context& c) override {
            runs_seen.push_back({ c.dt_ms, c.missed, c.now_ms });
        }
    };

    /** @brief Bank with control granted, which is what bring-up does. */
    struct Live {
        Bank io;
        Live() { io.enable_control(Bank::INHIBIT_HEALTH); }
    };
}

TEST_CASE("scan: each program runs at its own rate") {
    Recorder fast("fast", 100), slow("slow", 1000);
    Program* const progs[] = { &fast, &slow };
    Live f;
    Driver d(progs, 2, &f.io);
    for (uint64_t t = 0; t <= 2000; t += 10) d.step(t);

    // ~20 fast runs per 2 s, ~2 slow. Exact counts depend on the first-run
    // seeding, so assert the ratio holds rather than pinning both.
    CHECK(fast.runs_seen.size() >= 19);
    CHECK(fast.runs_seen.size() <= 21);
    CHECK(slow.runs_seen.size() >= 2);
    CHECK(slow.runs_seen.size() <= 3);
}

TEST_CASE("scan: dt_ms is the nominal period when on time") {
    Recorder fast("fast", 100);
    Program* const progs[] = { &fast };
    Live f;
    Driver d(progs, 1, &f.io);
    for (uint64_t t = 0; t <= 1000; t += 10) d.step(t);
    REQUIRE(fast.runs_seen.size() >= 3);
    for (size_t i = 1; i < fast.runs_seen.size(); i++)
        CHECK(fast.runs_seen[i].dt == 100);
}

TEST_CASE("scan: a late program runs once and reports the real elapsed time") {
    Recorder fast("fast", 100);
    Program* const progs[] = { &fast };
    Live f;
    Driver d(progs, 1, &f.io);
    d.step(0);        // first run seeds
    fast.runs_seen.clear();

    // A 950 ms stall: nine 100 ms activations were missed.
    d.step(1000);

    REQUIRE(fast.runs_seen.size() == 1);       // ran ONCE, no catch-up burst
    CHECK(fast.runs_seen[0].dt == 1000);       // and dt is the real gap, not 100
    CHECK(fast.runs_seen[0].missed == 9);
}

TEST_CASE("scan: period 0 runs every beat") {
    Recorder every("every", 0);
    Program* const progs[] = { &every };
    Live f;
    Driver d(progs, 1, &f.io);
    for (uint64_t t = 0; t < 50; t += 10) d.step(t);
    CHECK(every.runs_seen.size() == 5);
}

TEST_CASE("scan: bind runs once, before the first activation") {
    Recorder p("p", 0);
    Program* const progs[] = { &p };
    Live f;
    Driver d(progs, 1, &f.io);
    CHECK(p.binds == 0);
    for (uint64_t t = 0; t < 50; t += 10) d.step(t);
    CHECK(p.binds == 1);                       // once, not once per beat
    CHECK(p.runs_seen.size() == 5);
}

TEST_CASE("scan: same-period programs are spread across beats, not piled up") {
    // Two programs at one rate: without the stagger they would share every beat
    // forever, which is what puts the peak work per beat back where it started.
    Recorder a("a", 100), b("b", 100);
    Program* const progs[] = { &a, &b };
    Live f;
    Driver d(progs, 2, &f.io);
    for (uint64_t t = 0; t <= 1000; t += 10) d.step(t);

    // Both ran at their declared rate...
    CHECK(a.runs_seen.size() >= 9);
    CHECK(b.runs_seen.size() >= 9);
    // ...but only ever together on the first beat, which is unavoidable: a
    // program's first activation is what seeds its timer.
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
    Recorder slow("slow", 100), every("every", 0);
    Program* const progs[] = { &slow, &every };
    Live f;
    Driver d(progs, 2, &f.io);
    for (uint64_t t = 0; t < 50; t += 10) d.step(t);

    CHECK(every.runs_seen.size() == 5);
    CHECK(slow.runs_seen.size() == 1);   // just the first-beat run
}

TEST_CASE("scan: programs are inhibited while control is withheld") {
    Recorder fast("fast", 100);
    Program* const progs[] = { &fast };
    Bank io;                                   // starts at INHIBIT_HEALTH
    Driver d(progs, 1, &io);
    for (uint64_t t = 0; t <= 500; t += 10) d.step(t);

    CHECK(fast.runs_seen.empty());             // nothing drives the field
    // Nothing was ever driving, so there is nothing stale to drop either.
    CHECK(fast.resets == 0);
}

TEST_CASE("scan: control being withdrawn resets the programs, once") {
    Recorder fast("fast", 100);
    Program* const progs[] = { &fast };
    Live f;
    Driver d(progs, 1, &f.io);
    d.step(0);
    REQUIRE(fast.resets == 0);

    f.io.disable_control(Bank::INHIBIT_OPERATOR);
    for (uint64_t t = 10; t <= 500; t += 10) d.step(t);

    // On the withdrawal edge, not on every beat it stays withdrawn: from here
    // the process moves without us, and what a program remembered about the
    // field is stale.
    CHECK(fast.resets == 1);
}

TEST_CASE("scan: control returning does not hand a program the whole outage") {
    Recorder fast("fast", 100);
    Program* const progs[] = { &fast };
    Live f;
    Driver d(progs, 1, &f.io);
    d.step(0);                                // one activation, seeds the timer
    fast.runs_seen.clear();

    f.io.disable_control(Bank::INHIBIT_OPERATOR);
    for (uint64_t t = 10; t <= 60000; t += 10) d.step(t);
    REQUIRE(fast.runs_seen.empty());          // inhibited throughout

    f.io.enable_control(Bank::INHIBIT_OPERATOR);
    d.step(60010);                            // the reseed beat: nothing runs yet
    d.step(60110);

    REQUIRE(fast.runs_seen.size() == 1);
    // The nominal period, NOT the 60 s outage. A Ton holding 5 s handed an hour
    // of dt fires the moment control returns, which is what a withdrawal exists
    // to prevent.
    CHECK(fast.runs_seen[0].dt == 100);
    CHECK(fast.runs_seen[0].missed == 0);
}

TEST_CASE("scan: a null table or a null program is inert") {
    Live f;
    Driver none(nullptr, 0, &f.io);
    none.step(0);                       // must not fault

    Recorder p("p", 0);
    Program* const holey[] = { nullptr, &p };
    Driver d(holey, 2, &f.io);
    d.step(0);
    CHECK(p.runs_seen.size() == 1);     // the live one still runs
}

}  // namespace scan

// --------------------------------------------------------------------------
// plc_io
// --------------------------------------------------------------------------

namespace plc_io {

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

/** @brief A sink that counts criticals and keeps the last, so a test can ask
 *  whether the driver said something and how often. */
class Criticals : public Logs::LogWriter {
public:
    int  seen = 0;
    char last[Config::LOG_MESSAGE_LEN] = {};

    void push(const Logs::LogEntry* e) override {
        if (e == nullptr || e->level != Logs::LOG_CRITICAL) return;
        seen++;
        std::snprintf(last, sizeof last, "%s", e->message);
    }
    void drain() override {}
protected:
    size_t write(const char*, size_t) override { return 0; }
    size_t format(const Logs::LogEntry*, char*, size_t) override { return 0; }
};

/** @brief A program that drives one named row every beat. Two of them pointed
 *  at the same row is the design defect WP-3 exists to notice. */
class Drives : public Plc::Program {
    VarRef      _r = { VAR_NONE };
    const char* _row;
public:
    Drives(const char* name, const char* row) : Program(name, 0), _row(row) {}
protected:
    void bind(Bank& io) override { _r = io.find(_row); }
    void run(Plc::Context& c) override { c.io.set(_r, 1.0f); }
};

/** @brief Appends its own name to a shared log when it runs, so a case can ask
 *  what ran in what order. */
class Ordered : public Plc::Program {
    std::vector<std::string>* _log;
public:
    Ordered(const char* name, std::vector<std::string>* log)
        : Program(name, 0), _log(log) {}
protected:
    void run(Plc::Context&) override { _log->push_back(name()); }
};

/** @brief Reads one row and writes another, so a pair of them can show whether
 *  the second saw what the first wrote IN THE SAME BEAT. */
class Relay : public Plc::Program {
    VarRef      _in = { VAR_NONE }, _out = { VAR_NONE };
    const char* _from;
    const char* _to;
    float       _add;
public:
    Relay(const char* name, const char* from, const char* to, float add)
        : Program(name, 0), _from(from), _to(to), _add(add) {}
protected:
    void bind(Bank& io) override { _in = io.find(_from); _out = io.find(_to); }
    void run(Plc::Context& c) override { c.io.set(_out, c.io.value(_in) + _add); }
};

/** @brief Records the lifecycle edges and what the first-scan flag said. */
class Lifecycle : public Plc::Program {
public:
    int  resumes = 0;
    int  resets  = 0;
    std::vector<bool> firsts;

    Lifecycle(const char* name, uint32_t period_ms) : Program(name, period_ms) {}

    void reset() override  { resets++; }
    void resume() override { resumes++; Program::resume(); }

protected:
    void run(Plc::Context& c) override { firsts.push_back(c.first_scan); }
};

}  // namespace

TEST_CASE("plc io: it stays small enough to pass by value") {
    // The Context is built per activation, so this is per program per beat. A
    // pointer plus the writer byte: one word on the M7, where the byte lands in
    // padding that was already there.
    CHECK(sizeof(Io) <= 2 * sizeof(void*));
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

    // Q_REFUSED, not Q_STALE: a stale row KEEPS its last reading now, and this
    // row has never been published, so asserting the fallback through Q_STALE
    // would pass on the coincidence that the slot still holds the default.
    f.bank.var(r).publish(from_f32(T_F32, 33.0f), 1000);
    f.bank.var(r).invalidate(Q_REFUSED);
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

TEST_CASE("plc io: a row remembers which program drives it") {
    Live f;
    Io one(f.bank, 3);
    const VarRef r = f.ref("cli_temp_ext1");

    CHECK(f.bank.writer_of(r) == WRITER_NONE);
    one.set(r, 1.0f);
    CHECK(f.bank.writer_of(r) == 3);

    // The same program writing again, every beat, is the ordinary case and
    // must stay silent.
    for (int i = 0; i < 10; ++i) one.set(r, 2.0f);
    CHECK(f.bank.writer_conflicts() == 0);
}

TEST_CASE("plc io: a second program on one row is counted, and the write stands") {
    // Two authors is a defect in the site's design: the row gets whichever ran
    // last in the beat, so it chatters at the scan rate while both insist.
    Live f;
    Io one(f.bank, 1), two(f.bank, 2);
    const VarRef r = f.ref("cli_temp_ext1");

    one.set(r, 10.0f);
    REQUIRE(f.bank.writer_conflicts() == 0);

    two.set(r, 20.0f);
    CHECK(f.bank.writer_conflicts() == 1);
    CHECK(f.bank.first_conflict().index == r.index);

    INFO("the write is not refused - that would strand the plant wherever the "
         "losing program had got to");
    CHECK(f.bank.var(r).value() == doctest::Approx(20.0f));

    // Both keep writing every beat, each taking the row from the other. The
    // count is ROWS with two authors, so it settles at one instead of climbing
    // forever - which is what lets the driver report the defect once.
    for (int i = 0; i < 50; ++i) { one.set(r, 10.0f); two.set(r, 20.0f); }
    CHECK(f.bank.writer_conflicts() == 1);

    // A THIRD author is the same bad row, not a new one - but the row still
    // reports whoever wrote it last, rather than freezing on the first claimant.
    Io three(f.bank, 3);
    three.set(r, 30.0f);
    CHECK(f.bank.writer_conflicts() == 1);
    CHECK(f.bank.writer_of(r) == 3);

    // A DIFFERENT row with two authors is a second defect, and counts.
    const VarRef r2 = f.ref("cli_temp_ext2");
    one.set(r2, 1.0f);
    two.set(r2, 2.0f);
    CHECK(f.bank.writer_conflicts() == 2);
}

TEST_CASE("plc io: an unattributed view claims nothing") {
    // A program stepped by hand in a test has no slot. It must not claim rows
    // as writer 255 and then collide with a real program.
    Live f;
    Io anon(f.bank);
    const VarRef r = f.ref("cli_temp_ext1");

    anon.set(r, 1.0f);
    CHECK(f.bank.writer_of(r) == WRITER_NONE);
    CHECK(f.bank.writer_conflicts() == 0);

    Io real(f.bank, 0);
    real.set(r, 2.0f);
    CHECK(f.bank.writer_of(r) == 0);
    CHECK(f.bank.writer_conflicts() == 0);
}

TEST_CASE("scan: two programs on one row is named once, not every beat") {
    // The driver is the only thing that can turn a writer index back into a
    // program name, which is why the report lives there and the counting in
    // the bank.
    Live f;
    Logs::Logger logger;
    Criticals sink;
    logger.add_writer(&sink);

    Drives a("prog_a", "cli_temp_ext1");
    Drives b("prog_b", "cli_temp_ext1");
    Plc::Program* const progs[] = { &a, &b };
    Scan::Driver d(progs, 2, &f.bank, &logger);

    for (uint64_t t = 0; t <= 500; t += 10) d.step(t);

    INFO("last critical: " << std::string(sink.last));
    CHECK(f.bank.writer_conflicts() >= 1);
    INFO("a defect that is true on every beat must not be said on every beat");
    CHECK(sink.seen == 1);
    CHECK(std::string(sink.last).find("cli_temp_ext1") != std::string::npos);
    CHECK(std::string(sink.last).find("prog_") != std::string::npos);
}

TEST_CASE("scan: one program driving its own rows says nothing") {
    Live f;
    Logs::Logger logger;
    Criticals sink;
    logger.add_writer(&sink);

    Drives a("prog_a", "cli_temp_ext1");
    Drives b("prog_b", "cli_temp_ext2");
    Plc::Program* const progs[] = { &a, &b };
    Scan::Driver d(progs, 2, &f.bank, &logger);

    for (uint64_t t = 0; t <= 500; t += 10) d.step(t);

    CHECK(f.bank.writer_conflicts() == 0);
    CHECK(sink.seen == 0);
}

TEST_CASE("scan: a program that keeps its period raises nothing") {
    Live f;
    Drives a("prog_a", "cli_temp_ext1");
    Plc::Program* const progs[] = { &a };
    Scan::Driver d(progs, 1, &f.bank);
    const VarRef late = f.ref("alarm_scan_late");

    for (uint64_t t = 0; t <= 1000; t += 10) d.step(t);

    CHECK(d.late_ms() == 0);
    CHECK(d.worst() == nullptr);
    CHECK_FALSE(f.bank.var(late).b());
    INFO("and it costs something measurable, per program rather than per scan");
    CHECK(a.cost().runs > 0);
}

TEST_CASE("scan: a program starved of beats is late, and named") {
    // Lateness is against the PROGRAM's own period, so one threshold serves
    // every rate. Here the driver is simply not stepped for a while, which is
    // what an overrunning beat looks like from a program's point of view.
    Live f;
    Drives slow("prog_slow", "cli_temp_ext1");
    slow.set_period(100);
    Plc::Program* const progs[] = { &slow };
    Scan::Driver d(progs, 1, &f.bank);
    const VarRef late = f.ref("alarm_scan_late");

    d.step(0);                       // seeds the timer
    CHECK(d.late_ms() == 0);

    d.step(1000);                    // 900 ms past its 100 ms period

    CHECK(d.late_ms() == 900);
    REQUIRE(d.worst() != nullptr);
    CHECK(std::string(d.worst()->name()) == "prog_slow");
    CHECK(slow.late_max_ms() == 900);
    INFO("the row is driven; the alarm's own dwell decides if it annunciates");
    CHECK(f.bank.var(late).b());

    // Back on schedule, and the row clears - the peak is kept, because that is
    // the number worth keeping.
    d.step(1100);
    CHECK(d.late_ms() == 0);
    CHECK_FALSE(f.bank.var(late).b());
    CHECK(slow.late_max_ms() == 900);
}

TEST_CASE("scan: an every-beat program is never reported late") {
    // Period 0 is due every beat and has no nominal to be late against, so it
    // must not raise the alarm just because the beat itself slipped.
    Live f;
    Drives every("prog_every", "cli_temp_ext1");
    Plc::Program* const progs[] = { &every };
    Scan::Driver d(progs, 1, &f.bank);

    d.step(0);
    d.step(5000);                    // an enormous gap

    CHECK(every.late_ms() == 0);
    CHECK(d.late_ms() == 0);
    CHECK_FALSE(f.bank.var(f.ref("alarm_scan_late")).b());
}

TEST_CASE("scan: the first activation says so, and only the first") {
    Live f;
    Lifecycle p("p", 0);
    Plc::Program* const progs[] = { &p };
    Scan::Driver d(progs, 1, &f.bank);

    for (uint64_t t = 0; t < 50; t += 10) d.step(t);

    REQUIRE(p.firsts.size() == 5);
    CHECK(p.firsts[0]);
    for (size_t i = 1; i < p.firsts.size(); ++i) {
        INFO("activation " << i);
        CHECK_FALSE(p.firsts[i]);
    }
}

TEST_CASE("scan: control coming back is a first scan again") {
    // The process moved without us, so the previous activation describes a
    // plant that no longer exists - which is the same situation bring-up is in.
    Live f;
    Lifecycle p("p", 0);
    Plc::Program* const progs[] = { &p };
    Scan::Driver d(progs, 1, &f.bank);

    d.step(0);
    d.step(10);
    REQUIRE(p.firsts.size() == 2);
    REQUIRE(p.firsts[0]);
    REQUIRE_FALSE(p.firsts[1]);

    f.bank.disable_control(Bank::INHIBIT_OPERATOR);
    for (uint64_t t = 20; t <= 5000; t += 10) d.step(t);
    CHECK(p.resets == 1);                    // the withdrawal edge, once
    CHECK(p.firsts.size() == 2);             // nothing ran while stopped

    f.bank.enable_control(Bank::INHIBIT_OPERATOR);
    d.step(5010);

    CHECK(p.resumes == 2);                   // bring-up, then this return
    REQUIRE(p.firsts.size() == 3);
    INFO("the run after control returns starts from nothing");
    CHECK(p.firsts[2]);

    d.step(5020);
    REQUIRE(p.firsts.size() == 4);
    CHECK_FALSE(p.firsts[3]);
}

TEST_CASE("scan: bring-up is itself a resume, and reset is not called for it") {
    // Control starts withheld and is granted before the first beat, so the
    // return edge fires at bring-up too. That is correct rather than incidental:
    // at bring-up a program knows no more about the plant than it does after an
    // outage, and a program that re-datums an actuator wants to do it in both.
    Live f;
    Lifecycle p("p", 0);
    Plc::Program* const progs[] = { &p };
    Scan::Driver d(progs, 1, &f.bank);

    d.step(0);

    CHECK(p.resumes == 1);
    INFO("nothing was ever driving, so nothing has gone stale");
    CHECK(p.resets == 0);
}

TEST_CASE("scan: table order is execution order") {
    // A site orders its programs on purpose - a law that computes a setpoint
    // before the law that acts on it - and nothing else in the firmware says so.
    // Not alphabetical, not by period, not by when they were constructed: the
    // order the site wrote them in.
    Live f;
    std::vector<std::string> ran;
    Ordered a("zulu",  &ran);
    Ordered b("alpha", &ran);
    Ordered c("mike",  &ran);
    Plc::Program* const progs[] = { &a, &b, &c };
    Scan::Driver d(progs, 3, &f.bank);

    d.step(0);

    REQUIRE(ran.size() == 3);
    INFO("declared zulu, alpha, mike - and that is the order, not sorted");
    CHECK(ran[0] == "zulu");
    CHECK(ran[1] == "alpha");
    CHECK(ran[2] == "mike");
}

TEST_CASE("scan: reordering the table reorders the beat") {
    // The same three programs, declared the other way round. If the order came
    // from anything but the table, this would not change.
    Live f;
    std::vector<std::string> ran;
    Ordered a("zulu",  &ran);
    Ordered b("alpha", &ran);
    Ordered c("mike",  &ran);
    Plc::Program* const progs[] = { &c, &b, &a };
    Scan::Driver d(progs, 3, &f.bank);

    d.step(0);

    REQUIRE(ran.size() == 3);
    CHECK(ran[0] == "mike");
    CHECK(ran[1] == "alpha");
    CHECK(ran[2] == "zulu");
}

TEST_CASE("scan: a later program sees what an earlier one wrote this beat") {
    // Which is WHY the order is a contract rather than an accident: the image
    // mutates between phases, not under a running program, so a chain within
    // one beat is exactly as long as the table says it is.
    Live f;
    const VarRef src = f.ref("cli_temp_ext1");
    const VarRef mid = f.ref("cli_temp_ext2");
    const VarRef dst = f.ref("cli_temp_ext3");
    f.bank.var(src).publish(from_f32(T_F32, 10.0f), 0);

    Relay first("first",  "cli_temp_ext1", "cli_temp_ext2", 1.0f);
    Relay second("second", "cli_temp_ext2", "cli_temp_ext3", 1.0f);

    SUBCASE("in order, the whole chain completes in one beat") {
        Plc::Program* const progs[] = { &first, &second };
        Scan::Driver d(progs, 2, &f.bank);
        d.step(0);

        CHECK(f.bank.var(mid).value() == doctest::Approx(11.0f));
        INFO("second read what first had just written");
        CHECK(f.bank.var(dst).value() == doctest::Approx(12.0f));
    }

    SUBCASE("out of order, the chain takes a beat per link") {
        Plc::Program* const progs[] = { &second, &first };
        Scan::Driver d(progs, 2, &f.bank);
        d.step(0);

        // second ran first, on the value mid held BEFORE first wrote it.
        CHECK(f.bank.var(mid).value() == doctest::Approx(11.0f));
        INFO("the far end is a beat behind, which is what mis-ordering costs");
        CHECK(f.bank.var(dst).value() != doctest::Approx(12.0f));

        d.step(10);
        CHECK(f.bank.var(dst).value() == doctest::Approx(12.0f));
    }
}

TEST_CASE("scan: a computed row can be traced back to the program that wrote it") {
    // What the MONITOR page's Mapping field turns into "computed (prog_a)".
    // The page only formats; this is the path it formats - writer_of() gives the
    // index, the driver turns it into a name, and neither is much use alone.
    Live f;
    Drives a("prog_a", "cli_temp_ext1");
    Drives b("prog_b", "cli_temp_ext2");
    Plc::Program* const progs[] = { &a, &b };
    Scan::Driver d(progs, 2, &f.bank);

    d.step(0);

    const uint8_t w1 = f.bank.writer_of(f.ref("cli_temp_ext1"));
    const uint8_t w2 = f.bank.writer_of(f.ref("cli_temp_ext2"));
    REQUIRE(d.program(w1) != nullptr);
    REQUIRE(d.program(w2) != nullptr);
    CHECK(std::string(d.program(w1)->name()) == "prog_a");
    CHECK(std::string(d.program(w2)->name()) == "prog_b");

    // A row no program has written names nobody, rather than naming program 0.
    const uint8_t none = f.bank.writer_of(f.ref("cli_temp_ext3"));
    CHECK(none == WRITER_NONE);
    CHECK(d.program(none) == nullptr);
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

}  // namespace plc_io

// --------------------------------------------------------------------------
// component
// --------------------------------------------------------------------------

namespace component {

using Botanik::Component;

namespace {
    /** A component that records every beat it actually ran on. */
    class Spy : public Component {
    public:
        std::vector<uint64_t> ran;
        std::vector<uint32_t> dts;
        std::vector<uint16_t> misses;
        uint32_t setups = 0;
        uint32_t serial = 0;
        uint32_t reconfigs = 0;

        Spy() : Component(nullptr, "spy") {}

    protected:
        void on_setup(uint64_t) override { setups++; }
        void on_step(uint64_t now_ms) override {
            ran.push_back(now_ms);
            dts.push_back(dt_ms());
            misses.push_back(missed());
        }
        uint32_t param_serial() const override { return serial; }
        void on_param_changed(uint64_t) override { reconfigs++; }
    };
}

TEST_CASE("component: the default period is every beat") {
    Spy s;
    CHECK(s.period_ms() == 0);
    for (uint64_t t = 0; t < 50; t += 10) s.step(t);
    CHECK(s.ran.size() == 5);
}

TEST_CASE("component: a period makes it sit out the beats in between") {
    Spy s;
    s.set_period(50);
    for (uint64_t t = 0; t <= 200; t += 10) s.step(t);

    // t=0 (first step is always due), then 50/100/150/200.
    REQUIRE(s.ran.size() == 5);
    CHECK(s.ran[0] == 0);
    CHECK(s.ran[1] == 50);
    CHECK(s.ran[4] == 200);
}

TEST_CASE("component: on_setup() still runs on the first beat, whatever the period") {
    Spy s;
    s.set_period(1000);          // far longer than the run below
    for (uint64_t t = 0; t < 100; t += 10) s.step(t);

    CHECK(s.setups == 1);        // ran, despite never coming due again
    CHECK(s.ran.size() == 1);
}

TEST_CASE("component: dt_ms is the real gap, not the beat") {
    Spy s;
    s.set_period(50);
    for (uint64_t t = 0; t <= 150; t += 10) s.step(t);

    REQUIRE(s.dts.size() >= 3);
    CHECK(s.dts[0] == 50);       // first run reports the nominal period
    CHECK(s.dts[1] == 50);       // then the real gap, which here matches
    CHECK(s.dts[2] == 50);
}

TEST_CASE("component: a stalled loop reports the real gap and the dropped beats") {
    Spy s;
    s.set_period(50);
    s.step(0);
    s.ran.clear(); s.dts.clear(); s.misses.clear();

    s.step(500);                 // a 500 ms stall on a 50 ms period

    REQUIRE(s.ran.size() == 1);  // ran ONCE, no catch-up burst
    CHECK(s.dts[0] == 500);      // the real elapsed time
    CHECK(s.misses[0] == 9);
}

TEST_CASE("component: a sat-out beat does not poll the config serial either") {
    Spy s;
    s.set_period(100);
    s.step(0);
    CHECK(s.reconfigs == 0);     // serial 0 at boot is not a change

    s.serial = 7;                // config commits between activations
    s.step(10);
    s.step(20);
    CHECK(s.reconfigs == 0);     // not due, so not noticed yet

    s.step(100);
    CHECK(s.reconfigs == 1);     // picked up on the next activation
}

TEST_CASE("component: period 0 and an unset period behave identically") {
    Spy a, b;
    b.set_period(0);
    for (uint64_t t = 0; t < 100; t += 10) { a.step(t); b.step(t); }
    CHECK(a.ran == b.ran);
    CHECK(a.dts == b.dts);
}

}  // namespace component
