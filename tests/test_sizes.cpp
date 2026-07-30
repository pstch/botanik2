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
 * @see @ref cpptests
 */
#include <cstddef>

#include "doctest.h"

#include "libbotanik.h"            // all library types (pages, terminal, editor, plcio)
#include "libbotanik_site.h"       // BOTANIK_SITE -> BOTANIK_SITE_HEADER
#include BOTANIK_SITE_HEADER       // selected site: PlcInputData_t / PlcOutputData_t

// Concrete instantiations of the templated pages/PLC structs, matching what the
// simulator actually builds (see botanik_sim/main.cpp).
using HomePage_t   = Botanik::Pages::HomePage;
using StatusPage_t = Botanik::Pages::StatusPage<PlcOutputData_t>;

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
    // AnsiTerminal owns the screen back-buffer: two GRID_ROWS x GRID_COLS cell
    // grids (front + back, ~8 KB) diffed by present(); its size tracks the grid
    // dimensions, not the VT device state (~100 B).
    REPORT_SIZE(Botanik::Terminal::AnsiTerminal, 12288);
    REPORT_SIZE(Botanik::Terminal::TextEditor,   2048);
    REPORT_SIZE(Botanik::Terminal::InputParser,   512);
}

TEST_CASE("sizeof: pages") {
    REPORT_SIZE(HomePage_t,                32768);
    REPORT_SIZE(StatusPage_t,               4096);
    REPORT_SIZE(Botanik::Pages::LogsPage,  65536);
    REPORT_SIZE(Botanik::Pages::ShellPage, 65536);
}

TEST_CASE("sizeof: site data struct") {
    // The site data struct is plain RAM now - no shared region, no alignment or
    // cache contract to honour. Only its bulk still matters.
    REPORT_SIZE(PlcOutputData_t, 16384);
}
