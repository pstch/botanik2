/**
 * @file test_program.cpp
 * @brief Plc::Program's own contract: how it reaches the variable table, what
 *        declaring an output claims, and the health and going-safe lifecycle.
 *
 * The driver that activates it is test_scan.cpp; the site's laws are
 * test_site_programs.cpp.
 */

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "doctest.h"

#include "libbotanik.h"
#include "libbotanik_site.h"
#include BOTANIK_SITE_HEADER
#include "libbotanik/core/component.h"

#include "scan_fixture.h"


namespace plc_io {

using namespace Botanik;
using namespace Botanik::Vars;

using TestScan::Cycle;
using TestScan::Live;

namespace {

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
    void run(ProgramContext& c) override { c.bank.drive(_r, 1.0f); }
};

/** @brief Appends its name to a shared log when it runs, so a case can check order. */
class Ordered : public Plc::Program {
    std::vector<std::string>* _log;
public:
    Ordered(const char* name, std::vector<std::string>* log, Bank& io,
            const Botanik::SetupContext& ctx)
        : Program(ctx, name, Botanik::Tasks::GROUP_FAST, io), _log(log) {}
protected:
    void run(ProgramContext&) override { _log->push_back(name()); }
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
    void run(ProgramContext& c) override {
        c.bank.drive(_out, c.bank.var(_in).as_f32() + _add);
    }
};

/** @brief Records what the first-scan flag said on every activation. */
class Lifecycle : public Plc::Program {
public:
    std::vector<bool> firsts;

    Lifecycle(const char* name, Botanik::Tasks::GroupId g, Botanik::Vars::Bank& io,
              const Botanik::SetupContext& ctx)
        : Program(ctx, name, g, io) {}

protected:
    void run(ProgramContext& c) override { firsts.push_back(c.first_scan); }
};

}  // namespace

TEST_CASE("plc io: a program reads what the operator forced, not what the wire said") {
    // The point of applying an override in Var::raw(): a forced reading must reach control logic.
    Live f;
    const VarRef r = f.ref("cli_temp_ext1");
    REQUIRE(f.bank.var(r).meta()->name != nullptr);

    f.bank.var(r).publish(from_f32(T_F32, 20.0f), 1000);
    CHECK(f.bank.var(r).as_f32() == doctest::Approx(20.0f));
    CHECK(f.bank.var(r).valid());

    f.bank.override_set(f.bank.var(r), from_f32(T_F32, 55.0f), true);
    INFO("the program must see the forced value");
    CHECK(f.bank.var(r).as_f32() == doctest::Approx(55.0f));

    // ...and "force this to read as a dead sensor" is expressible too.
    f.bank.override_set(f.bank.var(r), from_f32(T_F32, 55.0f), false);
    CHECK_FALSE(f.bank.var(r).valid());

    f.bank.override_clear(f.bank.var(r));
    CHECK(f.bank.var(r).as_f32() == doctest::Approx(20.0f));
}

TEST_CASE("plc io: an invalid row reads as its declared default") {
    // So logic that doesn't care about quality still gets a sane number, not the slot's leftover.
    Live f;
    const VarRef r = f.ref("cli_temp_ext1");

    // Q_REFUSED, not Q_STALE: a stale row keeps its last reading, and this
    // row was never published - Q_STALE would pass only by coincidence.
    f.bank.var(r).publish(from_f32(T_F32, 33.0f), 1000);
    f.bank.var(r).invalidate(Q_REFUSED);
    CHECK_FALSE(f.bank.var(r).valid());
    CHECK(f.bank.var(r).as_f32() == doctest::Approx(f.bank.var(r).meta()->def.f));
}

TEST_CASE("plc io: a write lands on the row and is owed to the field") {
    Live f;

    // A relay coil: a ROLE_OUTPUT, so the write has to be carried outward.
    VarRef out = { VAR_NONE };
    for (uint16_t i = 0; i < f.bank.count(); ++i) {
        if (f.bank.descriptor(i).role == ROLE_OUTPUT && f.bank.descriptor(i).type == T_BOOL) {
            out = f.ref(f.bank.descriptor(i).name);
            break;
        }
    }
    REQUIRE(out.index != VAR_NONE);

    f.bank.var(out).clear_field_dirty();
    f.bank.drive(out, true);

    INFO("'" << std::string(f.bank.var(out).name()) << "'");
    CHECK(f.bank.var(out).b());
    CHECK(f.bank.var(out).field_dirty());

    f.bank.drive(out, false);
    CHECK_FALSE(f.bank.var(out).b());
}

TEST_CASE("plc io: a row remembers which program drives it") {
    Live f;
    const VarRef r = f.ref("cli_temp_ext1");

    CHECK(f.bank.writer_of(r) == WRITER_NONE);
    f.bank.begin_writer(3);
    f.bank.drive(r, 1.0f);
    CHECK(f.bank.writer_of(r) == 3);

    // The same program writing again every beat is the ordinary case and must stay silent.
    for (int i = 0; i < 10; ++i) f.bank.drive(r, 2.0f);
    f.bank.end_writer();
    CHECK(f.bank.writer_conflicts() == 0);
}

TEST_CASE("plc io: a second program on one row is counted, and the write stands") {
    // Two authors is a site design defect: the row gets whichever ran last, chattering at the scan rate.
    Live f;
    const VarRef r = f.ref("cli_temp_ext1");

    f.bank.begin_writer(1);
    f.bank.drive(r, 10.0f);
    f.bank.end_writer();
    REQUIRE(f.bank.writer_conflicts() == 0);

    f.bank.begin_writer(2);
    f.bank.drive(r, 20.0f);
    f.bank.end_writer();
    CHECK(f.bank.writer_conflicts() == 1);
    CHECK(f.bank.first_conflict().index == r.index);

    INFO("the write is not refused - that would strand the plant wherever the "
         "losing program had got to");
    CHECK(f.bank.var(r).as_f32() == doctest::Approx(20.0f));

    // Both keep writing every beat, taking the row from each other; the count settles at one.
    for (int i = 0; i < 50; ++i) {
        f.bank.begin_writer(1); f.bank.drive(r, 10.0f);
        f.bank.begin_writer(2); f.bank.drive(r, 20.0f);
    }
    f.bank.end_writer();
    CHECK(f.bank.writer_conflicts() == 1);

    // A THIRD author is the same bad row; it still reports whoever wrote it last.
    f.bank.begin_writer(3);
    f.bank.drive(r, 30.0f);
    f.bank.end_writer();
    CHECK(f.bank.writer_conflicts() == 1);
    CHECK(f.bank.writer_of(r) == 3);

    // A DIFFERENT row with two authors is a second defect, and counts.
    const VarRef r2 = f.ref("cli_temp_ext2");
    f.bank.begin_writer(1); f.bank.drive(r2, 1.0f);
    f.bank.begin_writer(2); f.bank.drive(r2, 2.0f);
    f.bank.end_writer();
    CHECK(f.bank.writer_conflicts() == 2);
}

TEST_CASE("plc io: a typed reference reads and drives its own row") {
    Live f;
    Vars::RefIn<float>  temp = f.bank.resolve_in<float>("cli_temp_ext1");
    Vars::RefOut<bool>  coil = f.bank.resolve_out<bool>("cli_window1_opn");

    f.bank.begin_writer(1);
    f.bank.drive(coil, true);
    f.bank.end_writer();
    CHECK(f.bank.get(coil) == true);
    CHECK(f.bank.var(coil.r).b());

    f.bank.begin_writer(1);
    f.bank.drive(f.bank.resolve_out<float>("cli_temp_ext1"), 21.5f);
    f.bank.end_writer();
    CHECK(f.bank.get(temp) == doctest::Approx(21.5f));
}

TEST_CASE("plc io: binding a name to the wrong type resolves to the sink") {
    // The name is right and the type is wrong, which is still a wiring defect:
    // it must not read the row through the other union arm.
    Live f;
    const uint16_t before = f.bank.unresolved_count();

    Vars::RefIn<bool> wrong = f.bank.resolve_in<bool>("cli_temp_ext1");   // an f32 row
    CHECK(wrong.r.index == Vars::VAR_NONE);
    CHECK(f.bank.unresolved_count() == before + 1);

    INFO("and the right type still binds");
    CHECK(f.bank.resolve_in<float>("cli_temp_ext1").r.index != Vars::VAR_NONE);
    CHECK(f.bank.unresolved_count() == before + 1);
}

TEST_CASE("plc io: a VAR_IN_OUT row takes two writers without a conflict") {
    Live f;
    Vars::RefInOut<float> shared = f.bank.resolve_inout<float>("cli_temp_ext1");

    f.bank.begin_writer(1); f.bank.drive(shared, 10.0f);
    f.bank.begin_writer(2); f.bank.drive(shared, 20.0f);
    f.bank.end_writer();

    CHECK(f.bank.writer_conflicts() == 0);
    CHECK(f.bank.get(shared) == doctest::Approx(20.0f));

    INFO("a shared row records no owner: naming one would report whoever "
         "happened to write last as THE writer");
    CHECK(f.bank.writer_of(shared.r) == Vars::WRITER_NONE);

    INFO("and an undeclared row next to it still counts its second author");
    const VarRef plain = f.ref("cli_temp_ext2");
    f.bank.begin_writer(1); f.bank.drive(plain, 1.0f);
    f.bank.begin_writer(2); f.bank.drive(plain, 2.0f);
    f.bank.end_writer();
    CHECK(f.bank.writer_conflicts() == 1);
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


namespace {
/** @brief Declares one row as its output and never writes it. */
class Claims : public Plc::Program {
    const char* _row;
    Vars::RefOut<float> _out;
public:
    Claims(const char* name, const char* row, Vars::Bank& io,
           const Botanik::SetupContext& ctx)
        : Program(ctx, name, Tasks::GROUP_SLOW, io), _row(row) {}
protected:
    void bind(Vars::Bank& io) override { _out = io.resolve_out<float>(_row); }
    void run(ProgramContext&) override {}   // never writes: the CLAIM is the point
};
}  // namespace

TEST_CASE("claims: two producers of one row are caught at bring-up, unwritten") {
    // The conflict used to surface on whichever beat both first wrote, which for
    // a row only a fault path drives could be months in. Declaring is claiming.
    Live f;
    Claims a("prog_a", "cli_temp_ext1", f.bank, Botanik::SetupContext(f.reg));
    Claims b("prog_b", "cli_temp_ext1", f.bank, Botanik::SetupContext(f.reg));
    Plc::Program* const progs[] = { &a, &b };

    REQUIRE(f.bank.writer_conflicts() == 0);
    Cycle d(progs, 2, &f.bank, f.reg);      // registers, then setup() -> bind()

    INFO("no beat has run and neither program has written anything");
    CHECK(f.bank.writer_conflicts() == 1);
    CHECK(f.bank.first_conflict().index == f.ref("cli_temp_ext1").index);
}

TEST_CASE("claims: two programs on DIFFERENT rows is not a conflict") {
    Live f;
    Claims a("prog_a", "cli_temp_ext1", f.bank, Botanik::SetupContext(f.reg));
    Claims b("prog_b", "cli_temp_ext2", f.bank, Botanik::SetupContext(f.reg));
    Plc::Program* const progs[] = { &a, &b };
    Cycle d(progs, 2, &f.bank, f.reg);

    CHECK(f.bank.writer_conflicts() == 0);
    CHECK(f.bank.writer_of(f.ref("cli_temp_ext1")) != WRITER_NONE);
    CHECK(f.bank.writer_of(f.ref("cli_temp_ext1")) !=
          f.bank.writer_of(f.ref("cli_temp_ext2")));
}

TEST_CASE("claims: a VAR_IN_OUT row is shared by declaration, so no conflict") {
    Live f;
    Vars::RefInOut<float> x = f.bank.resolve_inout<float>("cli_temp_ext1");
    Claims a("prog_a", "cli_temp_ext1", f.bank, Botanik::SetupContext(f.reg));
    Claims b("prog_b", "cli_temp_ext1", f.bank, Botanik::SetupContext(f.reg));
    Plc::Program* const progs[] = { &a, &b };
    Cycle d(progs, 2, &f.bank, f.reg);

    CHECK(x.r.index != Vars::VAR_NONE);
    CHECK(f.bank.writer_conflicts() == 0);
    INFO("and it records no owner: naming one would report whoever wrote last");
    CHECK(f.bank.writer_of(f.ref("cli_temp_ext1")) == WRITER_NONE);
}

namespace {
/** @brief Reports whatever health the case asks it to, so the base's rules can
 *  be checked without a real law. */
class Reporter : public Plc::Program {
public:
    int mode = 0;   // 0 healthy, 1 degraded, 2 faulted, 3 both
    Reporter(Vars::Bank& io, const Botanik::SetupContext& ctx)
        : Program(ctx, "reporter", Tasks::GROUP_SLOW, io) {}
protected:
    void run(ProgramContext&) override {
        if (mode == 1 || mode == 3) degraded();
        if (mode == 2 || mode == 3) faulted();
    }
};
}  // namespace

TEST_CASE("health: SAFE before the first activation, and again when control goes") {
    Live f;
    Reporter r(f.bank, Botanik::SetupContext(f.reg));
    Plc::Program* const progs[] = { &r };

    INFO("registered and bound, but no beat has run: the plant is not in hand");
    CHECK(r.health() == Plc::Health::INHIBITED);

    Cycle d(progs, 1, &f.bank, f.reg);
    d.step(1000);
    CHECK(r.health() == Plc::Health::HEALTHY);

    // Withdrawal stops the activations. Health is the one thing a stopped
    // program must NOT keep reporting: the last beat's verdict would read as a
    // plant still under control.
    f.bank.set_health_hold(true);
    d.step(2000);
    CHECK(r.health() == Plc::Health::INHIBITED);

    // Re-enabling reseeds the timer, so the next activation is a full period
    // away rather than immediate: step until it has actually run again.
    f.bank.set_health_hold(false);
    for (uint64_t t = 3000; t <= 9000; t += 500) d.step(t);
    CHECK(r.health() == Plc::Health::HEALTHY);
}

TEST_CASE("health: worst wins within one activation, and it is not sticky") {
    Live f;
    Reporter r(f.bank, Botanik::SetupContext(f.reg));
    Plc::Program* const progs[] = { &r };
    Cycle d(progs, 1, &f.bank, f.reg);

    uint64_t t = 0;
    r.mode = 1; d.step(t += 1000);
    CHECK(r.health() == Plc::Health::DEGRADED);

    INFO("an output it could not produce outranks an input it could not read");
    r.mode = 3; d.step(t += 1000);
    CHECK(r.health() == Plc::Health::FAULTED);

    r.mode = 2; d.step(t += 1000);
    CHECK(r.health() == Plc::Health::FAULTED);

    INFO("and a fault that clears is not remembered: it is re-derived each beat");
    r.mode = 0; d.step(t += 1000);
    CHECK(r.health() == Plc::Health::HEALTHY);
}

namespace {
/** @brief Takes three activations to park, like an actuator with travel. */
class Parker : public Plc::Program {
public:
    int  beats_to_park = 3;
    int  runs = 0, parks = 0;
    Parker(Vars::Bank& io, const Botanik::SetupContext& ctx)
        : Program(ctx, "parker", Tasks::GROUP_SLOW, io) {}
protected:
    // The parking IS the control law: one run(), which drives to a demand or to
    // the fail position depending on what has been asked of it.
    void run(ProgramContext&) override {
        if (!inhibit_requested()) { runs++; return; }
        if (++parks < beats_to_park) parking();   // still travelling
    }
};
}  // namespace

TEST_CASE("safe: a program is still ACTIVATED while it parks, and run() stops") {
    Live f;
    Parker p(f.bank, Botanik::SetupContext(f.reg));
    Plc::Program* const progs[] = { &p };
    Cycle d(progs, 1, &f.bank, f.reg);

    uint64_t t = 0;
    for (int i = 0; i < 3; i++) d.step(t += 1000);
    REQUIRE(p.runs > 0);
    const int ran = p.runs;

    f.reg.request_safe(true);
    CHECK_FALSE(f.reg.all_safe());
    CHECK(f.reg.unsafe_count() == 1);

    // Parking is WORK: it takes activations. A supervisor that dropped control
    // the instant the operator asked would leave an actuator mid-travel.
    d.step(t += 1000);
    CHECK_FALSE(f.reg.all_safe());
    CHECK(p.health() != Plc::Health::INHIBITED);

    d.step(t += 1000);
    d.step(t += 1000);
    CHECK(p.health() == Plc::Health::INHIBITED);
    CHECK(f.reg.all_safe());
    CHECK(f.reg.unsafe_count() == 0);

    INFO("and the control law stopped deciding the moment it was asked");
    CHECK(p.runs == ran);
    CHECK(p.parks == 3);
}

TEST_CASE("safe: releasing the request puts the program back to work") {
    Live f;
    Parker p(f.bank, Botanik::SetupContext(f.reg));
    Plc::Program* const progs[] = { &p };
    Cycle d(progs, 1, &f.bank, f.reg);

    uint64_t t = 0;
    f.reg.request_safe(true);
    for (int i = 0; i < 4; i++) d.step(t += 1000);
    REQUIRE(p.health() == Plc::Health::INHIBITED);

    f.reg.request_safe(false);
    p.parks = 0;
    d.step(t += 1000);
    CHECK(p.health() == Plc::Health::HEALTHY);
}

TEST_CASE("inhibit: the operator's switch parks exactly as a safe request does") {
    Live f;
    Parker p(f.bank, Botanik::SetupContext(f.reg));
    p.beats_to_park = 2;
    Plc::Program* const progs[] = { &p };
    Cycle d(progs, 1, &f.bank, f.reg);

    uint64_t t = 0;
    p.request_disable(true);
    d.step(t += 1000);
    CHECK(p.health() != Plc::Health::INHIBITED);   // still travelling
    d.step(t += 1000);
    CHECK(p.health() == Plc::Health::INHIBITED);
    CHECK(p.is_parked());

    INFO("a supervisor waiting on the plant counts it as arrived either way");
    CHECK(f.reg.all_safe());

    INFO("one state, two causes: which asked is a request flag, not a value "
         "every reader would have to treat alike");
    CHECK(p.disable_requested());
    CHECK_FALSE(p.safe_requested());
    f.reg.request_safe(true);
    CHECK(p.safe_requested());
    CHECK(p.health() == Plc::Health::INHIBITED);   // already there; nothing to redo
}

namespace {
/** @brief Controls in run(), reports in observe() - the split every status and
 *  alarm program relies on. */
class Watcher : public Plc::Program {
public:
    int controls = 0, observes = 0, parks = 0;
    Watcher(Vars::Bank& io, const Botanik::SetupContext& ctx)
        : Program(ctx, "watcher", Tasks::GROUP_SLOW, io) {}
protected:
    void run(ProgramContext&) override {
        if (!inhibit_requested()) { controls++; return; }
        if (++parks < 2) parking();
    }
    void observe(ProgramContext&) override { observes++; }
};
}  // namespace

TEST_CASE("observe: reporting continues while parked, and control does not") {
    // A plant going into its safe state is exactly when someone is watching. A
    // status row that froze at the moment of the request would be stale AND
    // look live, which is worse than being absent.
    Live f;
    Watcher w(f.bank, Botanik::SetupContext(f.reg));
    Plc::Program* const progs[] = { &w };
    Cycle d(progs, 1, &f.bank, f.reg);

    uint64_t t = 0;
    for (int i = 0; i < 3; i++) d.step(t += 1000);
    REQUIRE(w.controls > 0);
    REQUIRE(w.observes == w.controls);

    const int controlled = w.controls;
    f.reg.request_safe(true);
    for (int i = 0; i < 4; i++) d.step(t += 1000);
    REQUIRE(w.health() == Plc::Health::INHIBITED);

    INFO("the control law stopped deciding");
    CHECK(w.controls == controlled);
    INFO("...and the reporting did not");
    CHECK(w.observes > controlled);

    // Same again when the operator switches one program off rather than the
    // whole plant going safe.
    f.reg.request_safe(false);
    for (int i = 0; i < 3; i++) d.step(t += 1000);
    const int seen = w.observes;
    w.parks = 0;
    w.request_disable(true);
    for (int i = 0; i < 3; i++) d.step(t += 1000);
    REQUIRE(w.health() == Plc::Health::INHIBITED);
    CHECK(w.observes > seen);
}

TEST_CASE("health: the order is worst-last, so a supervisor can take a maximum") {
    using H = Plc::Health;
    CHECK(H::HEALTHY < H::DEGRADED);
    CHECK(H::DEGRADED < H::FAULTED);

    INFO("INHIBITED is off the severity axis: control deliberately stopped is "
         "not a fault, and comparing it to one is meaningless");
    CHECK(Plc::health_is_severity(H::HEALTHY));
    CHECK(Plc::health_is_severity(H::FAULTED));
    CHECK_FALSE(Plc::health_is_severity(H::INHIBITED));
}

}  // namespace plc_io
