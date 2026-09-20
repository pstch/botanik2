#ifndef VARS_ACCESS_H
#define VARS_ACCESS_H

/**
 * @file access.h
 * @brief Grouping variables for display, rendering one, and the by-row reads a
 *        configurable component's binding is written with.
 *
 * Deliberately thin: grouping is a RULE over the one table (a group is a name
 * prefix), and what is published is the row's Kind. The home-screen row is the
 * one thing no rule derives.
 */

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "libbotanik/config/vars.gen.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/vars/model.h"

namespace Botanik { namespace Vars {

    /** @brief A home-screen reading: a display @p label paired with the ROW
     *  whose live value it shows. The label is declared because no rule derives
     *  "Line V" from "grid_LN_V"; the row is a generated index. */
    struct HomeVar {
        const char* label;
        uint16_t    row;
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

    /** @brief @p d's access as a point list writes it. */
    inline const char* access_label(const Descriptor& d) {
        return writable(d) ? "RW" : "RO";
    }

    /** @} */
    /** @name Rendering
     *  @{ */

    /** @brief Render one LAYER of @p h into @p buf. Returns the text - an
     *  enum's static string, a T_STR's pool text, or @p buf.
     *  @note A T_STR keeps its live text in the pool rather than the slot, so
     *  @p v is not where its value is and the layer cannot be honoured for one. */
    inline const char* var_format(Var h, Value v, char* buf, size_t cap) {
        const Descriptor& d = h.meta();
        if (d.type == T_STR) {
            const char* t = h.text();
            return (t != nullptr) ? t : "";
        }
        return format_scalar(d.type, v.v, d.digits, d.labels, buf, cap);
    }

    /** @brief Render the value in force: the override where one is on. */
    inline const char* var_format(Var h, char* buf, size_t cap) {
        return var_format(h, h.value(), buf, cap);
    }

    /** @brief Render @p d's declared default or bound. Not the live value - a
     *  T_STR keeps that in the pool, so it renders empty here. */
    inline const char* format_declared(const Descriptor& d, Scalar v,
                                       char* buf, size_t cap) {
        if (d.type == T_STR) return "";
        return format_scalar(d.type, v, d.digits, d.labels, buf, cap);
    }

    /** @brief Where @p d's value physically comes from, in words: what the
     *  MONITOR page's Mapping field and `vars info`'s Source line both show.
     *  @param dev_tag the plant's name for the device, or null to print only
     *  the unit number an operator would otherwise have to look up. */
    inline const char* binding_phrase(const Descriptor& d, char* buf, size_t cap,
                                      const char* dev_tag = nullptr) {
        const Binding& b = d.bind;
        if (!bound(d))
            std::snprintf(buf, cap, "computed (nothing drives it)");
        else if (pulse(d))
            // A counter, not a level: the row IS the running edge total.
            std::snprintf(buf, cap, "pulse counter on channel %u, %s edges",
                     (unsigned)pulse_channel(d),
                     HAL::Pulse::edge_label(pulse_edge(d)));
        else if (local(d))
            // The wire type is what makes a local channel analog or digital;
            // the device address only says it is not on the bus.
            std::snprintf(buf, cap, "%s channel %u",
                     (b.wire == T_BOOL) ? "digital" : "analog", (unsigned)b.reg);
        else if (dev_tag != nullptr && dev_tag[0] != '\0')
            std::snprintf(buf, cap, "Modbus %s (dev %u) fc %u reg %u",
                     dev_tag, (unsigned)b.device, (unsigned)b.fc, (unsigned)b.reg);
        else
            std::snprintf(buf, cap, "Modbus dev %u fc %u reg %u",
                     (unsigned)b.device, (unsigned)b.fc, (unsigned)b.reg);
        return buf;
    }

    /** @} */
    /** @name By-row reads
     *
     *  What a configurable component's binding is written with. Null-safe, so a
     *  binding reads the same whether the build has a bank or not.
     *  @{ */

    inline int32_t get_int(const Bank* io, uint16_t row, int32_t dflt = 0) {
        if (io == nullptr || row >= io->count()) return dflt;
        return (int32_t)io->var(row).as_i64();
    }
    /** @brief A text row's live text ("" if absent). Valid for the bank's
     *  lifetime - it points into the string pool. */
    inline const char* get_str(const Bank* io, uint16_t row) {
        if (io == nullptr || row >= io->count()) return "";
        const char* t = io->var(row).text();
        return (t != nullptr) ? t : "";
    }

    /** @} */

}}  // namespace Botanik::Vars

#endif // VARS_ACCESS_H
