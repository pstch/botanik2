#ifndef LIBBOTANIK_VARS_ALARM_ROWS_H
#define LIBBOTANIK_VARS_ALARM_ROWS_H

/**
 * @file vars/alarm_rows.h
 * @brief BankAlarms - the site's K_ALARM rows, as an alarm source.
 *
 * There is no site alarm table: an alarm IS a row carrying both its boolean and
 * its dwell policy. This projects those rows onto Alarms::AlarmSourceTable, so
 * AlarmController never learns that a variable table exists.
 */

#include <cstdint>

#include "libbotanik/alarms/source.h"
#include "libbotanik/config/alarms.gen.h"
#include "libbotanik/vars/model.h"
#include "libbotanik/vars/persist.h"

namespace Botanik { namespace Vars {

    /** @brief The bank's K_ALARM rows, in table order. */
    class BankAlarms : public Alarms::AlarmSourceTable {
        /** @brief Row index per alarm, built by bind(). Sorted, since the scan
         *  runs the table in order. */
        uint16_t      _row[Config::ALARM_SLOTS];
        uint16_t    _count = 0;
        /** @brief K_ALARM rows the table DECLARES, which is @c _count unless it
         *  overran the slots. The pair is what makes a lost tail visible. */
        uint16_t    _declared = 0;
        Bank*       _io = nullptr;
        /** @brief What makes a force durable; null leaves saving a no-op. */
        Persist*    _persist = nullptr;

    public:
        /** @brief Scan @p io for K_ALARM rows. Non-const: an alarm row is
         *  forceable, and the force lands on the bank. @p persist is what makes
         *  such a force durable. */
        void bind(Bank* io, Persist* persist = nullptr);


        /** @brief The row alarm @p i reads, for anything that needs the variable
         *  rather than the alarm. */
        uint16_t row(uint16_t i) const { return _row[i]; }

        /** @brief Every K_ALARM row the table declares, slots or not - the
         *  contract AlarmSourceTable states, and what makes a lost tail
         *  visible. The controller caps its own loop at ALARM_SLOTS, and the
         *  row accessors below are only reached for what it took. */
        uint16_t    count() const override { return _declared; }
        /** @brief How many the slots actually hold. */
        uint16_t    driven() const { return _count; }
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
