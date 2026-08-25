#ifndef LIBBOTANIK_VARS_ALARM_ROWS_H
#define LIBBOTANIK_VARS_ALARM_ROWS_H

/**
 * @file vars/alarm_rows.h
 * @brief BankAlarms - the site's SCOPE_A rows, as an alarm source.
 *
 * There is no site alarm table: an alarm IS a row carrying both its boolean and
 * its dwell policy. This projects those rows onto Alarms::AlarmSourceTable, so
 * AlarmController never learns that a variable table exists.
 */

#include <cstdint>

#include "libbotanik/alarms/source.h"
#include "libbotanik/config/alarms.h"
#include "libbotanik/vars/model.h"

namespace Botanik { namespace Vars {

    /** @brief The bank's SCOPE_A rows, in table order. */
    class BankAlarms : public Alarms::AlarmSourceTable {
        /** @brief Row index per alarm, built by bind(). Sorted, since the scan
         *  runs the table in order. */
        VarRef      _row[Config::ALARM_SLOTS];
        uint16_t    _count = 0;
        /** @brief SCOPE_A rows the table DECLARES, which is @c _count unless it
         *  overran the slots. The pair is what makes a lost tail visible. */
        uint16_t    _declared = 0;
        Bank*       _io = nullptr;

    public:
        /** @brief Scan @p io for SCOPE_A rows. Call once the table is frozen:
         *  before that there is nothing to scan. Non-const: an alarm row is
         *  forceable, and the force lands on the bank. */
        void bind(Bank* io);

        /** @brief SCOPE_A rows the table declares, slots or not. Past
         *  count() the tail was lost. */
        uint16_t declared() const { return _declared; }

        /** @brief The row alarm @p i reads, for anything that needs the variable
         *  rather than the alarm. */
        VarRef row(uint16_t i) const { return _row[i]; }

        uint16_t    count() const override { return _count; }
        const char* name(uint16_t i) const override;
        const char* desc(uint16_t i) const override;
        const char* notes(uint16_t i) const override;
        Alarms::AlarmSpec spec(uint16_t i) const override;
        bool        raw(uint16_t i) const override;
        bool        forced(uint16_t i) const override;
        int32_t     find(const char* name, uint16_t len) const override;

        bool        force(uint16_t i, bool on) override;
        bool        force_clear(uint16_t i) override;
        bool        force_save(uint16_t i) override;
    };

}}  // namespace Botanik::Vars

#endif // LIBBOTANIK_VARS_ALARM_ROWS_H
