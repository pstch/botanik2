/**
 * @file test_scan.cpp
 * @brief The scan cycle: the driver, its programs' IO binding, and Component announce.
 *
 * Each section is its own namespace, isolating fixtures and helpers.
 */

#include <cstddef>
#include <cstdint>
#include <vector>
#include "libbotanik/tasks/tasks.h"
#include "doctest.h"
#include <cstdio>
#include <string>
#include "libbotanik_site.h"
#include "libbotanik.h"
#include "libbotanik/plc/io.h"
#include BOTANIK_SITE_HEADER
#include "libbotanik/core/component.h"
#include "site_bank.h"

namespace {

/** @brief What the firmware does with a program table, in one place: hand
 *  each program the bank and its slot, register it, then per beat carry the
 *  Bank's verdict to BOTH halves of the authority edge and dispatch
 *  PHASE_CONTROL through Registry::set_programs_enabled, which is the shipping
 *  call, so this drives the real code. */
struct Cycle {
    Botanik::Tasks::Registry&     reg;
    Botanik::Plc::Program* const* progs;
    uint8_t                       n;
    Botanik::Vars::Bank*          io;

    Cycle(Botanik::Plc::Program* const* p, uint8_t count,
          Botanik::Vars::Bank* bank, Botanik::Tasks::Registry& r)
        : reg(r), progs(p), n(count), io(bank) {
        for (uint8_t i = 0; i < n; i++) {
            if (progs == nullptr || progs[i] == nullptr) continue;
            progs[i]->set_slot(i);
            progs[i]->register_tasks();
        }
        reg.setup(0);          // bind(), off the beat, as bring-up does
        _late_alarm = bank->find("alarm_scan_late");
    }

    void step(uint64_t t) {
        // Idempotent, and the whole edge: the gate, plus on the way back up the
        // timer reseed and the first-scan flag.
        reg.set_programs_enabled(io->control_enabled(), t);
        reg.run_phase(Botanik::Tasks::PHASE_CONTROL, t);
        // ...and what the firmware does after the phase: publish the worst
        // lateness onto the alarm row, whose own dwell decides whether it
        // ever annunciates.
        _late = Botanik::Plc::worst_lateness(reg, &_worst);
        io->var(_late_alarm).set(_late > Botanik::Config::SCAN_LATE_MS);
    }

    /** @brief The worst-late program on the last beat, and who owned it. */
    uint32_t late_ms() const     { return _late; }
    const Botanik::Plc::Program* worst() const { return _worst; }

    /** @brief A writer index back to its program, the lookup the firmware hands
     *  MONITOR (BotanikFirmware::register_pages). Bounds-checked, so a row no
     *  program has written names nobody rather than naming program 0. */
    const Botanik::Plc::Program* program(uint8_t w) const {
        return (progs != nullptr && w < n) ? progs[w] : nullptr;
    }

private:
    Botanik::Vars::VarRef        _late_alarm = { Botanik::Vars::VAR_NONE };
    uint32_t                     _late  = 0;
    const Botanik::Plc::Program* _worst = nullptr;
};

}  // namespace

// scan

namespace scan {

using Botanik::Vars::Bank;
using Botanik::Plc::Program;
using Botanik::Plc::Context;

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
        void run(Context& c) override {
            if (c.first_scan) firsts++;
            runs_seen.push_back({ c.dt_ms, c.missed, c.now_ms });
        }
    };

    /** @brief Bank with control granted, which is what bring-up does. */
    struct Live {
        Botanik::Tasks::Registry reg;   ///< what the case's programs register in
        Bank io;
        Live() { io.set_health_hold(false); }
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

    f.io.set_control_mode(Botanik::Vars::CONTROL_DISABLED);
    for (uint64_t t = 10; t <= 500; t += 10) d.step(t);
    INFO("nothing ran, so nothing was told anything");
    CHECK(fast.firsts == 1);

    // Back: the process moved without us, so what the program remembered is
    // stale - and the flag on its next activation is where it drops it. Once,
    // not on every activation from here.
    f.io.set_control_mode(Botanik::Vars::CONTROL_ENABLED);
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

    f.io.set_control_mode(Botanik::Vars::CONTROL_DISABLED);
    for (uint64_t t = 10; t <= 60000; t += 10) d.step(t);
    REQUIRE(fast.runs_seen.empty());          // inhibited throughout

    f.io.set_control_mode(Botanik::Vars::CONTROL_ENABLED);
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

// plc_io

namespace plc_io {

using namespace Botanik;
using namespace Botanik::Vars;
using Botanik::Plc::Io;

namespace {

/** @brief A bank over the site's real tables, running. */
struct Live {
    Botanik::Tasks::Registry reg;   ///< what the case's programs register in
    Bank bank;
    Live() {
        TestSite::build(bank);
        bank.set_health_hold(false);
        bank.set_control_mode(CONTROL_ENABLED);
    }
    VarRef ref(const char* name) { return bank.find(name); }
};

/** @brief A sink that counts criticals and keeps the last, so a test can
 *  check what the driver said and how often. */
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

/** @brief Drives one named row every beat; two on the same row is the
 *  design defect WP-3 exists to notice. */
class Drives : public Plc::Program {
    VarRef      _r = { VAR_NONE };
    const char* _row;
public:
    Drives(const char* name, const char* row, Botanik::Vars::Bank& io,
           const Botanik::SetupContext& ctx,
           Botanik::Tasks::GroupId g = Botanik::Tasks::GROUP_FAST)
        : Program(ctx, name, g, io), _row(row) {}
protected:
    void bind(Bank& io) override { _r = io.find(_row); }
    void run(Plc::Context& c) override { c.io.set(_r, 1.0f); }
};

/** @brief Appends its name to a shared log when it runs, so a case can check order. */
class Ordered : public Plc::Program {
    std::vector<std::string>* _log;
public:
    Ordered(const char* name, std::vector<std::string>* log, Bank& io,
            const Botanik::SetupContext& ctx)
        : Program(ctx, name, Botanik::Tasks::GROUP_FAST, io), _log(log) {}
protected:
    void run(Plc::Context&) override { _log->push_back(name()); }
};

/** @brief Reads one row and writes another, showing whether the second saw
 *  the first's write IN THE SAME BEAT. */
class Relay : public Plc::Program {
    VarRef      _in = { VAR_NONE }, _out = { VAR_NONE };
    const char* _from;
    const char* _to;
    float       _add;
public:
    Relay(const char* name, const char* from, const char* to, float add, Bank& io,
          const Botanik::SetupContext& ctx)
        : Program(ctx, name, Botanik::Tasks::GROUP_FAST, io), _from(from), _to(to), _add(add) {}
protected:
    void bind(Bank& io) override { _in = io.find(_from); _out = io.find(_to); }
    void run(Plc::Context& c) override { c.io.set(_out, c.io.value(_in) + _add); }
};

/** @brief Records what the first-scan flag said on every activation. */
class Lifecycle : public Plc::Program {
public:
    std::vector<bool> firsts;

    Lifecycle(const char* name, Botanik::Tasks::GroupId g, Botanik::Vars::Bank& io,
              const Botanik::SetupContext& ctx)
        : Program(ctx, name, g, io) {}

protected:
    void run(Plc::Context& c) override { firsts.push_back(c.first_scan); }
};

}  // namespace

TEST_CASE("plc io: it stays small enough to pass by value") {
    // Built per activation: a pointer plus the writer byte, one word on the M7.
    CHECK(sizeof(Io) <= 2 * sizeof(void*));
}

TEST_CASE("plc io: a program reads what the operator forced, not what the wire said") {
    // The point of applying an override in Var::raw(): a forced reading must reach control logic.
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
    // So logic that doesn't care about quality still gets a sane number, not the slot's leftover.
    Live f;
    Io io(f.bank);
    const VarRef r = f.ref("cli_temp_ext1");

    // Q_REFUSED, not Q_STALE: a stale row keeps its last reading, and this
    // row was never published - Q_STALE would pass only by coincidence.
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
    for (uint16_t i = 0; i < f.bank.count(); ++i) {
        if (f.bank.desc(i).role == ROLE_OUTPUT && f.bank.desc(i).type == T_BOOL) {
            out = f.ref(f.bank.desc(i).name);
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

    // The same program writing again every beat is the ordinary case and must stay silent.
    for (int i = 0; i < 10; ++i) one.set(r, 2.0f);
    CHECK(f.bank.writer_conflicts() == 0);
}

TEST_CASE("plc io: a second program on one row is counted, and the write stands") {
    // Two authors is a site design defect: the row gets whichever ran last, chattering at the scan rate.
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
    CHECK(f.bank.var(r).as_f32() == doctest::Approx(20.0f));

    // Both keep writing every beat, taking the row from each other; the count settles at one.
    for (int i = 0; i < 50; ++i) { one.set(r, 10.0f); two.set(r, 20.0f); }
    CHECK(f.bank.writer_conflicts() == 1);

    // A THIRD author is the same bad row; it still reports whoever wrote it last.
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

TEST_CASE("scan: two programs on one row is named once, not every beat") {
    // The bank counts a second author but cannot NAME one: a writer is an
    // index, and only the program table turns it back into a name.
    Live f;
    Logs::Logger logger;
    Criticals sink;
    logger.add_writer(&sink);

    Drives a("prog_a", "cli_temp_ext1", f.bank, Botanik::SetupContext(f.reg));
    Drives b("prog_b", "cli_temp_ext1", f.bank, Botanik::SetupContext(f.reg));
    Plc::Program* const progs[] = { &a, &b };
    Cycle d(progs, 2, &f.bank, f.reg);

    uint16_t said = 0;
    for (uint64_t t = 0; t <= 500; t += 10) {
        d.step(t);
        Plc::report_writer_conflicts(f.bank, d.reg, &logger, said);
    }

    INFO("last critical: " << std::string(sink.last));
    CHECK(f.bank.writer_conflicts() >= 1);
    INFO("a defect that is true on every beat must not be said on every beat");
    CHECK(sink.seen == 1);
    CHECK(std::string(sink.last).find("cli_temp_ext1") != std::string::npos);
    CHECK(std::string(sink.last).find("prog_") != std::string::npos);
}

TEST_CASE("scan: a program starved of beats is late, and named") {
    // Lateness is against the PROGRAM's own period; not stepping the driver is what an overrunning beat looks like.
    Live f;
    Drives slow("prog_slow", "cli_temp_ext1", f.bank, Botanik::SetupContext(f.reg), Botanik::Tasks::GROUP_SLOW);
    Plc::Program* const progs[] = { &slow };
    Cycle d(progs, 1, &f.bank, f.reg);
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

    // Back on schedule, and the row clears - the peak is kept, since that's the number worth keeping.
    d.step(1100);
    CHECK(d.late_ms() == 0);
    CHECK_FALSE(f.bank.var(late).b());
    CHECK(slow.late_max_ms() == 900);
}

TEST_CASE("scan: an every-beat program is late only when the beat itself is") {
    // A rate GROUP always has a nominal period - GROUP_FAST's is one beat - so
    // unlike the old period-0 case there is something to be late against. On
    // time that is exactly zero; a stalled loop is genuinely late, and says so.
    Live f;
    Drives every("prog_every", "cli_temp_ext1", f.bank, Botanik::SetupContext(f.reg));
    Plc::Program* const progs[] = { &every };
    Cycle d(progs, 1, &f.bank, f.reg);

    for (uint64_t t = 0; t <= 200; t += 10) d.step(t);
    CHECK(every.late_ms() == 0);
    CHECK(d.late_ms() == 0);
    CHECK_FALSE(f.bank.var(f.ref("alarm_scan_late")).b());

    d.step(5200);                    // the whole beat stalled for 5 s
    CHECK(d.late_ms() > Botanik::Config::SCAN_LATE_MS);
    INFO("the row is driven; SCAN_LATE's own dwell decides if it annunciates");
    CHECK(f.bank.var(f.ref("alarm_scan_late")).b());
}

TEST_CASE("scan: the first activation says so, and only the first") {
    Live f;
    Lifecycle p("p", Botanik::Tasks::GROUP_FAST, f.bank, Botanik::SetupContext(f.reg));
    Plc::Program* const progs[] = { &p };
    Cycle d(progs, 1, &f.bank, f.reg);

    for (uint64_t t = 0; t < 50; t += 10) d.step(t);

    REQUIRE(p.firsts.size() == 5);
    CHECK(p.firsts[0]);
    for (size_t i = 1; i < p.firsts.size(); ++i) {
        INFO("activation " << i);
        CHECK_FALSE(p.firsts[i]);
    }
}

TEST_CASE("scan: control coming back is a first scan again") {
    // The process moved without us: the previous activation describes a plant that no longer exists.
    Live f;
    Lifecycle p("p", Botanik::Tasks::GROUP_FAST, f.bank, Botanik::SetupContext(f.reg));
    Plc::Program* const progs[] = { &p };
    Cycle d(progs, 1, &f.bank, f.reg);

    d.step(0);
    d.step(10);
    REQUIRE(p.firsts.size() == 2);
    REQUIRE(p.firsts[0]);
    REQUIRE_FALSE(p.firsts[1]);

    f.bank.set_control_mode(CONTROL_DISABLED);
    for (uint64_t t = 20; t <= 5000; t += 10) d.step(t);
    CHECK(p.firsts.size() == 2);             // nothing ran while stopped

    f.bank.set_control_mode(CONTROL_ENABLED);
    d.step(5010);                            // the reseed beat: nothing runs yet
    CHECK(p.firsts.size() == 2);

    d.step(5020);
    REQUIRE(p.firsts.size() == 3);
    INFO("the first run after control returns starts from nothing");
    CHECK(p.firsts[2]);

    d.step(5030);
    REQUIRE(p.firsts.size() == 4);
    CHECK_FALSE(p.firsts[3]);
}

TEST_CASE("scan: bring-up is a first scan, and needs no special case to be one") {
    // Control starts withheld and is granted before the first beat, so bring-up
    // takes the return edge. It needs nothing of its own: a task's first
    // activation EVER is a first scan too, because a program knows no more about
    // the plant at bring-up than it does after an outage.
    Live f;
    Lifecycle p("p", Botanik::Tasks::GROUP_FAST, f.bank, Botanik::SetupContext(f.reg));
    Plc::Program* const progs[] = { &p };
    Cycle d(progs, 1, &f.bank, f.reg);

    d.step(0);

    REQUIRE(p.firsts.size() == 1);
    CHECK(p.firsts[0]);
}

TEST_CASE("scan: table order is execution order") {
    // A site orders its programs on purpose (a setpoint law before the acting law) - as written.
    Live f;
    std::vector<std::string> ran;
    Ordered a("zulu", &ran, f.bank, Botanik::SetupContext(f.reg));
    Ordered b("alpha", &ran, f.bank, Botanik::SetupContext(f.reg));
    Ordered c("mike", &ran, f.bank, Botanik::SetupContext(f.reg));
    Plc::Program* const progs[] = { &a, &b, &c };
    Cycle d(progs, 3, &f.bank, f.reg);

    d.step(0);

    REQUIRE(ran.size() == 3);
    INFO("declared zulu, alpha, mike - and that is the order, not sorted");
    CHECK(ran[0] == "zulu");
    CHECK(ran[1] == "alpha");
    CHECK(ran[2] == "mike");
}

TEST_CASE("scan: a later program sees what an earlier one wrote this beat") {
    // Why order is a contract: the image mutates between phases, so a chain spans exactly as many beats as the table says.
    Live f;
    const VarRef src = f.ref("cli_temp_ext1");
    const VarRef mid = f.ref("cli_temp_ext2");
    const VarRef dst = f.ref("cli_temp_ext3");
    f.bank.var(src).publish(from_f32(T_F32, 10.0f), 0);

    Relay first("first", "cli_temp_ext1", "cli_temp_ext2", 1.0f, f.bank, Botanik::SetupContext(f.reg));
    Relay second("second", "cli_temp_ext2", "cli_temp_ext3", 1.0f, f.bank, Botanik::SetupContext(f.reg));

    SUBCASE("in order, the whole chain completes in one beat") {
        Plc::Program* const progs[] = { &first, &second };
        Cycle d(progs, 2, &f.bank, f.reg);
        d.step(0);

        CHECK(f.bank.var(mid).as_f32() == doctest::Approx(11.0f));
        INFO("second read what first had just written");
        CHECK(f.bank.var(dst).as_f32() == doctest::Approx(12.0f));
    }

    SUBCASE("out of order, the chain takes a beat per link") {
        Plc::Program* const progs[] = { &second, &first };
        Cycle d(progs, 2, &f.bank, f.reg);
        d.step(0);

        // second ran first, on the value mid held BEFORE first wrote it.
        CHECK(f.bank.var(mid).as_f32() == doctest::Approx(11.0f));
        INFO("the far end is a beat behind, which is what mis-ordering costs");
        CHECK(f.bank.var(dst).as_f32() != doctest::Approx(12.0f));

        d.step(10);
        CHECK(f.bank.var(dst).as_f32() == doctest::Approx(12.0f));
    }
}

TEST_CASE("scan: a computed row can be traced back to the program that wrote it") {
    // What MONITOR's Mapping shows as "computed (prog_a)": writer_of() plus
    // the site table's own name lookup.
    Live f;
    Drives a("prog_a", "cli_temp_ext1", f.bank, Botanik::SetupContext(f.reg));
    Drives b("prog_b", "cli_temp_ext2", f.bank, Botanik::SetupContext(f.reg));
    Plc::Program* const progs[] = { &a, &b };
    Cycle d(progs, 2, &f.bank, f.reg);

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

}  // namespace plc_io

// component

namespace component {

using Botanik::Component;

namespace {
    /** Records the beats it ran on and the phases it saw, in order. */
    class Spy : public Component {
    public:
        std::vector<uint64_t>            ran;      // beats on_service() saw
        std::vector<Botanik::Tasks::Phase> phases; // every hook, in call order
        uint32_t setups = 0;
        uint32_t serial = 0;
        uint32_t reconfigs = 0;

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
        uint32_t param_serial() const override { return serial; }
        void on_param_changed(uint64_t) override { reconfigs++; }
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

TEST_CASE("component: the config serial is polled once per beat, not per phase") {
    Spy s;
    s.step(0);
    CHECK(s.reconfigs == 0);     // serial 0 at boot is not a change

    s.serial = 7;
    s.step(10);                  // six hooks, one reconfigure
    CHECK(s.reconfigs == 1);

    s.step(20);                  // unchanged since, so nothing more
    CHECK(s.reconfigs == 1);
}

TEST_CASE("component: two steps in the same millisecond are two beats") {
    Spy s;
    s.step(0);
    s.serial = 7;
    s.step(0);                   // clock has not moved, but this IS a fresh beat
    CHECK(s.reconfigs == 1);
}

}  // namespace component
