#ifndef LIBBOTANIK_ALARMS_SPEC_H
#define LIBBOTANIK_ALARMS_SPEC_H

/**
 * @file alarms/spec.h
 * @brief AlarmSpec - an alarm's announce policy, and nothing else.
 *
 * Its own header, and not part of source.h, because `vars/descriptor.h` needs
 * it: a K_ALARM row carries one of these in its payload union, so the widest
 * header in the tree would otherwise acquire the whole alarm-source interface to
 * name a POD. Depends on nothing at all.
 */

#include <cstdint>

namespace Botanik { namespace Alarms {

    /** @brief An alarm's announce policy: display code plus the thresholds.
     *  @note Also the payload a K_ALARM row carries, so it must stay a POD a
     *  constexpr union can hold. */
    struct AlarmSpec {
        const char* code;         ///< short display code (max 9: AlarmsPage::CODE_W)
        int16_t     on_s;         ///< time ON before announcing (-1: off)
        int16_t     off_s;        ///< time OFF before announcing (-1: off)
        int32_t     recent_s;     ///< RECENT window (0: default, <0: forever)
        int16_t     hold_s;       ///< keep active this long past the last raw true
    };

}}  // namespace Botanik::Alarms

#endif // LIBBOTANIK_ALARMS_SPEC_H
