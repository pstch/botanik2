#ifndef VARS_ACCESS_H
#define VARS_ACCESS_H

/**
 * @file access.h
 * @brief Grouping variables for display, and resolving a reference for the
 *        consumers that only observe one.
 *
 * Control programs address their signals by index through the handle in
 * model.h - they know exactly which variable they mean. Everything that merely
 * *observes* one does not: the monitor page, the shell, MQTT telemetry, the
 * home screen and the alarm reader all want "whatever this reference points at,
 * read it and render it".
 *
 * There is very little left here. Resolving used to be a seven-case switch over
 * six tables; with one table it is an array index, so what remains is the
 * grouping types and a rendering convenience.
 */

#include <cstddef>
#include <cstdint>

#include "model.h"

namespace Botanik { namespace Vars {

    /** @brief A named set of variables under a shared sub-topic. A site
     *  declares one VarRef[] per domain (power, environment, ...) and lists
     *  them as a VarGroup[]; telemetry publishes each as
     *  "<prefix>/telemetry/<topic>/<name>". */
    struct VarGroup {
        const char*   topic;
        const VarRef* refs;
        uint16_t      count;
    };

    /** @brief A home-screen reading: a display @p label paired with the
     *  variable whose live value it shows. The label is separate from the
     *  variable's own name because the home screen wants "Line V" where the
     *  monitor table wants "grid_LN_V". */
    struct HomeVar {
        const char* label;
        VarRef      ref;
    };

    /** @brief Where a variable physically lives, as a short column label.
     *
     *  Transport and role together, because neither alone answers the question
     *  an operator is asking ("is this a reading, a thing I drive, or a number
     *  the firmware made up?"): XP_MODBUS says how it travels but not which way,
     *  and ROLE_INPUT says which way but not over what.
     *
     *  Kept to 8 columns so it fits the monitor table beside the name. */
    inline const char* var_scope(const Variable& d) {
        const bool in = (d.role == ROLE_INPUT);
        switch (d.bind.transport) {
            case XP_MODBUS:        return in ? "mb in"  : "mb out";
            case XP_LOCAL_ANALOG:  return in ? "ana in" : "ana out";
            case XP_LOCAL_DIGITAL: return in ? "dig in" : "relay";
            default: break;
        }
        // Nothing drives it from the field: it is either configuration or a
        // number the control programs computed.
        switch (d.role) {
            case ROLE_PARAM:    return "param";
            case ROLE_OUTPUT:   return "demand";
            case ROLE_INPUT:    return "unbound";
            default:            return "computed";
        }
    }

    /** @brief Render @p h's current value into @p buf. Returns the text - an
     *  enum's static string, a T_STR's pool text, or @p buf. */
    inline const char* var_format(const Bank& io, Var h, char* buf, size_t cap) {
        const Variable* d = h.meta();
        if (d->type == T_STR)
            return (io.pool() != nullptr) ? io.pool() + d->str_off : "";
        return format_scalar(d->type, h.raw(), d->digits, d->str, buf, cap);
    }
    inline const char* var_format(const Bank& io, VarRef r, char* buf, size_t cap) {
        return var_format(io, io.var(r), buf, cap);
    }

}}  // namespace Botanik::Vars

#endif // VARS_ACCESS_H
