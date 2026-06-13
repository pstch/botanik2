// REVIEW_34: OK
#ifndef UTILS_TIME_H
#define UTILS_TIME_H

/**
 * @file time.h
 * @brief Time utilities — project rolling 32-bit uptime counters into absolute
 *        64-bit time, convert to calendar/UNIX time, and format timestamps.
 */

#include <cstddef>
#include <cstdint>

namespace Botanik { namespace Utils {

    // These utilities provides some standard utility functions to work with the
    // different ways we use to represent time :
    //  - UNIX timestamps, stored as uint64_t on this platform
    //  - absolute millisecond/microsecond uptime counters, stored as uint64_t
    //  - rolling-over millisecond/microsecond uptime counters, stored as uint32_t
    //
    // Some components of our firmware record and transmit 32-bit rolling-over counters
    // without additional context, and we need to project them into the current rollover
    // window, assuming that they refer to events that occurred in a rollover window
    // centered on current time. This namespace provides functions to handle this projection
    // (absolute_ms() and absolute_us()), as well a some standard formatting/conversion
    // functions.

    /** @name Types
     *  @{ */

    /** @brief Calendar time struct, without the idiosyncracies from the standard tm struct
     * and with some additional fields for total days and milliseconds */
    struct CalendarTime {
        // standard calendar fields
        uint16_t    year;               // full calendar year, e.g. 2026
        uint8_t     month;              // month of current year, 1-indexed
        uint8_t     day;                // day of current month, 1-indexed
        uint8_t     hour;               // hour of current day, 0-indexed
        uint8_t     minute;             // minute of current hour, 0-indexed
        uint8_t     second;             // second of current minute, 0-indexed
        uint16_t    millisecond;        // millisecond of current second, 0-indexed
        // monotonic counters
        uint32_t    unixDays;           // days after UNIX epoch, 0-indexed
        uint64_t    unixSeconds;        // seconds after UNIX epoch, 0-indexed
    };

    /** @} */

    /** @name Rollover projection
     *  @{ */

    /** @brief Convert a 32-bit uptime timestamp to an absolute 64-bit timestamp,
     *  assuming the given timestamp occurs in the rollover window centered on the
     *  given absolute timestamp.
     *  @note Only correct for a timestamp within ±half a window of @p uptime
     *  (±~24.8 days for ms, ±~35.7 min for µs). */
    uint64_t _project_rollover(uint64_t uptime, uint32_t timestamp);

    /** @brief Project a rolling-over ms uptime counter into the current rollover window.
     *  @note Only correct for a timestamp within ±half a window of @p uptime
     *  (±~24.8 days for ms, ±~35.7 min for µs). */
    uint64_t absolute_ms(uint32_t timestamp_ms);

    /** @brief Project a rolling-over ms uptime counter into the current rollover window.
     *  @note Only correct for a timestamp within ±half a window of @p uptime
     *  (±~24.8 days for ms, ±~35.7 min for µs). 
     *  @warning see warning in uptime_ms() about the µs precision of the low-power clock. */
    uint64_t absolute_us(uint32_t timestamp_us);

    /** @} */

    /** @name Calendar conversion & formatting
     *  @{ */

    /** @brief Convert an absolute ms uptime counter into a calendar datetime. */
    CalendarTime cal_time_ms(uint64_t timestamp_ms);

    /** @brief Buffer length required by format_ms_timestamp, including the terminator.
     *  @note Format is "YY/MM/DD HH:MM:SS.mmm" (21 chars) + '\0'. */
    static constexpr size_t TIMESTAMP_LEN = 22;

    /** @brief Format an absolute millisecond uptime counter into an UTC timestamp. */
    void format_ms_timestamp(uint64_t timestamp_ms, char* buf, size_t len);

    /** @} */

}}

#endif // UTILS_TIME_H