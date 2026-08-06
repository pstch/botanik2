#ifndef PLC_BLOCKS_TIMERS_H
#define PLC_BLOCKS_TIMERS_H

/**
 * @file timers.h
 * @brief The three PLC timers: on-delay, off-delay, pulse.
 *
 * Each takes @p dt_ms (Plc::Context::dt_ms), the real elapsed time since the
 * program last ran, rather than reading a clock: one that ran late must not lose
 * the time it was late by.
 */

#include <cstdint>

#include "../../../libbotanik_config.h"
#if BOTANIK_FEATURE_PLC_BLK_BASE

namespace Botanik { namespace Plc {

    /** @brief On-delay: true once @p in has held true for @p delay_ms, false the
     *  moment it drops. Debounce, dwell, "has been wet for 30 s". */
    class Ton {
        uint32_t _elapsed = 0;
        bool     _q       = false;
    public:
        bool operator()(bool in, uint32_t delay_ms, uint32_t dt_ms);
        bool q() const { return _q; }
        /** @brief Elapsed dwell, for a "starting in N s" readout. */
        uint32_t elapsed_ms() const { return _elapsed; }
        void reset() { _elapsed = 0; _q = false; }
    };

    /** @brief Off-delay: follows @p in up, holds true for @p delay_ms after it
     *  drops. Minimum run time, fan overrun. */
    class Tof {
        uint32_t _elapsed = 0;
        bool     _q       = false;
    public:
        bool operator()(bool in, uint32_t delay_ms, uint32_t dt_ms);
        bool q() const { return _q; }
        uint32_t elapsed_ms() const { return _elapsed; }
        void reset() { _elapsed = 0; _q = false; }
    };

    /** @brief Pulse: a rising @p in starts a fixed @p width_ms pulse, which runs
     *  to completion whatever the input does next. */
    class Tp {
        uint32_t _elapsed = 0;
        bool     _q       = false;
        bool     _prev    = false;
    public:
        bool operator()(bool in, uint32_t width_ms, uint32_t dt_ms);
        bool q() const { return _q; }
        uint32_t elapsed_ms() const { return _elapsed; }
        void reset() { _elapsed = 0; _q = false; _prev = false; }
    };

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC_BLK_BASE

#endif // PLC_BLOCKS_TIMERS_H
