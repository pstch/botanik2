/**
 * @file test_pages.cpp
 * @brief Every page must leave the screen where a full repaint would, at every
 *        geometry the operator can ask for.
 *
 * A page reconciles its own rows, so a desync between what it thinks is on
 * screen and what is there survives the terminal's diff: FRONT and BACK agree
 * and the wrong glyph stays until a Ctrl+L. That is the class HOME's log band
 * belonged to, and it sat inside the supported range unnoticed because every
 * full-screen golden runs at 80x24.
 *
 * `console_cols` is banded 80..132 and `console_rows` 20..32, so the cases here
 * run the two ends and the default.
 */

#include "doctest.h"

#include "libbotanik.h"

#include "page_fixture.h"

namespace page_convergence {

using namespace Botanik;

/** @brief The geometries a page is actually asked to render at: the minimum
 *  the console clamps to, the default, and the widest it accepts. */
struct Geometry { uint16_t rows, cols; const char* name; };
static const Geometry GEOMETRIES[] = {
    { 20,  80, "80x20 (the minimum)" },
    { 24,  80, "80x24 (the default)" },
    { 32, 132, "132x32 (the maximum)" },
};

#if BOTANIK_FEATURE_TASKS_PAGE
TEST_CASE("pages: TASKS converges at every supported geometry") {
    for (const Geometry& g : GEOMETRIES) {
        INFO(g.name);
        botanik_test::PageFixture f(g.rows, g.cols);
        Tasks::Registry reg;
        Pages::TasksPage page(f.console, reg);

        page.focus();
        f.paint(page);
        f.converged(page, "TASKS after focus");
    }
}

TEST_CASE("pages: TASKS converges after the selection moves") {
    botanik_test::PageFixture f;
    Tasks::Registry reg;
    Pages::TasksPage page(f.console, reg);

    page.focus();
    f.paint(page);
    page.handle_command(Terminal::InputEvent::Command::DOWN, 1);
    f.paint(page);
    f.converged(page, "TASKS after DOWN");
}
#endif

#if BOTANIK_FEATURE_LOGS_PAGE
TEST_CASE("pages: LOGS converges at every supported geometry") {
    for (const Geometry& g : GEOMETRIES) {
        INFO(g.name);
        botanik_test::PageFixture f(g.rows, g.cols);
        Pages::LogsPage page(f.console);

        page.focus();
        f.paint(page);
        f.converged(page, "LOGS after focus");
    }
}
#endif


#if BOTANIK_FEATURE_SYSTEM_PAGE
TEST_CASE("pages: SYSTEM converges at every supported geometry") {
    Scheduler::SchedulerContext sched;
    sched.run_state       = Scheduler::RunState::RUN;
    sched.watchdog_ms     = 10000;
    sched.ticks_per_second = 100;
    Tasks::Registry reg;
    SystemContext sys;
    sys.scheduler = &sched;
    sys.tasks     = &reg;

    for (const Geometry& g : GEOMETRIES) {
        INFO(g.name);
        botanik_test::PageFixture f(g.rows, g.cols);
        Pages::StatusPage page(f.console, &sys);

        page.focus();
        f.paint(page);
        f.converged(page, "SYSTEM after focus");
    }
}

TEST_CASE("pages: SYSTEM renders with no SystemContext at all") {
    // The base documents a null aggregate and used to dereference it. A rig
    // that has no SystemContext should get a blank band, not a fault.
    botanik_test::PageFixture f;
    Pages::StatusPage page(f.console, nullptr);

    page.focus();
    f.paint(page);
    f.converged(page, "SYSTEM with a null context");

    INFO("the labels are still there - only the values are missing");
    bool saw_label = false;
    for (const std::string& row : f.content())
        if (row.find("Run state") != std::string::npos) saw_label = true;
    CHECK(saw_label);
}

TEST_CASE("pages: SYSTEM says what the task table costs") {
    Scheduler::SchedulerContext sched;
    Tasks::Registry reg;
    SystemContext sys;
    sys.scheduler = &sched;
    sys.tasks     = &reg;

    botanik_test::PageFixture f;
    Pages::StatusPage page(f.console, &sys);
    page.focus();
    f.paint(page);

    // An empty registry is still a number, and the row must render it rather
    // than being skipped.
    bool saw_tasks = false;
    for (const std::string& row : f.content())
        if (row.find("Tasks") != std::string::npos) saw_tasks = true;
    CHECK(saw_tasks);
}
#endif

}  // namespace page_convergence
