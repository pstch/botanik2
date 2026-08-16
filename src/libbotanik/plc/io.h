#ifndef PLC_IO_H
#define PLC_IO_H

/**
 * @file io.h
 * @brief Io - the narrowed view of the variable table a control program gets.
 * @note Deliberately not the Bank: a control law has no business editing a
 *       parameter, forcing an override or withdrawing control authority.
 */

#include <cstdint>

#include "libbotanik/vars/model.h"

namespace Botanik { namespace Plc {

    /**
     * @brief The field, as a control program is allowed to see it.
     * @note Addressed by VarRef only, resolved in bind() - a scan must not
     *       search the table. A bad ref resolves to the bank's inert sink.
     */
    class Io {

        /** @name State
         *  @{ */

        Vars::Bank* _bank;
        uint8_t     _who;  ///< writer id stamped on every write; WRITER_NONE if unowned

        /** @} */

    public:
        explicit Io(Vars::Bank& bank, uint8_t who = Vars::WRITER_NONE)
            : _bank(&bank), _who(who) {}

        /** @name Read
         *
         *  Override-aware. An invalid row reads as its declared default.
         *  @{ */

        /** @brief As a float; a digital reads 0 or 1. */
        float   value(Vars::VarRef r) const { return _bank->var(r).as_f32(); }
        bool    on(Vars::VarRef r)    const { return _bank->var(r).b(); }
        int64_t whole(Vars::VarRef r) const { return _bank->var(r).i(); }
        /** @brief Whether the reading can be believed. */
        bool    valid(Vars::VarRef r) const { return _bank->var(r).valid(); }

        /** @}
         *  @name Write
         *
         *  The whole write surface. The row's role decides whether this is an
         *  output demand or a computed value.
         *  @{ */

        void set(Vars::VarRef r, bool v)  { _claim(r); _bank->var(r).set(v); }
        void set(Vars::VarRef r, float v) { _claim(r); _bank->var(r).set(v); }
        void set_i(Vars::VarRef r, int64_t v) { _claim(r); _bank->var(r).set_i(v); }

        /** @} */

    private:
        void _claim(Vars::VarRef r) { (void)_bank->claim_writer(r, _who); }
    };

}}  // namespace Botanik::Plc

#endif // PLC_IO_H
