/**
 * @file test_sizes.cpp
 * @brief Living documentation of the in-memory footprint of the "big" objects.
 *
 * The OPTA core is RAM-constrained, so the size of the long-lived UI and site
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

#include "libbotanik.h"            // all library types (pages, terminal, editor)
#include "libbotanik_site.h"       // BOTANIK_SITE -> BOTANIK_SITE_HEADER
#include BOTANIK_SITE_HEADER       // selected site: its tables + parameter image

// Concrete instantiations of the templated pages, matching what the
// simulator actually builds (see botanik_sim/main.cpp).
using HomePage_t   = Botanik::Pages::HomePage;
// StatusPage is an unregistered building block; its template parameter is what
// a site subclass draws in its own data rows. Nothing at this site has one
// any more - variables are reached through the bank - so it is sized here on a
// stand-in, and the parameter is arguably vestigial now.
struct StatusPayloadStandIn { uint32_t whatever; };
using StatusPage_t = Botanik::Pages::StatusPage<StatusPayloadStandIn>;

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
    // The editor borrows its text buffer and row arrays, so it is just cursor
    // and layout state; the bytes live in whatever EditorBuffers fed it.
    REPORT_SIZE(Botanik::Terminal::TextEditor,    256);
    REPORT_SIZE(Botanik::Terminal::InputParser,   512);
}

// The editors' actual footprint, one instantiation per job. These are the sizes
// that move when SHELL_INPUT_LEN / EDITOR_MAX_ROWS / NOTES_* change.
TEST_CASE("sizeof: editor buffers") {
    using ShellBufs = Botanik::Terminal::EditorBuffers<Botanik::Config::SHELL_INPUT_LEN,
                                                       Botanik::Config::EDITOR_MAX_ROWS>;
    REPORT_SIZE(ShellBufs, 2048);
}

TEST_CASE("sizeof: pages") {
    REPORT_SIZE(HomePage_t,                     32768);
    REPORT_SIZE(StatusPage_t,                    4096);
    REPORT_SIZE(Botanik::Pages::LogsPage,       65536);
    REPORT_SIZE(Botanik::Pages::ShellPage,      65536);
    REPORT_SIZE(Botanik::Pages::MetricPage,      4096);
    REPORT_SIZE(Botanik::Pages::ShellScrollback,16384);
    REPORT_SIZE(Botanik::Pages::LogsScrollback, 32768);
    REPORT_SIZE(Botanik::Pages::HomeScrollback,  4096);
}

TEST_CASE("sizeof: logging & serial") {
    REPORT_SIZE(Botanik::Logs::LogEntry,             1024);
    REPORT_SIZE(Botanik::Logs::Logger,               1024);
    REPORT_SIZE(Botanik::Logs::BufferedLogWriter,   16384);
    REPORT_SIZE(Botanik::Logs::SerialLogWriter,     16384);
    REPORT_SIZE(Botanik::Utils::BufferedSerial,      8192);
}

TEST_CASE("sizeof: the site's variable table and value store") {
    // The site payload is gone entirely: parameters, status, actuators and
    // alarms are all rows of one table now. The descriptors are flash, the
    // values are the only RAM cost.
    REPORT_SIZE(Botanik::Vars::Variable, 128);
    REPORT_SIZE(Botanik::Vars::Value, 32);
    MESSAGE("site variables = " << (int)Site::VAR_TOTAL);
    MESSAGE("descriptor table = " << sizeof(Site::g_vars) << " bytes (flash)");
    MESSAGE("value store = " << sizeof(Site::Store::values) << " bytes (RAM)");
    CHECK(sizeof(Site::g_vars) > 0);
}
