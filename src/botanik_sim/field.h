#ifndef BOTANIK_SIM_FIELD_H
#define BOTANIK_SIM_FIELD_H

/**
 * @file field.h
 * @brief What a site's simulated field (its `sim.h`) writes through: the hooks
 *        botanik_sim fills in at bring-up, and the helpers built on them.
 *
 * A site provides `Site::simulate(Vars::Bank&, uint32_t now_ms)`, called every
 * beat, and writes its physical quantities with put(). Header-only, and
 * included under TARGET_SIMULATOR only.
 */

#include <cstdint>

#include "libbotanik/vars/model.h"

namespace Botanik { namespace Sim {

/** @brief Where a reading on the bus goes: into the DEVICE, which the master
 *  then polls. Null on a build with no simulated bus. */
typedef void (*FieldSink)(void* ctx, Vars::Var h, float v);
/** @brief Take a bus device off the wire (@p absent), or put it back. */
typedef void (*FieldFault)(void* ctx, uint8_t unit, bool absent);

inline FieldSink&  field_sink()  { static FieldSink fn = nullptr; return fn; }
inline FieldFault& field_fault() { static FieldFault fn = nullptr; return fn; }
inline void*&      field_ctx()   { static void* ctx = nullptr; return ctx; }

/** @brief The row @p id names. A simulator addresses rows by name; a name no
 *  table declares is VAR_NONE, the bank's inert sink. */
inline Vars::Var at(const Vars::Bank& io, const char* id) {
    return io.var(io.find(id));
}

/** @brief One physical reading: into its bus device where the row is on one,
 *  published on the row, with its timestamp and quality, where it is not. */
inline void put(Vars::Var h, float v, uint32_t now_ms) {
    if (h.meta().bind.device != 0 && field_sink() != nullptr)
        field_sink()(field_ctx(), h, v);
    else
        h.publish(Vars::Scalar::F(v), now_ms);
}
inline void put(Vars::Bank& io, uint16_t row, float v, uint32_t now_ms) {
    put(io.var(row), v, now_ms);
}
inline void put(Vars::Bank& io, const char* id, float v, uint32_t now_ms) {
    put(at(io, id), v, now_ms);
}

/** @brief Silence the bus device a named row lives on, or restore it. A device
 *  left holding its last register answers every poll and the row stays valid,
 *  so this is how a dead one is simulated. No effect on a row off the bus. */
inline void silence(const Vars::Bank& io, const char* id, bool absent) {
    const Vars::Descriptor& d = at(io, id).meta();
    if (d.bind.device != 0 && field_fault() != nullptr)
        field_fault()(field_ctx(), d.bind.device, absent);
}

}}  // namespace Botanik::Sim

#endif // BOTANIK_SIM_FIELD_H
