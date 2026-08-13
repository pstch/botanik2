#include "libbotanik/utils/assert.h"    // assert_fail / set_assert_logger

#include <cstdint>                      // uint8_t/uint16_t
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
        /** @brief Nesting depth of live ExpectedAsserts, and the running count
         *  of failures seen. Function-local statics for the same reason as the
         *  sink: assert_fail() is reached from anywhere, with no object to ask. */
        uint8_t&  _expected() { static uint8_t  depth = 0; return depth; }
        uint16_t& _failures() { static uint16_t seen  = 0; return seen; }
    }

    ExpectedAsserts::ExpectedAsserts() : _at_entry(_failures()) { _expected()++; }
    ExpectedAsserts::~ExpectedAsserts() { if (_expected() > 0) _expected()--; }
    uint16_t ExpectedAsserts::count() const {
        return (uint16_t)(_failures() - _at_entry);
    }

    void set_assert_logger(Logs::Logger* logger) { _sink() = logger; }

    Logs::Logger* get_assert_logger() { return _sink(); }

    void assert_fail(const char* expr, const char* file, int line) {
        // Expected: count it and return. Silent, because a test driving a
        // refusal on purpose does not want the CRITICAL either.
        if (_expected() > 0) { _failures()++; return; }
        Logs::Logger* logger = _sink();
        if (logger != nullptr)
            logger->critical(Logs::ASSERT, "assertion failed:\n%s (%s:%d)",
                             expr, file, line);

        // Break for the debugger.
        if (HAL::Debug::under_debugger()) {
            HAL::Debug::breakpoint();
            return;
        }

#ifdef TARGET_SIMULATOR
        abort();
#endif
        // On hardware, keep running.
        return;
    }

}
