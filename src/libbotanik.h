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
#include "libbotanik/alarms/alarms.h"
#include "libbotanik/console/console.h"
#include "libbotanik/console/editor.h"
#include "libbotanik/console/parser.h"
#include "libbotanik/console/terminal.h"
#include "libbotanik/core/context.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/logs/entry.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/logs/writer.h"
#include "libbotanik/pages/alarms.h"
#include "libbotanik/pages/home.h"
#include "libbotanik/pages/logs.h"
#include "libbotanik/pages/monitor.h"
#include "libbotanik/pages/notes.h"
#include "libbotanik/pages/params_page.h"
#include "libbotanik/pages/shell.h"
#include "libbotanik/pages/status.h"
#include "libbotanik/pages/tasks_page.h"
#include "libbotanik/plc/blocks/blocks.h"
#include "libbotanik/plc/functions/functions.h"
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/tasks/task_timer.h"
#include "libbotanik/shell/shell.h"
#include "libbotanik/utils/serial.h"
#include "libbotanik/utils/time.h"
#include "libbotanik/vars/access.h"
#include "libbotanik/vars/declare.h"
#include "libbotanik/vars/model.h"
#include "libbotanik/vars/params.h"

// Last on purpose: BotanikFirmware names types from nearly every header above,
// so it can only be declared once they exist.
#include "libbotanik/firmware/firmware.h"

#endif // LIBBOTANIK_H
