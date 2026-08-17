#ifndef LIBBOTANIK_ALARMS_SOURCE_H
#define LIBBOTANIK_ALARMS_SOURCE_H

/**
 * @file alarms/source.h
 * @brief AlarmSpec and AlarmSourceTable - where an alarm comes from.
 *
 * AlarmController owns the dwell, the latch and the announce; it does not own
 * where the boolean lives. With a variable table that is a SCOPE_A row; without
 * one it is a bool the owner publishes into. Both answer this interface, so the
 * controller and everything drawing it are the same code either way.
 *
 * Index-based rather than a struct per alarm: a condition then costs a table
 * pointer and an index, which is what it cost when it held a Descriptor and a
 * VarRef. @note An alarm's boolean lives inside a Scalar union when the model is
 * on, so there is no stable `bool*` to hand out - hence raw(i) rather than a
 * pointer in a POD.
 */

#include <cstdint>

namespace Botanik { namespace Alarms {

    /** @brief An alarm's announce policy: display code plus the thresholds.
     *  @note Also the payload a SCOPE_A row carries, so it must stay a POD that
     *  a constexpr union can hold. */
    struct AlarmSpec {
        const char* code;         ///< short display code (max 9: AlarmsPage::CODE_W)
        int16_t     on_s;         ///< time ON before announcing (-1: off)
        int16_t     off_s;        ///< time OFF before announcing (-1: off)
        int32_t     recent_s;     ///< RECENT window (0: default, <0: forever)
        int16_t     hold_s;       ///< keep active this long past the last raw true
    };

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
    };

    /** @brief One alarm declared without a variable table: its metadata in
     *  flash, its boolean somewhere the owner writes. */
    struct StaticAlarm {
        const char* name;
        const char* desc;
        const char* notes;
        AlarmSpec   spec;
        /** @brief Borrowed, and must outlive the table. Null reads as false,
         *  which is what an alarm nobody publishes is. */
        const bool* raw;
    };

    /** @brief A source over a flash array of StaticAlarm. What a build with no
     *  variable model uses. */
    class StaticAlarmTable : public AlarmSourceTable {
        const StaticAlarm* _rows;
        uint16_t           _count;

    public:
        /** @brief Borrowed; @p rows must outlive this table. */
        StaticAlarmTable(const StaticAlarm* rows = nullptr, uint16_t count = 0)
            : _rows(rows), _count(rows != nullptr ? count : 0) {}

        uint16_t    count() const override { return _count; }
        const char* name(uint16_t i) const override  { return _rows[i].name; }
        const char* desc(uint16_t i) const override  { return _rows[i].desc; }
        const char* notes(uint16_t i) const override { return _rows[i].notes; }
        AlarmSpec   spec(uint16_t i) const override  { return _rows[i].spec; }
        bool        raw(uint16_t i) const override {
            const bool* p = _rows[i].raw;
            return p != nullptr && *p;
        }
        int32_t find(const char* n, uint16_t len) const override;
    };

}}  // namespace Botanik::Alarms

#endif // LIBBOTANIK_ALARMS_SOURCE_H
