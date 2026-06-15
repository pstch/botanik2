/**
 * @file test_sizes.cpp
 * @brief Living documentation of the in-memory footprint of the "big" objects.
 *
 * The OPTA core is RAM-constrained, so the size of the long-lived UI and PLC
 * objects is a number worth watching. This suite does not pin exact byte counts
 * (they differ between the x86-64 host build and the ARM target) — it prints a
 * size table on every run and fails only if a structure grows past a generous
 * budget, catching accidental bloat without churning on every layout tweak.
 *
 * Run `ctest --output-on-failure` or `./botanik_tests -s` to see the table; the
 * reported sizes are the host-build figures.
 *
 * @see @ref testing
 */
#include <cstddef>

#include "doctest.h"

#include "libbotanik.h"            // all library types (pages, terminal, editor, plcio)
#include "botanik_sim/simulator.h" // site payloads: PlcInputData_t / PlcOutputData_t

// Concrete instantiations of the templated pages/PLC structs, matching what the
// simulator actually builds (see botanik_sim/main.cpp).
using HomePage_t   = Botanik::Pages::HomePage<PlcOutputData_t>;
using StatusPage_t = Botanik::Pages::StatusPage<PlcOutputData_t>;
using PlcInput_t   = Botanik::PlcIO::PlcInput<PlcInputData_t>;
using PlcOutput_t  = Botanik::PlcIO::PlcOutput<PlcOutputData_t>;

// Report a type's size and guard it against runaway growth. The budget is a
// loose ceiling — bump it deliberately if a structure legitimately needs it.
#define REPORT_SIZE(T, budget)                                               \
    do {                                                                     \
        MESSAGE(#T " = " << sizeof(T) << " bytes (alignof "                  \
                         << alignof(T) << ", budget " << (budget) << ")");   \
        CHECK(sizeof(T) > 0);                                                \
        CHECK(sizeof(T) <= (size_t)(budget));                                \
    } while (0)

// Budgets are loose round ceilings above the current host-build size — a
// regression tripwire for accidental bloat, not a tight spec. The page sizes are
// dominated by config-driven line/log buffers (scrollback + BufferedLogWriter),
// so they move with the Config::*_LINES / LOG_* knobs, not with code churn.
TEST_CASE("sizeof: terminal & input objects") {
    REPORT_SIZE(Botanik::Terminal::AnsiTerminal, 1024);
    REPORT_SIZE(Botanik::Terminal::TextEditor,   2048);
    REPORT_SIZE(Botanik::Terminal::InputParser,   512);
}

TEST_CASE("sizeof: pages") {
    REPORT_SIZE(HomePage_t,                32768);
    REPORT_SIZE(StatusPage_t,               4096);
    REPORT_SIZE(Botanik::Pages::LogsPage,  65536);
    REPORT_SIZE(Botanik::Pages::ShellPage, 65536);
}

TEST_CASE("sizeof: PLC shared-memory payloads") {
    REPORT_SIZE(PlcInput_t,    256);
    REPORT_SIZE(PlcOutput_t, 16384);
    // With a payload sized to a 4-byte multiple, the combined struct is
    // padding-free — the whole is exactly its parts (see plcio/vars.h). The
    // simulator's PlcOutputData_t satisfies that (static_assert in simulator.h);
    // the empty PlcInputData_t does not, so it may carry trailing padding.
    CHECK(sizeof(PlcOutput_t)
          == sizeof(Botanik::PlcIO::PlcOutputCounters_t)
           + sizeof(Botanik::PlcIO::PlcOutputLogs_t)
           + sizeof(PlcOutputData_t));
    CHECK(sizeof(PlcInput_t)
          >= sizeof(Botanik::PlcIO::PlcInputCounters_t)
           + sizeof(Botanik::PlcIO::PlcInputLogs_t)
           + sizeof(PlcInputData_t));
}
