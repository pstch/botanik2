/**
 * @file test_table_golden.cpp
 * @brief An equivalence snapshot of the frozen table: every descriptor field of
 *        every row, against a committed golden.
 *
 * Its one job is to survive the move of the tables out of C++ and into the CSVs.
 * Section bytes cannot do it - splitting three arrays into eleven files changes
 * the layout legitimately while the table does not change at all - so this
 * compares what the FIRMWARE sees, through a frozen bank.
 *
 * Rows are compared SORTED BY NAME, because the split reorders them. Table
 * order is asserted separately, and only where it is load-bearing.
 *
 * Regenerate with `make -C build table-golden-update`, then read the diff.
 */

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "doctest.h"

#include "libbotanik_site.h"
#include "libbotanik.h"
#include BOTANIK_SITE_HEADER
#include "site_bank.h"

namespace {

using namespace Botanik::Vars;

const char* const GOLDEN = BOTANIK_TEST_DATA_DIR "/table_golden.txt";

/** @brief A float as its exact bits: a decimal rendering would make the golden
 *  hostage to the host's formatting. */
std::string bits(float f) {
    uint32_t u; std::memcpy(&u, &f, 4);
    char b[16]; std::snprintf(b, sizeof(b), "%08x", u);
    return b;
}
std::string bits(Scalar s) {
    char b[24]; std::snprintf(b, sizeof(b), "%016llx", (unsigned long long)s.u);
    return b;
}
std::string text(const char* s) { return (s == nullptr) ? std::string("<null>") : std::string(s); }

/** @brief One tunable: the parameter it reads, or its fixed value. */
std::string tunable(const Tunable& t) {
    return (t.param != nullptr) ? (std::string("R:") + t.param + "@" + bits(t.k))
                                : (std::string("K:") + bits(t.k));
}

/** @brief The half of a row that depends on its kind, through the checked
 *  accessors - never the union arm directly. */
std::string payload(const Descriptor& d) {
    std::ostringstream o;
    switch (payload_arm(d)) {
        case PAY_NUM: {
            const NumBand b = num_band(d);
            o << "num[" << bits(b.min) << "," << bits(b.max) << "]";
            break;
        }
        case PAY_TEXT: {
            const TextSlot t = text_slot(d);
            o << "text[cap=" << t.cap << ",kind=" << (int)t.kind << "]";
            break;
        }
        case PAY_ALARM: {
            const AlarmSpec a = alarm_spec(d);
            o << "alarm[code=" << text(a.code) << ",on=" << a.on_s << ",off=" << a.off_s
              << ",recent=" << a.recent_s << ",hold=" << a.hold_s << "]";
            break;
        }
        case PAY_ANALOG: {
            const AnalogCal* c = analog_cal(d);
            if (c == nullptr) { o << "analog[<null>]"; break; }
            o << "analog[" << tunable(c->raw_min) << ";" << tunable(c->raw_max)
              << ";" << tunable(c->eng_min) << ";" << tunable(c->eng_max)
              << ";" << tunable(c->min) << ";" << tunable(c->max) << "]";
            break;
        }
    }
    return o.str();
}

/** @brief One row, whole. Every field the descriptor carries, prose included: a
 *  migration that drops a note has lost something worth failing on. */
std::string row(const Descriptor& d) {
    std::ostringstream o;
    o << text(d.name)
      << " type=" << (int)d.type << " role=" << (int)d.role << " scope=" << (int)d.scope
      << " digits=" << (int)d.digits << " flags=0x" << std::hex << (int)d.flags << std::dec
      << " def=" << (d.type == T_STR ? text(d.def.s) : bits(d.def))
      << " unit=" << text(d.unit)
      << " pay=" << payload(d)
      << " bind[xp=" << (int)d.bind.transport << ",wire=" << (int)d.bind.wire
      << ",dev=" << (int)d.bind.device << ",fc=" << (int)d.bind.fc
      << ",reg=" << d.bind.reg << ",regs=" << d.bind.regs
      << ",scale=" << bits(d.bind.scale) << ",offset=" << bits(d.bind.offset) << "]"
      << " timeout=" << d.timeout_ms << " poll=" << d.poll_ms
      << " str=" << (d.str != nullptr ? 'y' : 'n')
      << " desc=" << text(d.desc)
      << " notes=" << text(d.notes);
    return o.str();
}

/** @brief Every row of a frozen bank, sorted by name. */
std::vector<std::string> dump(const Bank& io) {
    std::vector<std::string> out;
    out.reserve(io.count());
    for (uint16_t i = 0; i < io.count(); i++) out.push_back(row(io.descriptor(i)));
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> read_lines(const char* path) {
    std::vector<std::string> out;
    std::ifstream f(path);
    for (std::string l; std::getline(f, l); ) if (!l.empty()) out.push_back(l);
    return out;
}

}  // namespace

TEST_CASE("table golden: every descriptor field survives, row for row") {
    Bank io;
    TestSite::build(io);
    const std::vector<std::string> now = dump(io);

    if (std::getenv("BOTANIK_GOLDEN_UPDATE") != nullptr) {
        std::ofstream f(GOLDEN, std::ios::trunc);
        REQUIRE_MESSAGE(f.good(), "cannot write the golden: " << GOLDEN);
        for (const std::string& l : now) f << l << "\n";
        MESSAGE("golden rewritten with " << now.size() << " rows - review the diff");
        return;
    }

    const std::vector<std::string> was = read_lines(GOLDEN);
    REQUIRE_MESSAGE(!was.empty(),
                    "no golden at " << GOLDEN << " - run `make -C build table-golden-update`");

    // Name-keyed, so a row that MOVED is not reported as every row after it
    // changing. The three ways a table can differ are named separately.
    for (size_t i = 0; i < was.size() && i < now.size(); i++) {
        if (was[i] != now[i]) {
            const std::string name_was = was[i].substr(0, was[i].find(' '));
            const std::string name_now = now[i].substr(0, now[i].find(' '));
            INFO("first difference at sorted position " << i);
            INFO("golden: " << was[i]);
            INFO("now   : " << now[i]);
            if (name_was != name_now) FAIL("row '" << name_was << "' is gone, or '"
                                                   << name_now << "' is new");
            FAIL("row '" << name_was << "' changed");
        }
    }
    CHECK_MESSAGE(now.size() == was.size(),
                  "the table gained or lost rows: " << was.size() << " -> " << now.size());
}

TEST_CASE("table golden: the load-bearing order still holds") {
    // The split reorders the table, which is why the dump above is sorted. Three
    // properties are NOT free to move, each for its own reason.
    Bank io;
    TestSite::build(io);

    SUBCASE("the alarm rows keep their relative order") {
        // AlarmController scans the table in order, and an alarm's position is
        // what the ALARMS page lists it at.
        std::vector<std::string> seen;
        for (uint16_t i = 0; i < io.count(); i++) {
            const Descriptor& d = io.descriptor(i);
            if (d.scope == SCOPE_A) seen.push_back(text(d.name));
        }
        const std::vector<std::string> was = read_lines(BOTANIK_TEST_DATA_DIR "/alarm_order.txt");
        if (std::getenv("BOTANIK_GOLDEN_UPDATE") != nullptr) {
            std::ofstream f(BOTANIK_TEST_DATA_DIR "/alarm_order.txt", std::ios::trunc);
            for (const std::string& n : seen) f << n << "\n";
            return;
        }
        REQUIRE(!was.empty());
        CHECK(seen == was);
    }

    SUBCASE("the parameters are one run, and it starts where it started") {
        // The PARAMS page and the shell address the block by a dense 0-based
        // index, so the run must stay contiguous; freeze() names the row if not.
        CHECK(io.param_stray_count() == 0);
        CHECK(io.param_count() > 0);
        for (uint16_t k = 0; k < io.param_count(); k++)
            CHECK(io.param_at(k).role() == ROLE_PARAM);
    }

    SUBCASE("every text row still has a slot of its own") {
        // Pool offsets are derived from TABLE ORDER, so reordering relays into
        // them; overlapping slots would be silent corruption.
        CHECK(io.pool_overflow() == 0);
        for (uint16_t i = 0; i < io.count(); i++) {
            const Descriptor& d = io.descriptor(i);
            if (d.type != T_STR) continue;
            uint16_t cap = 0;
            CHECK(io.text(io.var(i), &cap) != nullptr);
            CHECK(cap == text_slot(d).cap);
        }
    }
}
