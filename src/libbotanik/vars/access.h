#ifndef VARS_ACCESS_H
#define VARS_ACCESS_H

/**
 * @file access.h
 * @brief Reading and rendering any variable through a VarRef, and grouping
 *        variables for display.
 *
 * Control programs address their signals by index through the typed handles in
 * io.h - they know which table they mean. Everything that merely *observes* a
 * variable does not: the monitor page, the shell, MQTT telemetry, the home
 * screen and the alarm reader all want "whatever this reference points at,
 * read it and render it". That is what a VarRef buys, and this file is the one
 * place that resolves one.
 *
 * The predecessor was scalar_read()/scalar_format() over a byte offset into a
 * payload struct. The offset is gone: a reference now names a table and an
 * index, so it cannot silently address the wrong field when the site's data
 * changes shape.
 */

#include <cstddef>
#include <cstdint>

#include "io.h"
#include "../utils/text.h"   // format_float / format_int

namespace Botanik { namespace Vars {

    /** @brief A named set of variables under a shared sub-topic. A site
     *  declares one VarRef[] per domain (ac_supply, environment, ...) and lists
     *  them as a VarGroup[]; telemetry publishes each as
     *  "<prefix>/telemetry/<topic>/<name>". */
    struct VarGroup {
        const char*   topic;
        const VarRef* refs;
        uint16_t      count;
    };

    /** @brief A home-screen reading: a display @p label paired with the
     *  variable whose live value it shows. A site lists these in a HomeVar[];
     *  the HomePage lays them out in two columns. The label is separate from
     *  the variable's own name because the home screen wants "Temp" where the
     *  monitor table wants "ext_temp_c". */
    struct HomeVar {
        const char* label;
        VarRef      ref;
    };

    /** @brief A resolved variable: its descriptor and its current value, with
     *  the kind already folded away.
     *
     *  @c number carries a digital as 0/1 so a consumer that only wants to
     *  print, publish or compare does not branch on kind at all. @c meta is
     *  never null - an unresolvable reference yields the same inert descriptor
     *  a bad index does, so rendering one is safe. */
    struct VarView {
        const Variable* meta;
        float           number;
        bool            digital;
        bool            valid;
    };

    /** @brief Resolve @p r against the live tables. */
    inline VarView var_view(const Io& io, VarRef r) {
        VarView v;
        v.digital = false;
        switch (r.kind) {
            case VAR_ANALOG_IN: {
                AnalogIn h = io.ai(r.index);
                v.meta = h.meta(); v.number = h.value(); v.valid = h.valid();
                break;
            }
            case VAR_ANALOG_OUT: {
                AnalogOut h = io.ao(r.index);
                v.meta = h.meta(); v.number = h.value(); v.valid = h.valid();
                break;
            }
            case VAR_INTERNAL_ANALOG: {
                AnalogOut h = io.ia(r.index);
                v.meta = h.meta(); v.number = h.value(); v.valid = h.valid();
                break;
            }
            case VAR_DIGITAL_IN: {
                DigitalIn h = io.di(r.index);
                v.meta = h.meta(); v.number = h.value() ? 1.0f : 0.0f;
                v.valid = h.valid(); v.digital = true;
                break;
            }
            case VAR_DIGITAL_OUT: {
                DigitalOut h = io.dout(r.index);
                v.meta = h.meta(); v.number = h.value() ? 1.0f : 0.0f;
                v.valid = h.valid(); v.digital = true;
                break;
            }
            case VAR_INTERNAL_DIGITAL: {
                DigitalOut h = io.id(r.index);
                v.meta = h.meta(); v.number = h.value() ? 1.0f : 0.0f;
                v.valid = h.valid(); v.digital = true;
                break;
            }
            case VAR_PARAM:
            default:
                // Parameters still live in their own store; a reference to one
                // resolves inertly rather than reading a neighbouring table.
                v.meta = io.none(); v.number = 0.0f; v.valid = false;
                break;
        }
        return v;
    }

    /** @brief Read @p r as a number, digitals included (0/1). An invalid
     *  variable reads as its default, per the model's rule. */
    inline float var_number(const Io& io, VarRef r) { return var_view(io, r).number; }

    /** @brief Read @p r as a boolean: non-zero is true, so this is meaningful
     *  for an analog too (a threshold-free "is it set"). */
    inline bool var_bool(const Io& io, VarRef r) { return var_view(io, r).number != 0.0f; }

    /** @brief Render @p v into @p buf; returns the text (an enum's static
     *  string when it maps, else @p buf). */
    inline const char* var_format(const VarView& v, char* buf, size_t cap) {
        if (v.meta->str != nullptr) {
            const char* s = v.meta->str((uint8_t)v.number);
            if (s != nullptr) return s;
        }
        if (!v.digital && v.meta->digits != 0)
            return Utils::format_float(buf, cap, (double)v.number, v.meta->digits);
        Utils::format_int(buf, cap, (long long)v.number);
        return buf;
    }

    /** @brief Resolve and render in one step. */
    inline const char* var_format(const Io& io, VarRef r, char* buf, size_t cap) {
        return var_format(var_view(io, r), buf, cap);
    }

}}  // namespace Botanik::Vars

#endif // VARS_ACCESS_H
