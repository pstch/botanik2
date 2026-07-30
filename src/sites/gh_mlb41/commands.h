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
 * The ctx passed to each handler is the live `const SiteData_t*` (wired by
 * the firmware base from the descriptor), so a command reads fresh PLC data
 * without holding any state of its own.
 */

#include <cstdint>
#include <cstdio>    // snprintf

#include "botanik.h"

#include "data.h"

namespace Site {

/** @brief Example site command: a one-line greenhouse summary from live PLC
 *  data. */
inline void cmd_gh(Botanik::Shell::ShellWriter& out, const char*, uint16_t, void* ctx) {
    const SiteData_t* d = static_cast<const SiteData_t*>(ctx);
    char buf[96];
    int n;
    if (d == nullptr)
        n = snprintf(buf, sizeof(buf), "%s: no PLC data", Botanik::Config::SITE_NAME);
    else if (d->current_tray)
        n = snprintf(buf, sizeof(buf), "%s: tray %u %s, UPS %s, net %s",
                     Botanik::Config::SITE_NAME, (unsigned)d->current_tray,
                     TrayStatus::str(d->tray_status), UpsStatus::str(d->ups_status),
                     NetStatus::str(d->net_status));
    else
        n = snprintf(buf, sizeof(buf), "%s: idle, UPS %s, net %s",
                     Botanik::Config::SITE_NAME, UpsStatus::str(d->ups_status),
                     NetStatus::str(d->net_status));
    if (n > 0) out.write(buf, (size_t)n);
}

/** @brief The site's shell command table: name, one-line help, handler. */
const Botanik::Shell::ShellCommand g_commands[] = {
    { "gh", "greenhouse summary (live PLC data)", &cmd_gh },
};
const uint8_t g_command_count = (uint8_t)(sizeof(g_commands) / sizeof(g_commands[0]));

}  // namespace Site

#endif // SITE_COMMANDS_H
