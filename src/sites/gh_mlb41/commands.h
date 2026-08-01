#ifndef SITE_COMMANDS_H
#define SITE_COMMANDS_H

/**
 * @file commands.h
 * @brief HAND-WRITTEN - the gh_mlb41 shell commands.
 *
 * Not generated: a command is code, not data, so this file is yours to edit and
 * the generator never touches it. Only the table's shape is fixed. Commands are
 * matched BEFORE the library built-ins, so a name reused here shadows one -
 * avoid exit/reset.
 *
 * The ctx passed to each handler is the live `const Vars::Io*` (wired by
 * the firmware base from the descriptor), so a command reads fresh variable data
 * without holding any state of its own.
 */

#include <cstdint>
#include <cstdio>    // snprintf

#include "botanik.h"

#include "vars.h"

namespace Site {

/** @brief The site's shell command table: name, one-line help, handler. */
const Botanik::Shell::ShellCommand* const g_commands = nullptr;
const uint8_t g_command_count = 0;

}  // namespace Site

#endif // SITE_COMMANDS_H
