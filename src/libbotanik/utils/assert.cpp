#include "libbotanik/utils/assert.h"    // assert_fail / set_assert_logger

#include <cstdlib>                      // abort

#include "libbotanik/hal/platform.h"    // HAL::Debug::breakpoint / under_debugger
#include "libbotanik/logs/logger.h"     // Logs::Logger

namespace Botanik {

    namespace {
        /** @brief The one sink, set once at setup. A function-local static so
         *  there is no init-order dependency on a translation unit. */
        Logs::Logger*& _sink() {
            static Logs::Logger* logger = nullptr;
            return logger;
        }
    }

    void set_assert_logger(Logs::Logger* logger) { _sink() = logger; }

    Logs::Logger* get_assert_logger() { return _sink(); }

    void assert_fail(const char* expr, const char* file, int line) {
        // Log a message if we have a logger
        Logs::Logger* logger = _sink();
        if (logger != nullptr)
            logger->critical(Logs::ASSERT, "assertion failed:\n%s (%s:%d)",
                             expr, file, line);

        // Break for the debugger.
        if (HAL::Debug::under_debugger()) {
            HAL::Debug::breakpoint();
            return;
        }

        // If on the simulator, abort the program.
#ifdef TARGET_SIMULATOR
        abort();
#endif

        // If on hardware, keep running.
        return;
    }

}
