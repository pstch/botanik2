#ifndef LIBBOTANIK_ALARMS_SOURCE_H
#define LIBBOTANIK_ALARMS_SOURCE_H

/**
 * @file alarms/source.h
 * @brief AlarmSourceTable - where an alarm comes from.
 *
 * AlarmController owns the dwell, the latch and the announce; it does not own
 * where the boolean lives: that is a K_ALARM row, reached through this
 * interface, so `alarms/` compiles without `vars/` and a test can stand its own
 * source in front of the controller.
 *
 * Index-based rather than a struct per alarm: a condition then costs a table
 * pointer and an index, which is what it cost when it held a Descriptor and a
 * uint16_t. @note An alarm's boolean lives inside a Scalar union when the model is
 * on, so there is no stable `bool*` to hand out - hence raw(i) rather than a
 * pointer in a POD.
 */

#include <cstdint>

#include "libbotanik/alarms/spec.h"

namespace Botanik { namespace Alarms {

    /** @brief The alarms a controller scans, and their raw inputs. */
    class AlarmSourceTable {
    public:
        virtual ~AlarmSourceTable() {}

        /** @name The alarms
         *  @{ */

        /** @brief How many alarms this source declares. May exceed the
         *  controller's slots; the controller is what reports the loss. */
        virtual uint16_t    count() const = 0;
        virtual const char* name(uint16_t i) const = 0;
        virtual const char* desc(uint16_t i) const = 0;
        virtual const char* notes(uint16_t i) const = 0;
        virtual AlarmSpec   spec(uint16_t i) const = 0;

        /** @} */
        /** @name The inputs
         *  @{ */

        /** @brief Alarm @p i's raw condition, this instant. */
        virtual bool raw(uint16_t i) const = 0;

        /** @brief Whether @p i is being FORCED rather than read. A forced
         *  condition behaves exactly like a real one, which is why a display has
         *  to be able to say so. Default: nothing forces anything. */
        virtual bool forced(uint16_t i) const { (void)i; return false; }

        /** @brief Index of the alarm called @p name, or -1. @p len bounds the
         *  compare: this is operator input, so a miss is a typo. */
        virtual int32_t find(const char* name, uint16_t len) const = 0;

        /** @} */
        /** @name Forcing
         *  Where the boolean lives is the source's business, so holding one by
         *  hand is too. The default is a source nothing can force; false means
         *  the gesture did not happen.
         *  @{ */

        /** @brief Hold alarm @p i at @p on until force_clear(). */
        virtual bool force(uint16_t i, bool on) { (void)i; (void)on; return false; }
        /** @brief Release @p i's force; the live condition takes over again. */
        virtual bool force_clear(uint16_t i) { (void)i; return false; }
        /** @brief Persist @p i's force, so it survives a reboot. */
        virtual bool force_save(uint16_t i) { (void)i; return false; }

        /** @} */
    };

}}  // namespace Botanik::Alarms

#endif // LIBBOTANIK_ALARMS_SOURCE_H
