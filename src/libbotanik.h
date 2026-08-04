// REVIEW_35: TODO
#ifndef LIBBOTANIK_H
#define LIBBOTANIK_H

/**
 * @file libbotanik.h
 * @brief Umbrella header - pulls in the full libbotanik public API in one
 *  include.
 */

#include "libbotanik/hal/platform.h"

#include "libbotanik/core/context.h"
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/scheduler/task_timer.h"

#include "libbotanik/logs/logger.h"
#include "libbotanik/logs/writer.h"
#include "libbotanik/logs/entry.h"

#include "libbotanik/utils/serial.h"
#include "libbotanik/utils/time.h"

#include "libbotanik/vars/declare.h"
#include "libbotanik/vars/model.h"
#include "libbotanik/vars/params.h"
#include "libbotanik/vars/access.h"

#include "libbotanik/scan/scan.h"
#include "libbotanik/scan/blocks/blocks.h"
#include "libbotanik/alarms/alarms.h"


#include "libbotanik/shell/shell.h"

#include "libbotanik/pages/home.h"
#include "libbotanik/pages/shell.h"
#include "libbotanik/pages/logs.h"
#include "libbotanik/pages/alarms.h"
#include "libbotanik/pages/params_page.h"
#include "libbotanik/pages/monitor.h"
#include "libbotanik/pages/notes.h"
#include "libbotanik/pages/status.h"

#include "libbotanik/console/terminal.h"
#include "libbotanik/console/console.h"
#include "libbotanik/console/parser.h"
#include "libbotanik/console/editor.h"


// firmware.h goes LAST, and it is load-bearing rather than tidiness: it owns a
// page of every kind as a member, and it includes THIS file back to get them.
// The include guard makes that back-include a no-op, so anything firmware.h
// needs must already be declared by the time we reach it. Moved up, it compiles
// against an empty Pages namespace and every member declaration fails.
#include "libbotanik/core/firmware.h"


#endif // LIBBOTANIK_H
