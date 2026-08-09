#ifndef PLC_IO_H
#define PLC_IO_H

/**
 * @file io.h
 * @brief Io - everything a control program may do to the variable table, and
 *        nothing else.
 *
 * @section plc_io_why Why not just hand it the Bank
 *
 * Because the Bank is the whole model. A program handed one can edit a
 * PARAMETER, force an OVERRIDE, release someone else's, look a row up by name
 * mid-scan, or withdraw control authority from inside the very phase that
 * authority gates. None of those are things a control law does; all of them are
 * things a typo or a copied line can do, in a class a site author writes, on a
 * machine that moves actuators.
 *
 * So the argument is narrowed to the verbs. Reads are the same four the rest of
 * the firmware uses, override-aware, so a forced reading reaches control logic
 * exactly as it reaches the console. Writes are `set()`, by reference, and that
 * is the entire write surface:
 *
 * | Wanted | Where it lives |
 * |---|---|
 * | read a value / its quality | here |
 * | drive an output, publish a computed row | here |
 * | resolve a name | `bind()`, once, at bring-up |
 * | edit a parameter | the operator, through `Bank::set()` |
 * | force a row | the operator, through `override_set()` |
 * | publish a reading, invalidate on a bad read | the field-bus driver |
 * | withhold control | `HealthMonitor`, or the console |
 *
 * @section plc_io_chokepoint The write chokepoint
 *
 * Every write a program makes goes through one function here. That is worth
 * more than the narrowing on its own: it is the only place that can know WHICH
 * program is writing, which is what attributing a row to its author needs, and
 * what noticing two programs driving one output needs. Neither is possible while
 * the write is a method on a handle the program made itself.
 *
 * @section plc_io_cost What it costs
 *
 * One pointer, passed by value in the Context a program already receives. The
 * calls are inline and forward straight to `Vars::Var`, so the generated code is
 * what the direct call was.
 */

#include <cstdint>                  // int64_t

#include "libbotanik/vars/model.h"  // Vars::Bank / Var / VarRef / Scalar

namespace Botanik { namespace Plc {

    /**
     * @brief The field, as a control program is allowed to see it.
     *
     * Addressed by `VarRef` only - the reference a program resolved in `bind()`.
     * There is deliberately no lookup by name: doing that per activation was
     * sixty table searches a second to learn what the program already knew, and
     * removing the route is what keeps it gone.
     *
     * A bad reference resolves to the bank's inert sink, exactly as it does
     * anywhere else, so a program driving a row that does not exist moves
     * nothing and says so through `unresolved_count()`.
     */
    class Io {

        /** @name State
         *  @{ */

        Vars::Bank* _bank;

        /** @} */

    public:
        explicit Io(Vars::Bank& bank) : _bank(&bank) {}

        /** @name Read
         *
         *  Override-aware, like every other reader: a forced value reaches
         *  control logic the same way it reaches the console. An invalid row
         *  reads as its declared default, so logic that does not care about
         *  quality still gets a sane number and logic that does asks valid().
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
         *  The whole write surface. An output's demand, or a computed row's
         *  value - the row's own role decides which, and decides whether the
         *  field-bus driver is asked to carry it.
         *  @{ */

        void set(Vars::VarRef r, bool v)    { _bank->var(r).set(v); }
        void set(Vars::VarRef r, float v)   { _bank->var(r).set(v); }
        void set_i(Vars::VarRef r, int64_t v) { _bank->var(r).set_i(v); }

        /** @} */
    };

}}  // namespace Botanik::Plc

#endif // PLC_IO_H
