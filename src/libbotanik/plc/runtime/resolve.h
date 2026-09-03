/** @file  Resolving an ST name against the site's table.
 *
 *  IEC identifiers are case-insensitive and matiec upper-cases every one, so
 *  the row a POU names arrives here as `CLI_RAIN_MMPH`. The table spells it
 *  `cli_rain_mmph`. Folding case is the whole of the difference; all 425 site
 *  rows stay unique under it.
 *
 *  A `_qlt` companion is an ordinary row and resolves the same way: the
 *  generator declares one, so nothing here knows about the suffix.
 */
#ifndef BOTANIK_PLC_RUNTIME_RESOLVE_H
#define BOTANIK_PLC_RUNTIME_RESOLVE_H

#include <cstring>
#include "libbotanik/vars/model.h"

namespace Botanik { namespace Plc { namespace Runtime {

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
     *  A miss goes through `Bank::resolve` too, and that is load-bearing: it is
     *  what COUNTS the miss, which is the only thing Plant::wire() can see, and
     *  what leaves the reference holding a bank so the sink is reachable at
     *  all. Returning a default Ref here instead left the program HEALTHY and
     *  its bank pointer null. */
    template <typename T>
    Vars::Ref<T> resolve_ci(Vars::Bank& bank, const char* name) {
        for (uint16_t i = 0; i < bank.count(); i++)
            if (same_folded(bank.descriptor(i).name, name))
                return bank.resolve<T>(bank.descriptor(i).name);
        return bank.resolve<T>(name);
    }

}}}  // namespace Botanik::Plc::Runtime

#endif
