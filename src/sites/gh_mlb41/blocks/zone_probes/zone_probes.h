#ifndef SITE_BLOCKS_ZONE_PROBES_H
#define SITE_BLOCKS_ZONE_PROBES_H

/**
 * @file zone_probes.h
 * @brief HAND-WRITTEN - one measurement location: three probes, voted and filtered.
 */

#include <cstdint>

#include "libbotanik.h"

namespace Site {

using namespace Botanik;

/** @brief What a three-probe selection came to: the reading, and how many
 *  probes were behind it. @c used of 0 means nothing was believable. */
struct ZoneVote {
    float   value;
    uint8_t used;
};

/**
 * @brief One measurement location: three probes selected down to one reading,
 *        then filtered.
 *
 * The selection drops the OUTLIER and averages the two that agree, which keeps
 * the second good probe's information where a plain median discards it. Only
 * VALID probes take part; with none left the zone's rows go invalid rather than
 * publishing a number nothing measured.
 *
 * Not a Program - a location is a part OF one. Held by value so one zone's
 * filter state cannot be shared with another's by accident.
 */
struct ZoneProbeRefs {
    /** @name VAR_INPUT
     *  Three probes per quantity: the vote is what the third one buys. @{ */
    Vars::RefIn<float>  t[3];
    Vars::RefIn<float>  h[3];
    /** @} */
    /** @name VAR_OUTPUT
     *  @{ */
    Vars::RefOut<float> temp;
    Vars::RefOut<float> rh;
    Vars::RefOut<bool>  fault;
    /** @} */
};

class ZoneProbes : public Plc::Block {

    /** @name Blocks
     *  Filtered against dt, not against a fixed blend: a program due late runs
     *  ONCE with the real elapsed time, so a filter carrying a constant alpha
     *  silently changes its time constant the moment the scan slips. The tau is
     *  the same for every zone (cli_sensorTau_ms) and the block does the rest.
     *  @{ */

    Plc::Ewma _temp;
    Plc::Ewma _rh;
    Plc::Ton                _quorum_for;   ///< dwell before a lost probe is announced
    bool                    _temp_ok     = false;
    bool                    _temp_seeded = false;
    bool                    _rh_seeded   = false;

    /** @} */
    /** @name Variables
     *  @{ */

    ZoneProbeRefs _r;
    float         _tau_s = 1.0f;

    /** @} */

public:
    /** @brief Adopt this zone's three probes, the two conditioned rows they
     *  feed and its quorum alarm - all resolved by the program that owns them.
     *  @p tau_s is the filter's time constant, in seconds. */
    void bind(const ZoneProbeRefs& r, float tau_s) { _r = r; _tau_s = tau_s; }

    /** @brief Select, filter and publish. @return how many probes were used. */
    uint8_t operator()(uint32_t dt_ms);

    float temperature() const { return _r.temp.get(); }
    float humidity() const    { return _r.rh.get(); }
    /** @brief Whether this zone MEASURED its temperature this beat. The
     *  block's own verdict, not the row read back: it becomes a VAR_OUTPUT
     *  when this block is ported, and a `_qlt` row where a law needs it. */
    bool valid() const { return _temp_ok; }

    void reset() {
        _quorum_for.reset();
        _temp.reset(); _rh.reset();
        _temp_ok = false;
        _temp_seeded = _rh_seeded = false;
    }

private:
    /** @brief The three readings of one quantity, reduced. Invalid probes do
     *  not take part; the outlier of what is left is dropped. */
    static ZoneVote _select(const Vars::RefIn<float>* refs);

    /** @brief Filter and publish one quantity, or mark it unmeasured. */
    static bool _publish(Vars::RefOut<float> row, Plc::Ewma& filter,
                         const ZoneVote& v, bool& seeded, float tau_s,
                         uint32_t dt_ms, float lo, float hi);
};

BOTANIK_PLC_BLOCK(ZoneProbes);

}  // namespace Site

#endif // SITE_BLOCKS_ZONE_PROBES_H
