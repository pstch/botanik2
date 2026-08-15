#ifndef SHELL_ARGS_H
#define SHELL_ARGS_H

/**
 * @file args.h
 * @brief Word-splitting for command arguments, shared by the shell_*.cpp files.
 *
 * Internal to shell/: a command line is a byte range, never a copy, so parsing
 * one costs no buffer.
 */

#include <cstdint>

#include "libbotanik/utils/text.h"

namespace Botanik { namespace Shell {

    /** @brief Next whitespace-delimited word from [@p args, @p args+alen),
     *  leading spaces skipped. Returns its start (nullptr if none), sets
     *  @p wlen to its length, and advances @p args / @p alen past it. */
    inline const char* arg_next(const char*& args, uint16_t& alen, uint16_t& wlen) {
        while (alen > 0 && *args == ' ') { args++; alen--; }
        if (alen == 0) { wlen = 0; return nullptr; }
        const char* w = args;
        wlen = 0;
        while (alen > 0 && *args != ' ') { args++; alen--; wlen++; }
        return w;
    }

    /** @brief True if word [@p w, @p w+wlen) equals the NUL-terminated literal
     *  @p lit. The terminator check that makes a prefix not a match is
     *  Utils::equals_bounded's, shared with the bank and the task registry.
     *
     *  @p w null answers false for any real literal: arg_next() yields either a
     *  word of non-zero length or nothing at all. */
    inline bool arg_eq(const char* w, uint16_t wlen, const char* lit) {
        return w != nullptr && Utils::equals_bounded(lit, w, wlen);
    }

    /** @brief True for the word naming the whole set: the literal `all`, and
     *  only that. A bare verb does NOT mean "all" - `params save` with nothing
     *  after it is too easy to type when one row was meant. */
    inline bool arg_is_all(const char* w, uint16_t wlen) {
        return w != nullptr && arg_eq(w, wlen, "all");
    }

    /** @brief If word [@p w, @p w+wlen) is `valid` or `invalid`, set @p valid
     *  from it and return true. The quality word is accepted either side of a
     *  value, so this is called twice. */
    inline bool arg_validity(const char* w, uint16_t wlen, bool& valid) {
        if (w == nullptr) return false;
        if (!arg_eq(w, wlen, "valid") && !arg_eq(w, wlen, "invalid")) return false;
        valid = arg_eq(w, wlen, "valid");
        return true;
    }

}}  // namespace Botanik::Shell

#endif // SHELL_ARGS_H
