#ifndef VARS_PARAMS_H
#define VARS_PARAMS_H

/**
 * @file params.h
 * @brief The store behind the parameters, and the helpers that read them.
 *
 * A parameter is a variable with ROLE_PARAM, min/max as edit bounds, and a name
 * that doubles as its storage key. The Bank owns the block and the store; what
 * is left here is KvParamStore and the by-name readers config-driven components
 * source their values through.
 */

#include <cstddef>
#include <cstdint>

#include "libbotanik/vars/model.h"

namespace Botanik { namespace Vars {

    /** @brief A VarStore over HAL::Storage: one key per parameter, named for
     *  it, holding the narrow value bytes.
     *
     *  Per key rather than one blob, so adding, removing or reordering a
     *  parameter cannot invalidate the others: there is no layout to version. */
    class KvParamStore : public VarStore {
    public:
        bool load(const Variable& d, Value& v, char* text, uint16_t cap) override;
        bool save(const Variable& d, const Value& v,
                  const char* text, uint16_t cap) override;
        bool load_override(const Variable& d, Value& v) override;
        bool save_override(const Variable& d, const Value& v) override;
        bool load_retained(const Variable& d, Value& v) override;
        bool save_retained(const Variable& d, const Value& v) override;
        bool clear_override(const Variable& d) override;
        bool clear(const Variable& d) override;
    };

    /**
     * @brief The parameter surface: the editable view of the ROLE_PARAM block.
     *
     * Holds no registry of its own: the variables are the registry. It owns the
     * store, runs the boot load, and gives the PARAMS page and the shell a dense
     * 0-based index over just the parameters.
     */
    /** @name Parameter helpers
     *  Free functions: the parameter block is the bank's, and these need no
     *  state of their own.
     *  @{ */

    /** @brief Find a PARAMETER by name; VAR_NONE when absent or not one. */
    VarRef param_find(const Bank* io, const char* name, uint16_t len);

    /** @brief Render @p d's declared default or bound. Not the live value -
     *  a T_STR keeps that in the pool, so it renders empty here. */
    const char* format_declared(const Variable& d, Scalar v, char* buf, size_t cap);

    /** @} */

    /** @name Read parameters by name (null-safe)
     *
     *  The config-driven components (network / NTP / MQTT / sun) source their
     *  values by NAME, so they neither know nor care where a parameter sits.
     *  @{ */

    int32_t     get_int(const Bank* io, const char* name, int32_t dflt = 0);
    float       get_f32(const Bank* io, const char* name, float dflt = 0.0f);
    /** @brief A text parameter's live text ("" if absent). Valid for the bank's
     *  lifetime - it points into the string pool. */
    const char* get_str(const Bank* io, const char* name);
    /** @brief A config group's commit serial: the parameter named
     *  "<group>_serial". A component re-applies its group when this changes. */
    uint32_t    group_serial(const Bank* io, const char* group);

    /** @} */

    /** @name Analog calibration
     *
     *  Resolving an AnalogCal needs the bank, because most of its fields name a
     *  parameter, which is why it lives here and not beside the declaration.
     *  @{ */

    /** @brief One tunable's live value: its constant, or the parameter it names
     *  (falling back to that constant when the parameter is absent). */
    float tunable(const Bank* io, const Tunable& t);

    /** @brief An analog channel's six numbers, resolved now.
     *
     *  Read together, in one call: they are used together, and per-field reads
     *  would pay a name lookup each, every sample. */
    struct AnalogValues {
        float raw_min, raw_max;
        float eng_min, eng_max;
        float min, max;
        /** @brief Slope of the raw-to-engineering conversion. 0 for a degenerate
         *  calibration (both raw endpoints equal), which would otherwise divide
         *  by zero and hand the control programs an infinity. */
        float scale() const {
            const float span = raw_max - raw_min;
            return (span == 0.0f) ? 0.0f : (eng_max - eng_min) / span;
        }
        /** @brief Intercept, so `eng = raw * scale() + offset()`. */
        float offset() const { return eng_min - raw_min * scale(); }
        /** @brief Convert a raw wire reading to engineering units. */
        float to_eng(float raw) const { return raw * scale() + offset(); }
        /** @brief The inverse, for an analog OUTPUT: engineering units to the
         *  raw count to write. Returns raw_min for a degenerate calibration. */
        float to_raw(float eng) const {
            const float sc = scale();
            return (sc == 0.0f) ? raw_min : (eng - offset()) / sc;
        }
        /** @brief Whether @p eng is inside the plausibility band. */
        bool plausible(float eng) const { return eng >= min && eng <= max; }
    };

    /** @brief Resolve @p c against @p io. A null block reads as all zeros, so a
     *  caller that forgot to check gets an inert calibration rather than
     *  garbage. */
    AnalogValues analog_values(const Bank* io, const AnalogCal* c);

    /** @brief @p d's live min/max, whatever kind of row it is: an analog row's
     *  band comes from its calibration (and may be operator-editable), every
     *  other row's from the flash constants.
     *
     *  @warning Everything that DISPLAYS a band must come through here: reading
     *  num_band() directly shows an analog row the firmware defaults rather than
     *  the parameters the operator just edited. */
    NumBand var_band(const Bank* io, const Variable& d);

    /** @} */

}}  // namespace Botanik::Vars

#endif // VARS_PARAMS_H
