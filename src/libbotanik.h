// REVIEW_35: TODO
#ifndef LIBBOTANIK_H
#define LIBBOTANIK_H

/**
 * @file libbotanik.h
 * @brief Umbrella header - pulls in the full libbotanik public API in one
 *  include.
 */

// Every include here is RE-EXPORTED, not used: the job of this header is to give
// a site one include for the public API. Each trailing note says what that header
// contributes to the API, not a dependency of this file.
#include "libbotanik/alarms/alarms.h"           // AlarmController / AlarmCondition
#include "libbotanik/console/console.h"         // Console
#include "libbotanik/console/editor.h"          // TextEditor + EditorBuffers
#include "libbotanik/console/parser.h"          // InputParser / InputEvent
#include "libbotanik/console/terminal.h"        // AnsiTerminal
#include "libbotanik/core/context.h"            // SystemContext
#include "libbotanik/hal/platform.h"            // the whole HAL surface
#include "libbotanik/logs/entry.h"              // LogEntry
#include "libbotanik/logs/logger.h"             // Logger
#include "libbotanik/logs/writer.h"             // LogWriter / BufferedLogWriter / SerialLogWriter
#include "libbotanik/pages/alarms.h"            // AlarmsPage
#include "libbotanik/pages/home.h"              // HomePage
#include "libbotanik/pages/logs.h"              // LogsPage
#include "libbotanik/pages/monitor.h"           // MonitorPage
#include "libbotanik/pages/notes.h"             // NotesPage
#include "libbotanik/pages/params_page.h"       // ParamsPage
#include "libbotanik/pages/shell.h"             // ShellPage
#include "libbotanik/pages/status.h"            // StatusPage
#include "libbotanik/pages/tasks_page.h"        // TasksPage
#include "libbotanik/plc/blocks/blocks.h"       // the stateful POU vocabulary
#include "libbotanik/plc/functions/functions.h" // the stateless POU vocabulary
#include "libbotanik/scheduler/scheduler.h"     // LoopScheduler + SchedulerContext
#include "libbotanik/tasks/task_timer.h"    // TaskTimer
#include "libbotanik/shell/shell.h"             // CommandShell + ShellCommand (the site's command table)
#include "libbotanik/utils/serial.h"            // BufferedSerial
#include "libbotanik/utils/time.h"              // CalendarTime + the uptime/wall conversions
#include "libbotanik/vars/access.h"             // var_group / var_published / HomeVar
#include "libbotanik/vars/declare.h"            // the A/P/G/SV/MI/AI/DI/MO/AO/DO/RO factories a table is written in
#include "libbotanik/library_vars.h"            // BOTANIK_LIBRARY_VARS
#include "libbotanik/vars/model.h"              // Variable / Value / Var / Bank / VarTables
#include "libbotanik/vars/params.h"             // Params / KvParamStore

// Last on purpose: BotanikFirmware names types from nearly every header above,
// so it can only be declared once they exist.
#include "libbotanik/firmware/firmware.h"           // BotanikFirmware

#endif // LIBBOTANIK_H
