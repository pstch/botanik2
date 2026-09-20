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
#include "site_bank.h"

namespace tests { namespace page_convergence {

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
        INFO("geometry " << std::string(g.name));
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
        INFO("geometry " << std::string(g.name));
        botanik_test::PageFixture f(g.rows, g.cols);
        Pages::LogsPage page(f.console);

        // Enough entries to fill the tallest screen and scroll it, and one
        // longer than the narrowest, so the wrap runs too. An empty page
        // converges trivially, which is what this case did before.
        Logs::Logger logger;
        REQUIRE(logger.add_writer(&page));
        for (int i = 0; i < 40; i++)
            logger.info(Logs::STATUS, "entry %d, and a tail long enough to wrap "
                                      "at the narrowest console the operator "
                                      "can ask for", i);
        logger.refill();

        page.focus();
        f.paint(page);
        f.converged(page, "LOGS after focus");

        // Not convergence while scrolled: focus() resets the view,
        // so Ctrl+L on a scrolled log deliberately jumps to the newest and a
        // repaint is a DIFFERENT screen by design (pages/scrollback.h). What
        // is worth asserting is that the scroll moved the view at all, and
        // that coming back to the bottom converges again.
        const std::vector<std::string> at_bottom = f.content();
        page.handle_command(Terminal::InputEvent::Command::UP, 1);
        f.paint(page);
        CHECK(f.content() != at_bottom);

        // DOWN, not ENTER: ENTER clears the screen and re-replays, which is a
        // different act. One UP, one DOWN, back where we started.
        page.handle_command(Terminal::InputEvent::Command::DOWN, 1);
        f.paint(page);
        f.converged(page, "LOGS back at the bottom");
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
        INFO("geometry " << std::string(g.name));
        botanik_test::PageFixture f(g.rows, g.cols);
        Pages::SystemPage page(f.console, &sys);

        page.focus();
        f.paint(page);
        f.converged(page, "SYSTEM after focus");
    }
}

TEST_CASE("pages: SYSTEM renders with no SystemContext at all") {
    // The base documents a null aggregate, so a rig with no SystemContext gets
    // a blank band rather than a fault.
    botanik_test::PageFixture f;
    Pages::SystemPage page(f.console, nullptr);

    page.focus();
    f.paint(page);
    f.converged(page, "SYSTEM with a null context");

    INFO("the labels are still there - only the values are missing");
    bool saw_label = false;
    for (const std::string& row : f.content())
        if (row.find("Run state") != std::string::npos) saw_label = true;
    CHECK(saw_label);
}

TEST_CASE("pages: the SYSTEM band's site half carries labels, values AND units") {
    // The site half is a declared table, so nothing about it is derivable from
    // the page: a caption comes from the CSV and the unit from the row. A bare
    // "227.0" is a number, not a reading, which is why the home screen appends
    // the unit and why this one has to as well.
    Vars::Bank bank;
    TestSite::build(bank);
    Scheduler::SchedulerContext sched;
    Tasks::Registry reg;
    SystemContext sys;
    sys.scheduler = &sched;
    sys.tasks     = &reg;

    static const Vars::HomeVar FIELDS[] = {
        { "Warm-up",  Vars::Row::sys_startup_sec },
        { "Cool-off", Vars::Row::sys_shutdown_sec },
    };
    bank.var(FIELDS[0].row).set(45.0f);
    bank.var(FIELDS[1].row).set(30.0f);

    botanik_test::PageFixture f;
    Pages::SystemPage page(f.console, &sys, FIELDS, 2, &bank);
    page.focus();
    f.paint(page);
    f.converged(page, "SYSTEM with a site band");

    bool saw_label = false, saw_value = false;
    for (const std::string& row : f.content()) {
        if (row.find("Warm-up") != std::string::npos) saw_label = true;
        // The unit is the half that went missing: the value alone rendered
        // fine and read as unitless.
        if (row.find("45 sec") != std::string::npos) saw_value = true;
    }
    CHECK(saw_label);
    CHECK(saw_value);
}

TEST_CASE("pages: SYSTEM says what the task table costs") {
    Scheduler::SchedulerContext sched;
    Tasks::Registry reg;
    SystemContext sys;
    sys.scheduler = &sched;
    sys.tasks     = &reg;

    botanik_test::PageFixture f;
    Pages::SystemPage page(f.console, &sys);
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


#if BOTANIK_FEATURE_HOME_PAGE
TEST_CASE("pages: HOME converges at every supported geometry") {
    // The site's own home fields, not a stub: HOME lays out around what it is
    // given, and an empty field list exercises none of the placement that the
    // log band shares a screen with.
    for (const Geometry& g : GEOMETRIES) {
        INFO("geometry " << std::string(g.name));
        botanik_test::PageFixture f(g.rows, g.cols);
        Vars::Bank bank;
        TestSite::build(bank);
        Pages::HomePage page(f.console, Site::g_site.home_fields,
                             Site::g_site.home_field_count, &bank);

        page.focus();
        f.paint(page);
        f.converged(page, "HOME after focus");
    }
}
#endif

#if BOTANIK_FEATURE_PARAMS_PAGE
TEST_CASE("pages: PARAMS converges at every supported geometry") {
    for (const Geometry& g : GEOMETRIES) {
        INFO("geometry " << std::string(g.name));
        botanik_test::PageFixture f(g.rows, g.cols);
        Vars::Bank bank;
        TestSite::build(bank);
        Pages::ParamsPage page(f.console, &bank);

        page.focus();
        f.paint(page);
        f.converged(page, "PARAMS after focus");

        // A table page reflows around the selection, which is the state a
        // static screen never reaches.
        page.handle_command(Terminal::InputEvent::Command::DOWN, 1);
        f.paint(page);
        f.converged(page, "PARAMS after DOWN");
    }
}
#endif

#if BOTANIK_FEATURE_MONITOR_PAGE
TEST_CASE("pages: MONITOR converges at every supported geometry") {
    for (const Geometry& g : GEOMETRIES) {
        INFO("geometry " << std::string(g.name));
        botanik_test::PageFixture f(g.rows, g.cols);
        Vars::Bank bank;
        TestSite::build(bank);
        Pages::MonitorPage page(f.console, &bank);

        page.focus();
        f.paint(page);
        f.converged(page, "MONITOR after focus");

        page.handle_command(Terminal::InputEvent::Command::DOWN, 1);
        f.paint(page);
        f.converged(page, "MONITOR after DOWN");
    }
}
#endif

#if BOTANIK_FEATURE_ALARMS_PAGE
TEST_CASE("pages: ALARMS converges at every supported geometry") {
    for (const Geometry& g : GEOMETRIES) {
        INFO("geometry " << std::string(g.name));
        botanik_test::PageFixture f(g.rows, g.cols);
        Vars::Bank bank;
        TestSite::build(bank);
        Vars::BankAlarms sources;
        sources.bind(&bank);
        Alarms::AlarmController model(SetupContext(), &sources);
        Pages::AlarmsPage page(f.console, &model);

        page.focus();
        f.paint(page);
        f.converged(page, "ALARMS after focus");
    }
}
#endif

#if BOTANIK_FEATURE_NOTES_PAGE
TEST_CASE("pages: NOTES converges at every supported geometry") {
    for (const Geometry& g : GEOMETRIES) {
        INFO("geometry " << std::string(g.name));
        botanik_test::PageFixture f(g.rows, g.cols);
        Pages::NotesPage page(f.console);

        page.focus();
        f.paint(page);
        f.converged(page, "NOTES after focus");
    }
}
#endif

}}  // namespace tests::page_convergence
