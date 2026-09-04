/**
 * @file health.h
 * @brief A control program's own verdict: wired, or not.
 */

#ifndef BOTANIK_PLC_HEALTH_H
#define BOTANIK_PLC_HEALTH_H

#include <cstdint>

namespace Botanik { namespace Plc {

    /** @brief What is worth saying about a program. Anything finer belongs to
     *  the ROWS - a reading that is not believable says so in its own quality,
     *  and every consumer reads that.
     *
     *  There was a second code, for a program whose bindings did not resolve.
     *  A binding is a generated index now, so that cannot happen: a name no
     *  table declares fails to compile.
     *
     *  @note The codes are published as a T_ENUM row, so the vocabulary may
     *  gain labels and must never renumber them. */
    enum class Health : uint8_t {
        HEALTHY = 0    ///< wired, and running
    };

    /** @brief A health CODE as a word: the converter a status row over this
     *  vocabulary takes the address of.
     *  @note Null for anything that is not one of the two, which is what a
     *  T_ENUM row refuses. @see health_name() for the typed form. */
    inline const char* health_label(uint64_t code) {
        switch (code) {
            case (uint64_t)Health::HEALTHY: return "HEALTHY";
            default:                        return nullptr;
        }
    }

    /** @brief @p h as a word, for the TASKS page and the logs. The typed form
     *  of health_label(), so the two vocabularies cannot drift. */
    inline const char* health_name(Health h) {
        const char* n = health_label((uint64_t)h);
        return (n != nullptr) ? n : "?";
    }

}}  // namespace Botanik::Plc

#endif
