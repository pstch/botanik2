#ifndef VARS_ACCESS_H
#define VARS_ACCESS_H

/**
 * @file access.h
 * @brief Grouping variables for display, and resolving a reference for the
 *        consumers that only observe one.
 *
 * Control programs name the signal they mean and the bank resolves it. What is
 * left over is everything that merely *observes* a variable without knowing
 * which: the monitor page, the shell, MQTT telemetry, the home screen and the
 * alarm reader all want "whatever this reference points at, read it and render
 * it".
 *
 * There is very little left here, and less each time. Resolving used to be a
 * seven-case switch over six tables; grouping used to be a declared table per
 * site. Both are now rules over the one variable table - a group is a name
 * prefix, and what gets published is a scope - so what remains is those rules,
 * the home-screen row (the one thing no rule derives) and a rendering
 * convenience.
 */

#include <cstddef>                      // size_t
#include <cstdint>                      // uint8_t
#include <cstring>                      // strchr / strcmp / strlen / memcpy

#include "libbotanik/config/vars.h"       // Config::VAR_* / STR_*

#include "libbotanik/vars/declare.h"    // Vars::scope_label
#include "libbotanik/vars/model.h"      // Vars::Bank / Var / VarRef

namespace Botanik { namespace Vars {

    /** @brief A home-screen reading: a display @p label paired with the name of
     *  the variable whose live value it shows. The label is separate from the
     *  variable's own name because the home screen wants "Line V" where the
     *  monitor table wants "grid_LN_V", and no rule derives one from the
     *  other - which is why this is the one declarative table left. */
    struct HomeVar {
        const char* label;
        const char* name;
    };

    /** @name Grouping
     *
     *  A variable's group is the prefix of its name, up to the first underscore.
     *  It used to be a declared table per site - one VarRef[] per domain, plus a
     *  VarGroup[] listing them - which was 72 rows of pure restatement: every
     *  grid_* variable in the grid group, every cli_* in climate. Adding a
     *  variable meant remembering to add it to its group, and forgetting meant
     *  it silently never appeared on the monitor page or in telemetry.
     *
     *  Naming a variable now IS filing it. The PARAMS page already worked this
     *  way for the config groups; this is the same rule applied to the rest.
     *  @{ */

    /** @brief Distinct group names a site may have. Both tables' view modes are
     *  built from this, so it bounds the mode line as well. */
    static const uint8_t VAR_GROUPS_MAX = Config::VAR_GROUPS_MAX;

    /** @brief Intern @p g so the pointer outlives the call.
     *
     *  var_group() has to return storage of its own, and a shared scratch buffer
     *  is not enough: the table legend passes the selected row's group to the
     *  mode line, which then walks every row asking for ITS group - so the
     *  buffer was overwritten mid-call and the legend always showed whatever
     *  came last. */
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
    inline const char* var_group(const Variable& d) {
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
     *  parameter is the PARAMS page's, an alarm is the ALARMS page's. Declared
     *  by scope rather than by listing the survivors, so a new row is published
     *  by default - the failure mode is a variable that shows up uninvited,
     *  which someone notices, rather than one that silently never appears. */
    inline bool var_published(const Variable& d) {
        return d.scope != SCOPE_P && d.scope != SCOPE_A;
    }

    /** @brief Whether this row carries a Modbus TCP *slave* register: a
     *  parameter, a status value or an alarm.
     *
     *  Deliberately not the same set as var_published() - that one answers "does
     *  the HMI and MQTT show it", and it excludes exactly the two kinds this one
     *  includes, because a parameter and an alarm each have a page of their own.
     *  Beside it all the same, because both are the same kind of rule: what a row
     *  IS decides where it appears, and the answer lives here rather than being
     *  restated by every consumer.
     *
     *  The register itself is the site's to assign; a row of one of these scopes
     *  with reg 0 is a table defect the site test fails on, not a row that
     *  quietly does not publish. */
    inline bool var_mapped(const Variable& d) {
        return d.scope == SCOPE_P || d.scope == SCOPE_SV || d.scope == SCOPE_A;
    }

    /** @} */

    /** @brief What kind of row this is, as a short column label.
     *
     *  One field lookup now that the scope is declared. It used to be
     *  reconstructed here from (transport, role, type), which meant this
     *  function had to agree with three others about what a row was, and a new
     *  kind of row was a new case in each of them. */
    inline const char* var_scope(const Variable& d) { return scope_label(d.scope); }

    /** @brief Render @p h's current value into @p buf. Returns the text - an
     *  enum's static string, a T_STR's pool text, or @p buf. */
    inline const char* var_format(const Bank& io, Var h, char* buf, size_t cap) {
        const Variable* d = h.meta();
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
