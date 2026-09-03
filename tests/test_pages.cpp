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

}  // namespace page_convergence
