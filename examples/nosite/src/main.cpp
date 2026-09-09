/**
 * @file main.cpp
 * @brief libbotanik with no site: the console, its shell and the task view,
 *        reused as components.
 *
 * There is no variable table here, no control programs and no BotanikFirmware.
 * Each component is constructed directly and configured through its own
 * set_config(), which is the path that exists whether or not anything binds
 * rows to it. What a site normally supplies - the geometry, the baud rate, the
 * terminal type - this program states itself.
 */

#include <cstdio>

#include "libbotanik/console/console.h"
#include "libbotanik/console/editor.h"
#include "libbotanik/core/setup.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/pages/shell.h"
#include "libbotanik/pages/tasks_page.h"
#include "libbotanik/tasks/tasks.h"
#include "libbotanik/utils/serial.h"

using namespace Botanik;

int main() {
    Tasks::Registry reg;
    Logs::Logger    log;
    SetupContext    ctx(reg, &log);

    Utils::BufferedSerial serial(HAL::Serial::get(HAL::Serial::CONSOLE));
    uint64_t              now_ms = 0;

    Terminal::Console console(ctx, serial, &now_ms);

    // Where a site would hand over rows, this is a struct and one call.
    Terminal::ConsoleConfig cfg;
    cfg.baud = 115200;
    cfg.cols = 80;
    cfg.rows = 24;
    console.set_config(cfg, /*announce=*/false);

    // Both pages take what they draw, not a bank: the shell's SystemContext is
    // borrowed and may be null, and the task view reads the registry.
    Pages::ShellPage shell_page(console);
    Pages::TasksPage tasks_page(console, reg);
    console.add_page(&shell_page);
    console.add_page(&tasks_page);

    console.setup(now_ms);
    for (uint8_t i = 0; i < 4; i++) {
        now_ms += Config::TASK_PERIOD_MS;
        reg.run_phase(Tasks::PHASE_INPUT, now_ms);
        reg.run_phase(Tasks::PHASE_PRESENT, now_ms);
    }

    printf("no site: %ux%u, %u pages, %u tasks\n",
           (unsigned)console.terminal().cols(), (unsigned)console.rows(),
           (unsigned)console.page_count(), (unsigned)reg.count());
    return 0;
}
