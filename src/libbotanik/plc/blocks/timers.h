#ifndef PLC_BLOCKS_TIMERS_H
#define PLC_BLOCKS_TIMERS_H

/**
 * @file timers.h
 * @brief The three PLC timers: on-delay, off-delay, pulse.
 *
 * Each takes @p dt_ms (Plc::Context::dt_ms), the ACTUAL elapsed time since the
 * task last ran, rather than reading a clock: a task that ran late must not
 * lose the time it was late by.
 */

#include <cstdint>

#include "../../../libbotanik_config.h"
#if BOTANIK_FEATURE_PLC_BLK_BASE

namespace Botanik { namespace Plc {

    /** @brief On-delay. Output goes true once the input has been continuously
     *  true for @p delay_ms, and false immediately when the input drops. The
     *  workhorse: debounce, dwell, "has been wet for 30 s". */
    class Ton {

        /** @name State
         *  @{ */

        uint32_t _elapsed = 0;
        bool     _q       = false;

        /** @} */

    public:
        /** @name Operation
         *  @{ */

        bool operator()(bool in, uint32_t delay_ms, uint32_t dt_ms);
        bool q() const { return _q; }
        /** @brief Elapsed dwell, for a "starting in N s" readout. */
        uint32_t elapsed_ms() const { return _elapsed; }
        void reset() { _elapsed = 0; _q = false; }

        /** @} */
    };

    /** @brief Off-delay. Output follows the input up, and holds true for
     *  @p delay_ms after the input drops. Minimum run time, fan overrun. */
    class Tof {

        /** @name State
         *  @{ */

        uint32_t _elapsed = 0;
        bool     _q       = false;

        /** @} */

    public:
        /** @name Operation
         *  @{ */

        bool operator()(bool in, uint32_t delay_ms, uint32_t dt_ms);
        bool q() const { return _q; }
        uint32_t elapsed_ms() const { return _elapsed; }
        void reset() { _elapsed = 0; _q = false; }

        /** @} */
    };

    /** @brief Pulse. A rising input starts a fixed @p width_ms pulse; the pulse
     *  runs to completion whatever the input does afterwards. */
    class Tp {

        /** @name State
         *  @{ */

        uint32_t _elapsed = 0;
        bool     _q       = false;
        bool     _prev    = false;

        /** @} */

    public:
        /** @name Operation
         *  @{ */

        bool operator()(bool in, uint32_t width_ms, uint32_t dt_ms);
        bool q() const { return _q; }
        uint32_t elapsed_ms() const { return _elapsed; }
        void reset() { _elapsed = 0; _q = false; _prev = false; }

        /** @} */
    };

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC_BLK_BASE

#endif // PLC_BLOCKS_TIMERS_H
