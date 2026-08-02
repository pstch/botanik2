#ifndef VARS_PARAMS_H
#define VARS_PARAMS_H

/**
 * @file params.h
 * @brief Parameters: the persisted, editable variables.
 *
 * A parameter is not a separate kind of thing - it is a variable with
 * ROLE_PARAM, min/max as edit bounds, and a name that doubles as its storage
 * key. What is left here is the two behaviours a parameter has and other
 * variables do not: it persists, and a user can edit it.
 *
 * The predecessor was a parallel model - its own descriptor, its own registry,
 * and a flat byte image addressed by out_offset. That image is what let a
 * parameter overrun the buffer it lived in and be silently dropped at boot.
 * Addressing by index removes the failure mode rather than guarding it.
 */

#include <cstddef>
#include <cstdint>

#include "../core/component.h"
#include "model.h"

namespace Botanik { namespace Vars {

    /** @brief A ParamStore over HAL::Storage: one key per parameter, named for
     *  it, holding the narrow value bytes.
     *
     *  Per key rather than one blob so that adding, removing or reordering a
     *  parameter cannot invalidate the others - there is no layout to version. */
    class KvParamStore : public ParamStore {
    public:
        bool load(const Variable& d, Value& v, char* pool) override;
        bool save(const Variable& d, const Value& v, const char* pool) override;
    };

    /**
     * @brief The parameter surface: the editable view of the ROLE_PARAM block.
     *
     * Holds no registry of its own - the variables are the registry. It exists
     * to own the store, run the boot load, and give the PARAMS page and the
     * shell a dense 0-based index over just the parameters.
     */
    class Params : public Component {
        Bank* _io;
    public:
        /** @brief Binds @p store to @p io: the bank is what actually reads and
         *  writes variables (set() included), so it is what holds the store. */
        Params(Bank* io, ParamStore* store, Logs::Logger* logger)
            : Component(logger, "Params"), _io(io) {
            if (_io != nullptr) _io->set_store(store, logger);
        }

        /** @name Access
         *  @{ */
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

        /** @brief Apply an edited value, as display text, then persist it. */
        SetResult set(Var p, const char* text) {
            return _io != nullptr ? _io->set(p, text) : SET_INVALID;
        }

    protected:
        /** @brief Load every parameter from the store. */
        void setup(uint64_t now_ms) override;
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

}}  // namespace Botanik::Vars

#endif // VARS_PARAMS_H
