#ifndef LIBBOTANIK_H
#define LIBBOTANIK_H

#include "libbotanik/scheduler/scheduler.h"

#include "libbotanik/logs/logger.h"
#include "libbotanik/logs/writer.h"

#include "libbotanik/terminal/terminal.h"
#include "libbotanik/terminal/parser.h"
#include "libbotanik/terminal/editor.h"

#include "libbotanik/plcio/sync.h"
#include "libbotanik/plcio/logs.h"
#include "libbotanik/plcio/emulator.h"

#include "libbotanik/utils/serial.h"
#include "libbotanik/utils/time.h"

namespace Botanik {

    class BotanikLoop {
    public:
        void setup() {

        }

        void step() {

        }

    };

}

#endif // LIBBOTANIK_H