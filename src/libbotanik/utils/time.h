// REVIEW_35: DONE
#ifndef UTILS_TIME_H
#define UTILS_TIME_H

/**
 * @file time.h
 * @brief Time utilities - uptime <-> UNIX/calendar conversion and timestamp
 *        formatting.
 */

#include <cstddef>  // size_t
#include <cstdint>  // uint8_t/uint16_t/uint64_t

namespace Botanik { namespace Utils {

    // Two representations, both uint64: UNIX timestamps, and ms/us uptime.

    /** @name Types
     *  @{ */


    /** @brief Calendar time, without the tm struct's idiosyncrasies and with
     *  extra total-days / -seconds fields. */
    struct CalendarTime {
        uint16_t    year;               ///< Year of epoch, e.g. 2026

        uint8_t     month;              ///< Month of year,     1-indexed
        uint8_t     day;                ///< Day of month,      1-indexed
        uint8_t     hour;               ///< Hour of day,       0-indexed
        uint8_t     minute;             ///< Minute of hour,    0-indexed
        uint8_t     second;             ///< Second of minute,  0-indexed
        uint16_t    millisecond;        ///< Millis of second,  0-indexed

        uint32_t    unixDays;           ///< Days since the UNIX epoch
        uint64_t    unixSeconds;        ///< Seconds since the UNIX epoch
    };

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