#ifndef VARS_EDIT_H
#define VARS_EDIT_H

/**
 * @file edit.h
 * @brief The two helpers the three editing paths share.
 *
 * INTERNAL to vars/: declared here only because writing a parameter, clearing
 * one and forcing an override live in different translation units and parse the
 * same way. Defined in params.cpp.
 */

#include <cstdint>

#include "libbotanik/vars/model.h"

namespace Botanik { namespace Vars {

    /** @brief Copy @p src into @p dst, always NUL-terminating. */
    void copy_bounded(char* dst, uint16_t cap, const char* src);

    /** @brief Parse @p text as @p d's type into @p out, optionally holding it
     *  to the declared band. Shared by param_set() and override_set(), which
     *  differ only in whether the band applies. */
    SetResult parse_for(const Descriptor& d, const char* text, bool bounded,
                        Scalar* out);

}}  // namespace Botanik::Vars

#endif // VARS_EDIT_H
