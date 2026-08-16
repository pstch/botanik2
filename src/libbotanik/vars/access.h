#ifndef VARS_ACCESS_H
#define VARS_ACCESS_H

/**
 * @file access.h
 * @brief Grouping variables for display, and rendering one a consumer only
 *        observes (monitor page, shell, telemetry, home screen).
 *
 * Deliberately thin: grouping and publishing are RULES over the one variable
 * table (a group is a name prefix, what is published is a scope), plus the
 * home-screen row, which is the one thing no rule derives.
 */

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "libbotanik/config/vars.h"

#include "libbotanik/vars/declare.h"
#include "libbotanik/vars/model.h"

namespace Botanik { namespace Vars {

    /** @brief A home-screen reading: a display @p label paired with the name of
     *  the variable whose live value it shows. The label is declared because no
     *  rule derives "Line V" from "grid_LN_V". */
    struct HomeVar {
        const char* label;
        const char* name;
    };

    /** @name Grouping
     *
     *  A variable's group is the prefix of its name, up to the first
     *  underscore. Naming a variable IS filing it: there is no group table.
     *  @{ */

    /** @brief Distinct group names a site may have. Both tables' view modes are
     *  built from this, so it bounds the mode line as well. */
    static const uint8_t VAR_GROUPS_MAX = Config::VAR_GROUPS_MAX;

    /** @brief Intern @p g so the pointer outlives the call.
     *
     *  var_group() must return storage of its own: a shared scratch buffer
     *  cannot survive a caller that holds one group while asking for others.
     *  Returns "?" once VAR_GROUPS_MAX distinct groups are interned. */
    inline const char* group_intern(const char* g) {
        static char pool[VAR_GROUPS_MAX][Config::VAR_GROUP_NAME_LEN];
        static uint8_t used = 0;
        for (uint8_t i = 0; i < used; i++)
            if (strcmp(pool[i], g) == 0) return pool[i];
        if (used >= VAR_GROUPS_MAX) return "?";
        char* dst = pool[used++];
        size_t n = strlen(g);
        if (n >= sizeof(pool[0])) n = sizeof(pool[0]) - 1;
        memcpy(dst, g, n); dst[n] = 0;
        return dst;
    }

    /** @brief @p d's group: its name up to the first underscore. A name with no
     *  underscore is its own group, which is what a config group's commit serial
     *  is (`net`, `ntp`). */
    inline const char* var_group(const Descriptor& d) {
        if (d.name == nullptr) return "?";
        const char* u = strchr(d.name, '_');
        if (u == nullptr) return group_intern(d.name);
        char g[Config::VAR_GROUP_NAME_LEN];
        size_t n = (size_t)(u - d.name);
        if (n >= sizeof(g)) n = sizeof(g) - 1;
        memcpy(g, d.name, n); g[n] = 0;
        return group_intern(g);
    }

    /** @brief Whether @p d belongs on the MONITOR page and in MQTT telemetry.
     *
     *  Everything except the two kinds of row that have a page of their own: a
     *  parameter is the PARAMS page's, an alarm is the ALARMS page's. Tested by
     *  scope, so a new row is published by default. */
    inline bool var_published(const Descriptor& d) {
        return d.scope != SCOPE_P && d.scope != SCOPE_A;
    }

    /** @brief Whether this row carries a Modbus TCP *slave* register: a
     *  parameter, a status value or an alarm.
     *
     *  Deliberately not the same set as var_published(), which excludes exactly
     *  the two kinds this one includes. The register is the site's to assign; a
     *  row of one of these scopes with reg 0 is a table defect the site test
     *  fails on, not a row that quietly does not publish. */
    inline bool var_mapped(const Descriptor& d) {
        return d.scope == SCOPE_P || d.scope == SCOPE_SV || d.scope == SCOPE_A;
    }

    /** @} */

    /** @brief What kind of row this is, as a short column label: the declared
     *  scope, never reconstructed from (transport, role, type). */
    inline const char* var_scope(const Descriptor& d) { return scope_label(d.scope); }

    /** @brief Render @p h's current value into @p buf. Returns the text - an
     *  enum's static string, a T_STR's pool text, or @p buf. */
    inline const char* var_format(const Bank& io, Var h, char* buf, size_t cap) {
        const Descriptor* d = h.meta();
        if (d->type == T_STR) {
            const char* t = io.text_at(h);
            return (t != nullptr) ? t : "";
        }
        return format_scalar(d->type, h.raw(), d->digits, d->str, buf, cap);
    }
    inline const char* var_format(const Bank& io, VarRef r, char* buf, size_t cap) {
        return var_format(io, io.var(r), buf, cap);
    }

}}  // namespace Botanik::Vars

#endif // VARS_ACCESS_H
