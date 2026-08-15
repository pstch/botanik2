#ifndef VARS_PARAMS_H
#define VARS_PARAMS_H

/**
 * @file params.h
 * @brief Parameters: the persisted, editable variables.
 *
 * A parameter is a variable with ROLE_PARAM, min/max as edit bounds, and a name
 * that doubles as its storage key. What lives here is the two behaviours other
 * variables do not have: it persists, and a user can edit it.
 *
 * Addressed by INDEX into the bank, never by an offset into a flat byte image.
 */

#include <cstddef>
#include <cstdint>

#include "libbotanik/core/component.h"
#include "libbotanik/vars/model.h"

namespace Botanik { namespace Tasks { struct Tick; } }

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
    class Params : public Component {
        Bank* _io;
        /** @brief When the retained rows were last written out; 0 until the
         *  first save. @see Config::RETAIN_SAVE_MS */
        uint64_t _last_retain_ms = 0;
    public:
        /** @brief Binds @p store to @p io: the bank reads and writes variables
         *  (set() included), so it is what holds the store. */
        Params(const SetupContext& ctx, Bank* io, VarStore* store)
            : Component(ctx, "Params"), _io(io) {
            if (_io != nullptr) _io->set_store(store, ctx.logger);
        }

        /** @name Access
         *  @{ */
        /** @brief The bank behind this view - what resolves a calibration's
         *  parameter names. May be null. */
        const Bank* bank() const { return _io; }
        uint16_t count() const { return _io != nullptr ? _io->param_count() : 0; }
        /** @brief The @p k-th parameter, 0-based over the block. */
        Var      param(uint16_t k) const { return _io->param(k); }
        /** @brief Find a parameter by name; VAR_NONE when absent or not a
         *  parameter. */
        VarRef   find(const char* name, uint16_t len) const;
        /** @brief Render parameter @p k's current value. */
        const char* format(uint16_t k, char* buf, size_t cap) const;
        /** @brief Render @p d's declared default / bound, for the detail panel. */
        const char* format_scalar_of(const Variable& d, Scalar v,
                                     char* buf, size_t cap) const;
        /** @} */

        /** @name Editing
         *
         *  Two steps, deliberately: set() is live, save() is durable. @see
         *  Bank::set()
         *  @{ */

        /** @brief Apply an edited value, as display text. Live, not saved. */
        SetResult set(Var p, const char* text) {
            return _io != nullptr ? _io->set(p, text) : SET_INVALID;
        }
        /** @brief Commit one edited parameter to flash. */
        SetResult save(Var p) {
            return _io != nullptr ? _io->save(p) : SET_INVALID;
        }
        /** @brief Commit every parameter still unsaved. @return how many. */
        uint16_t save_all(uint16_t* refused = nullptr) {
            return _io != nullptr ? _io->save_params(refused) : 0;
        }
        /** @brief How many edits are live but not yet committed. */
        uint16_t unsaved() const {
            return _io != nullptr ? _io->unsaved_count() : 0;
        }

        /** @} */

    public:
        /** @brief One task, PHASE_CONFIG: the parameter image everything below
         *  it in the beat reads its config from. */
        void register_tasks() override;

    private:
        /** @brief The registration handler. A static MEMBER, so the cast has
         *  access to the protected hook and stays beside the class. */
        static void _config_task(Component* c, const Tasks::Tick& t);

        /** @brief Push `sys_log_level` into the logger.
         *
         *  Here because this is the one object already holding BOTH the bank and
         *  a logger, so it costs no dependency at all - `vars/` depends on
         *  `logs/` already, since the Bank reports a refused write through one.
         *
         *  The logger cannot read it for itself: that would mean `logs/`
         *  depending back on `vars/`, and the lowest-level service in the tree
         *  would stop building without the variable model. `set_min_level()` is
         *  the indirection, and applying a parameter to the thing that cannot
         *  read it is the same job as loading them and pacing their writes. */
        void _apply_log_level();

    public:
        /** @brief Write every retained row out NOW, whatever the cadence, and
         *  restart the interval from this moment.
         *
         *  What the shell's `save` command calls. The cadence bounds how much
         *  counting an unexpected power cut can cost; this is for when the cut
         *  is not unexpected - before pulling a fuse, after a commissioning run,
         *  or as the whole mechanism on a unit running `sys_retain_sec 0`.
         *
         *  @return rows written; 0 when there are none or there is no store. */
        uint16_t save_retained(uint64_t now_ms) {
            if (_io == nullptr) return 0;
            _last_retain_ms = now_ms;
            return _io->save_retained();
        }

    protected:
        /** @brief Load every parameter from the store. */
        void on_setup(uint64_t now_ms) override;
        /** @brief Write the retained rows out, at most once per
         *  `sys_retain_sec` (Config::RETAIN_SAVE_MS is its default). Nothing
         *  else per beat. */
        void on_config(uint64_t now_ms) override;

    private:
        /** @brief The retain cadence in ms: `sys_retain_sec` if the row is
         *  there, else Config::RETAIN_SAVE_MS. 0 means "only on the way down,
         *  and on `save`". */
        uint32_t _retain_ms() const;
    };

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
