#ifndef PLC_IO_H
#define PLC_IO_H

/**
 * @file io.h
 * @brief Io - everything a control program may do to the variable table, and
 *        nothing else.
 *
 * @section plc_io_why Why not just hand it the Bank
 *
 * Because the Bank is the whole model: a program handed one can edit a
 * PARAMETER, force or release an OVERRIDE, look a row up by name mid-scan, or
 * withdraw the control authority that gates its own phase. None of those are
 * things a control law does, and all of them are things a typo can do on a
 * machine that moves actuators.
 *
 * So the argument is narrowed to the verbs: four override-aware reads, and
 * `set()` by reference as the entire write surface.
 *
 * | Wanted | Where it lives |
 * |---|---|
 * | read a value / its quality | here |
 * | drive an output, publish a computed row | here |
 * | resolve a name | `bind()`, once, at bring-up |
 * | edit a parameter | the operator, through `Bank::set()` |
 * | force a row | the operator, through `override_set()` |
 * | publish a reading, invalidate on a bad read | the field-bus driver |
 * | withhold control | `HealthMonitor`, or the console | */

#include <cstdint>                  // int64_t

#include "libbotanik/vars/model.h"  // Vars::Bank / Var / VarRef / Scalar

namespace Botanik { namespace Plc {

    /**
     * @brief The field, as a control program is allowed to see it.
     *
     * Addressed by `VarRef` only, the reference a program resolved in `bind()`.
     * There is deliberately no lookup by name: a scan must not search the table.
     *
     * A bad reference resolves to the bank's inert sink, so a program driving a
     * row that does not exist moves nothing and says so through
     * `unresolved_count()`.
     */
    class Io {

        /** @name State
         *  @{ */

        Vars::Bank* _bank;
        /** @brief Which program is holding this view, as its index in the
         *  site's program table. Every write is stamped with it. */
        uint8_t     _who;

        /** @} */

    public:
        explicit Io(Vars::Bank& bank, uint8_t who = Vars::WRITER_NONE)
            : _bank(&bank), _who(who) {}

        /** @name Read
         *
         *  Override-aware, like every other reader. An invalid row reads as its
         *  declared default; logic that cares about quality asks valid().
         *  @{ */

        /** @brief As a float. A digital reads 0 or 1, so logic that only
         *  compares never has to branch on the type. */
        float   value(Vars::VarRef r) const { return _bank->var(r).value(); }
        /** @brief As a boolean. */
        bool    on(Vars::VarRef r)    const { return _bank->var(r).b(); }
        /** @brief As an integer. */
        int64_t whole(Vars::VarRef r) const { return _bank->var(r).i(); }
        /** @brief Whether the reading can be believed. */
        bool    valid(Vars::VarRef r) const { return _bank->var(r).valid(); }

        /** @}
         *  @name Write
         *
         *  The whole write surface. An output's demand or a computed row's
         *  value: the row's role decides which, and whether the field-bus
         *  driver is asked to carry it.
         *  @{ */

        void set(Vars::VarRef r, bool v)  { _claim(r); _bank->var(r).set(v); }
        void set(Vars::VarRef r, float v) { _claim(r); _bank->var(r).set(v); }
        void set_i(Vars::VarRef r, int64_t v) { _claim(r); _bank->var(r).set_i(v); }

        /** @} */

    private:
        /** @brief Stamp this row with its author. One byte compared, and on the
         *  usual path left alone: the row already belongs to us. */
        void _claim(Vars::VarRef r) { (void)_bank->claim_writer(r, _who); }
    };

}}  // namespace Botanik::Plc

#endif // PLC_IO_H
