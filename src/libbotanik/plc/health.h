/**
 * @file health.h
 * @brief What a control program says about its own last scan.
 */

#ifndef BOTANIK_PLC_HEALTH_H
#define BOTANIK_PLC_HEALTH_H

#include <cstdint>

namespace Botanik { namespace Plc {

    /** @brief A program's verdict on its own answer, written by the LAW.
     *
     *  Coarse on purpose: anything finer belongs to the ROWS, where a reading
     *  that is not believable says so in its own quality and every consumer
     *  already reads that. This is the one thing a row cannot say - whether the
     *  law that wrote it thinks the answer is worth having.
     *
     *  Every output is written on every scan, so the question is never whether
     *  a row moved. It is what the number MEANS: a decision, a decision taken
     *  on evidence the law does not fully trust, or the safe state.
     *
     *  @note The codes are the ST enumeration's too, which matiec numbers
     *  0,1,2... in declaration order - so this order is
     *  `plc/functions/health.st`'s and cannot move. They are published as a
     *  T_ENUM row, so the vocabulary may gain codes and must never renumber
     *  the ones it has. */
    enum class Health : uint8_t {
        HEALTHY = 0,   ///< it ran, on inputs it trusts, and stands behind what it wrote
        /** @brief It produced an answer, on evidence it does not fully trust: a
         *  reduced quorum, a held reading, a fallback it chose deliberately. */
        DEGRADED,
        /** @brief It could not produce an answer at all. What it wrote is its
         *  declared safe state, which is where a program out of the scan sits
         *  too - so this is also what a program that has never run reads. */
        FAULTED
    };

    /** @brief A health CODE as a word: the converter a status row over this
     *  vocabulary takes the address of.
     *  @note Null for any code the vocabulary does not carry, which is what a
     *  T_ENUM row refuses. @see health_name() for the typed form. */
    inline const char* health_label(uint64_t code) {
        switch (code) {
            case (uint64_t)Health::HEALTHY:  return "HEALTHY";
            case (uint64_t)Health::DEGRADED: return "DEGRADED";
            case (uint64_t)Health::FAULTED:  return "FAULTED";
            default:                         return nullptr;
        }
    }

    /** @brief @p h as a word, for the TASKS page and the logs. The typed form
     *  of health_label(), so the two vocabularies cannot drift. */
    inline const char* health_name(Health h) {
        const char* n = health_label((uint64_t)h);
        return (n != nullptr) ? n : "?";
    }

}}  // namespace Botanik::Plc

#endif // BOTANIK_PLC_HEALTH_H
