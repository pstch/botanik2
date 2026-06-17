#ifndef LIBBOTANIK_H
#define LIBBOTANIK_H

/**
 * @file libbotanik.h
 * @brief Umbrella header - pulls in the full libbotanik public API in one include.
 */

#include "libbotanik/hal/platform.h"

#include "libbotanik/context.h"

#include "libbotanik/scheduler/scheduler.h"

#include "libbotanik/logs/logger.h"
#include "libbotanik/logs/writer.h"

#include "libbotanik/pages/home.h"
#include "libbotanik/pages/shell.h"
#include "libbotanik/pages/logs.h"
#include "libbotanik/pages/status.h"
#include "libbotanik/pages/alerts.h"

#include "libbotanik/terminal/terminal.h"
#include "libbotanik/terminal/parser.h"
#include "libbotanik/terminal/editor.h"

#include "libbotanik/plcio/vars.h"
#include "libbotanik/plcio/sync.h"
#include "libbotanik/plcio/emulator.h"
#include "libbotanik/plcio/logs.h"

#include "libbotanik/utils/serial.h"
#include "libbotanik/utils/time.h"

// Memory-footprint contract (static_asserts pinning the "big" object sizes).
#include "libbotanik_sizes.h"

#endif // LIBBOTANIK_H
