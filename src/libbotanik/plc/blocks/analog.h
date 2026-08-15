#ifndef PLC_BLOCKS_ANALOG_H
#define PLC_BLOCKS_ANALOG_H

/**
 * @file analog.h
 * @brief Thresholding and shaping of a continuous reading: hysteresis (two- and
 *        three-band), rate limit, deadband.
 * @note IEC 61131-3: HYSTERESIS, RAMP.
 */

#include <cstdint>

#include "libbotanik/config/base.h"
#include "libbotanik/plc/blocks/block.h"
#if BOTANIK_FEATURE_PLC_BLK_BASE

namespace Botanik { namespace Plc {

    /** @name Thresholds
     *  @{ */

    /** @brief Threshold with hysteresis (IEC HYSTERESIS): true at or above
     *  @p high, false at or below @p low, holding its state between them. */
    class Hysteresis : public Block {

        bool _q = false;

    public:
        bool operator()(float value, float low, float high);
        bool q() const { return _q; }
        /** @brief Declare the current state without a reading, for a cold start
         *  that knows better than "false". */
        void preset(bool q) { _q = q; }
        void reset() { _q = false; }

    };

    /**
     * @brief Three-band threshold, each edge with its own hysteresis: LOW at or
     *        below @p low (left at @p low + @p hyst), HIGH at or above @p high
     *        (left at @p high - @p hyst), MID between. Two IEC HYSTERESIS blocks.
     * @note The first call seeds from the raw thresholds, so a block starting
     *       mid-band reports that band rather than waiting for an edge.
     * @warning @p hyst must be under half of (@p high - @p low), or the bands
     *          overlap: HIGH then wins over LOW and MID can vanish.
     */
    class Hysteresis3 : public Block {
    public:
        enum Band { BAND_LOW = 0, BAND_MID = 1, BAND_HIGH = 2 };

    private:
        Hysteresis _clear_low;   ///< true once the reading is clear of the low band
        Hysteresis _in_high;
        uint8_t    _band = BAND_MID;
        bool       _seed = false;

    public:
        Band operator()(float value, float low, float high, float hyst);
        Band band() const { return (Band)_band; }
        bool low() const  { return _band == BAND_LOW; }
        bool mid() const  { return _band == BAND_MID; }
        bool high() const { return _band == BAND_HIGH; }
        void reset();

    };

    /** @}
     *  @name Shaping
     *  @{ */

    /** @brief Rate limiter (IEC RAMP). Moves the output toward @p target by at
     *  most @p rate_per_s, so a step demand becomes a ramp the mechanics can
     *  follow. */
    class Ramp : public Block {

        float _out  = 0.0f;
        bool  _seed = false;

    public:
        float operator()(float target, float rate_per_s, uint32_t dt_ms);
        float value() const { return _out; }
        /** @brief Jump to @p v without ramping (mode changes, first enable). */
        void preset(float v) { _out = v; _seed = true; }
        /** @brief Cold: the next activation adopts its target rather than ramping
         *  toward it from a position the plant has since left. */
        void reset() { _out = 0.0f; _seed = false; }

    };

    /** @brief Reports @p value only once it has moved more than @p band from the
     *  last reported figure. Suppresses jitter without lagging a real change. */
    class Deadband : public Block {

        float _held = 0.0f;
        bool  _seed = false;

    public:
        float operator()(float value, float band);
        float value() const { return _held; }
        /** @brief Cold: the next reading is reported whatever it is, rather than
         *  suppressed against a held value from before the gap. */
        void reset() { _held = 0.0f; _seed = false; }

    };

    /** @} */

    /** @name Family checks
     *  @{ */
    BOTANIK_PLC_BLOCK(Hysteresis);
    BOTANIK_PLC_BLOCK(Hysteresis3);
    BOTANIK_PLC_BLOCK(Ramp);
    BOTANIK_PLC_BLOCK(Deadband);
    /** @} */

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC_BLK_BASE

#endif // PLC_BLOCKS_ANALOG_H
