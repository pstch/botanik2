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

#include "libbotanik/logs/logger.h"
#include "libbotanik/logs/writer.h"

#include "libbotanik/pages/home.h"
#include "libbotanik/pages/shell.h"
#include "libbotanik/pages/logs.h"
#include "libbotanik/pages/alarms.h"
#include "libbotanik/pages/params_page.h"
#include "libbotanik/pages/monitor.h"
// StatusPage is a reusable building block, kept but NOT in the default tab
// order: a site re-adds a STATUS page by subclassing it and overriding
// register_pages(). See core/firmware.h.
#include "libbotanik/pages/status.h"

#include "libbotanik/console/terminal.h"
#include "libbotanik/console/console.h"
#include "libbotanik/console/parser.h"
#include "libbotanik/console/editor.h"

#include "libbotanik/logs/entry.h"
#include "libbotanik/plcio/vars.h"
#include "libbotanik/plcio/sync.h"
#include "libbotanik/plcio/logs.h"

#include "libbotanik/utils/serial.h"
#include "libbotanik/utils/time.h"

#include "libbotanik_sizes.h"   // static_asserts pinning the "big" object sizes

#endif // LIBBOTANIK_H
