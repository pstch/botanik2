/** @file  Resolving an ST name against the site's table.
 *
 *  IEC identifiers are case-insensitive and matiec upper-cases every one, so
 *  the row a POU names arrives here as `CLI_RAIN_MMPH`. The table spells it
 *  `cli_rain_mmph`. Folding case is the whole of the difference; all 425 site
 *  rows stay unique under it.
 */
#ifndef BOTANIK_ST_RESOLVE_CI_H
#define BOTANIK_ST_RESOLVE_CI_H

#include <cstring>
#include "libbotanik/vars/model.h"

namespace Botanik { namespace St {

    inline bool same_folded(const char* a, const char* b) {
        for (;; a++, b++) {
            const char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
            const char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
            if (ca != cb) return false;
            if (ca == 0)  return true;
        }
    }

    /** @brief The row @p name, whatever case it is spelled in.
     *
     *  A miss returns the inert sink, exactly as `Bank::resolve_*` does, so a
     *  POU naming a row no site declares fails the same way one written in
     *  C++ does. */
    template <typename T>
    Vars::Ref<T, Vars::Dir::INOUT> resolve_ci(Vars::Bank& bank, const char* name) {
        for (uint16_t i = 0; i < bank.count(); i++)
            if (same_folded(bank.descriptor(i).name, name))
                return bank.resolve_inout<T>(bank.descriptor(i).name);
        return Vars::Ref<T, Vars::Dir::INOUT>();
    }

}}  // namespace Botanik::St

#endif
