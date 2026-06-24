#ifndef UTILS_TIME_H
#define UTILS_TIME_H

/**
 * @file time.h
 * @brief Time utilities - project rolling 32-bit uptime counters into absolute
 *        64-bit time, convert to calendar/UNIX time, and format timestamps.
 */

#include <cstddef>
#include <cstdint>

namespace Botanik { namespace Utils {

    // Three time representations: UNIX timestamps (uint64), absolute ms/us
    // uptime (uint64), and rolling-over 32-bit ms/us uptime. Some components
    // record bare 32-bit counters; absolute_ms()/absolute_us() project them
    // into the rollover window centered on now. Plus the usual
    // conversion/formatting helpers.

    /** @name Types
     *  @{ */

    /** @brief A 32-bit ms timestamp that wraps every ~49 days (e.g. the PLC
     *  loop clock). A distinct type offering only wrap-safe elapsed
     *  subtraction: no bare `>`, no mixing with the monotonic 64-bit
     *  HAL::Clock::uptime_ms(). Construct from the raw counter; read `.ms` for the raw
     *  value. */
    struct Millis32 {
        uint32_t ms = 0;
        Millis32() = default;
        explicit Millis32(uint32_t v) : ms(v) {}
        /** @brief Wrap-safe elapsed time since the @p earlier stamp (this -
         *  earlier). */
        uint32_t operator-(Millis32 earlier) const { return ms - earlier.ms; }
        bool operator==(Millis32 o) const { return ms == o.ms; }
        bool operator!=(Millis32 o) const { return ms != o.ms; }
    };

    /** @brief Calendar time, without the tm struct's idiosyncrasies and with
     *  extra total-days / -seconds fields. */
    struct CalendarTime {
        uint16_t    year;               // full year, e.g. 2026
        uint8_t     month;              // 1-indexed
        uint8_t     day;                // 1-indexed
        uint8_t     hour;               // 0-indexed
        uint8_t     minute;             // 0-indexed
        uint8_t     second;             // 0-indexed
        uint16_t    millisecond;        // 0-indexed
        uint32_t    unixDays;           // days since the UNIX epoch
        uint64_t    unixSeconds;        // seconds since the UNIX epoch
    };

    /** @} */

    /** @name Rollover projection
     *  @{ */

    /** @brief Project a 32-bit @p timestamp into the rollover window centered
     *  on @p uptime. @note Only correct within +/-half a window (+/-~24.8 days
     *  for ms, +/-~35.7 min for µs). */
    uint64_t _project_rollover(uint64_t uptime, uint32_t timestamp);

    /** @brief Project a rolling-over ms uptime counter into the current window
     *  (see _project_rollover). */
    uint64_t absolute_ms(uint32_t timestamp_ms);

    /** @brief Project a rolling-over µs uptime counter into the current window.
     *  @warning see uptime_ms() about the µs precision of the low-power clock.
     */
    uint64_t absolute_us(uint32_t timestamp_us);

    /** @} */

    /** @name Calendar conversion & formatting
     *  @{ */

    /** @brief Map an uptime-domain ms instant onto wall-clock UNIX ms, and back
     *  (the boot-epoch projection cal_time_ms() uses, for callers crossing the
     *  two domains - e.g. a sun-event UNIX instant back into an uptime
     *  timestamp). */
    uint64_t unix_ms_from_uptime_ms(uint64_t uptime_ms);
    uint64_t uptime_ms_from_unix_ms(uint64_t unix_ms);

    /** @brief Convert an absolute ms uptime counter into a calendar datetime.
     */
    CalendarTime cal_time_ms(uint64_t timestamp_ms);

    /** @brief Buffer length required by format_ms_timestamp, including the
     *  terminator. @note Format is "YY/MM/DD HH:MM:SS.mmm" (21 chars) + '\0'.
     */
    static constexpr size_t TIMESTAMP_LEN = 22;

    /** @brief Format an absolute millisecond uptime counter into an UTC
     *  timestamp. */
    void format_ms_timestamp(uint64_t timestamp_ms, char* buf, size_t len);

    /** @} */

}}

#endif // UTILS_TIME_H