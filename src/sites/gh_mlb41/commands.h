#ifndef SITE_COMMANDS_H
#define SITE_COMMANDS_H

/**
 * @file commands.h
 * @brief HAND-WRITTEN - the gh_mlb41 shell commands.
 *
 * Not generated: a command is code, not data, so this file is yours to edit and
 * the generator never touches it. Only the table's shape is fixed. Commands are
 * registered after the library built-ins, so they cannot shadow exit/reset.
 *
 * The ctx passed to each handler is the live `const Vars::Io*` (wired by
 * the firmware base from the descriptor), so a command reads fresh PLC data
 * without holding any state of its own.
 */

#include <cstdint>
#include <cstdio>    // snprintf

#include "botanik.h"

#include "io.h"

namespace Site {

/** @brief Example site command: a one-line greenhouse summary read straight
 *  off the variable tables. */
inline void cmd_gh(Botanik::Shell::ShellWriter& out, const char*, uint16_t, void* ctx) {
    const Botanik::Vars::Io* io = static_cast<const Botanik::Vars::Io*>(ctx);
    char buf[96];
    int n;
    if (io == nullptr) {
        n = snprintf(buf, sizeof(buf), "%s: no variables bound", Botanik::Config::SITE_NAME);
    } else {
        // Report validity, not just the number: a supply reading of 0.0 V is
        // what an unanswered meter looks like, and it should not read as an
        // outage the operator goes chasing.
        Botanik::Vars::AnalogIn ln = io->ai(AI_PWR_LN_V);
        Botanik::Vars::AnalogIn w  = io->ai(AI_PWR_TOTAL_W);
        n = snprintf(buf, sizeof(buf), "%s: supply %.1f V%s, %.0f W, windows %s",
                     Botanik::Config::SITE_NAME,
                     (double)ln.value(), ln.valid() ? "" : " (no data)",
                     (double)w.value(),
                     io->id(ID_SYS_SUPPLY_LOCKOUT).value() ? "LOCKED OUT" : "free");
    }
    if (n > 0) out.write(buf, (size_t)n);
}

/** @brief The site's shell command table: name, one-line help, handler. */
const Botanik::Shell::ShellCommand g_commands[] = {
    { "gh", "greenhouse summary (live variables)", &cmd_gh },
};
const uint8_t g_command_count = (uint8_t)(sizeof(g_commands) / sizeof(g_commands[0]));

}  // namespace Site

#endif // SITE_COMMANDS_H
