// A consumer that wants the terminal stack and nothing else: no site, no
// firmware, no variable table. It configures the console through the component's
// own set_config(), which is the path that exists whether or not anything binds
// rows to it.
#include <cstdio>

#include "libbotanik/console/console.h"
#include "libbotanik/console/editor.h"
#include "libbotanik/core/setup.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/tasks/tasks.h"
#include "libbotanik/utils/serial.h"

using namespace Botanik;

int main() {
    Tasks::Registry  reg;
    Logs::Logger     log;
    SetupContext     ctx(reg, &log);

    Utils::BufferedSerial serial(HAL::Serial::get(HAL::Serial::CONSOLE));
    uint64_t              now_ms = 0;

    Terminal::Console console(ctx, serial, &now_ms);

    // The whole configuration surface, with no bank anywhere.
    Terminal::ConsoleConfig cfg;
    cfg.baud = 9600;
    cfg.cols = 80;
    cfg.rows = 24;
    cfg.type = Terminal::Ansi::TerminalType::MINITEL_2;
    console.set_config(cfg, /*announce=*/false);

    Terminal::EditorBuffers<256, 16> buffers;
    Terminal::TextEditor             editor(&console, buffers.storage());

    printf("cols=%u rows=%u editor=%zu\n",
           (unsigned)console.terminal().cols(), (unsigned)console.rows(),
           sizeof(editor));
    return 0;
}
