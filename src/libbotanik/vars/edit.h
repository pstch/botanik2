#ifndef VARS_EDIT_H
#define VARS_EDIT_H

/**
 * @file edit.h
 * @brief The two helpers the three editing paths share.
 *
 * INTERNAL to vars/: declared here only because writing a parameter, clearing
 * one and forcing an override live in different translation units and parse the
 * same way. Defined in model.cpp.
 */

#include <cstdint>

#include "libbotanik/vars/model.h"

namespace Botanik { namespace Vars {

    /** @brief Copy @p src into @p dst, always NUL-terminating. */
    void copy_bounded(char* dst, uint16_t cap, const char* src);

    /** @brief Parse @p text as @p d's type into @p out, holding it to @p band.
     *
     *  Shared by param_set() and override_set(), which differ only in whether a
     *  band applies: null is unbounded. The band is the CALLER's because it is
     *  RESOLVED - an end that names a row is only a limit once the bank has
     *  read it.
     *  @param band the band to hold to, or null for none */
    SetResult parse_for(const Descriptor& d, const char* text,
                        const Band* band, Scalar* out);

}}  // namespace Botanik::Vars

#endif // VARS_EDIT_H
