#ifndef PLC_BLOCKS_ACTUATOR_H
#define PLC_BLOCKS_ACTUATOR_H

/**
 * @file actuator.h
 * @brief Two-position actuator control: an open/close output pair driven to a
 *        fractional position by time.
 */

#include <cstdint>

#include "libbotanik/config/base.h"
#include "libbotanik/plc/blocks/block.h"
#if BOTANIK_FEATURE_PLC_BLK_BASE

namespace Botanik { namespace Plc {

    /** @brief One actuator's mechanics: travel times, and how finely it is worth
     *  chasing a target.
     *  @note @c close_ms defaults to @c open_ms but is separate: the two are
     *  rarely equal, and one figure for both walks the estimate off per stroke. */
    struct ActuatorConfig {
        uint32_t open_ms;      ///< full travel, closed -> open
        uint32_t close_ms;     ///< full travel, open -> closed
        uint32_t seat_ms;      ///< extra run at an end, to re-datum
        uint32_t min_run_ms;   ///< shortest run worth energising
        uint32_t reverse_ms;   ///< dead time before driving the other way
        float    deadband;     ///< position error ignored, as a fraction
        /** @brief Travel to allow between datums, in FULL STROKES.
         *
         *  Position is integrated from time, so every partial move adds a
         *  little error - start and stop transients, a motor that is not quite
         *  the speed the travel time says, a seal that stiffens. None of it is
         *  visible, and it only ever accumulates: nothing between the two stops
         *  can correct it. Past this much travel the next move goes to the
         *  closed seat FIRST and then out to its target, buying back a known
         *  position for one extra stroke. 0 disables it. */
        float    redatum_travel;

        constexpr ActuatorConfig(uint32_t open_ms_, uint32_t close_ms_ = 0,
                                 float deadband_ = 0.02f,
                                 uint32_t seat_ms_ = 2000,
                                 uint32_t min_run_ms_ = 250,
                                 uint32_t reverse_ms_ = 500,
                                 float redatum_travel_ = 3.0f)
            : open_ms(open_ms_),
              close_ms(close_ms_ != 0 ? close_ms_ : open_ms_),
              seat_ms(seat_ms_), min_run_ms(min_run_ms_),
              reverse_ms(reverse_ms_), deadband(deadband_),
              redatum_travel(redatum_travel_) {}
    };

    /**
     * @brief Drives an open/close coil pair to a fractional target by timing the
     *        strokes. No position feedback. Position is an ESTIMATE: a stroke to
     *        either end runs seat_ms extra to re-datum, and is UNKNOWN until one has.
     * @note Enforced invariants: the coils are never both energised, a reversal
     *       always passes through reverse_ms with both dropped (a contactor that
     *       makes before the other breaks shorts the motor), and a move under
     *       min_run_ms is not made at all.
     */
    class Actuator : public Block {
    public:
        /** @brief CLOSED means ON THE SEAT, which only a stroke to the closed end
         *  produces; anything else at rest is OPEN, with position() for the
         *  detail. The valve convention: a claim about the seal, not a number. */
        enum State {
            ST_UNKNOWN = 0,   ///< position not established since power-on
            ST_CLOSING,       ///< close coil energised
            ST_CLOSED,        ///< at rest on the closed seat
            ST_OPENING,       ///< open coil energised
            ST_OPEN           ///< at rest off the seat (fully or partly open)
        };

    private:
        enum Dir { D_NONE = 0, D_OPEN, D_CLOSE };

        /** @name State
         *  @{ */

        float    _pos     = 0.0f;   ///< 0 closed .. 1 open; meaningless if UNKNOWN
        uint32_t _run_ms  = 0;      ///< time left in the committed stroke
        uint32_t _wait_ms = 0;      ///< reversal dead time left
        uint32_t _reverse_ms = 0;   ///< cfg.reverse_ms kept, so stop()/arrive() need no config
        uint8_t  _state   = ST_UNKNOWN;
        uint8_t  _dir     = D_NONE;
        bool     _seating = false;  ///< this stroke ends at a seat, so snap there
        bool     _datum   = false;  ///< ...and it is the one establishing position
        /** @brief Travel since the last seat, in full strokes: the estimate's
         *  accumulated uncertainty. Only a seat clears it. */
        float    _since_datum = 0.0f;
        /** @brief Where to go once the re-datum stroke has finished, or -1. */
        float    _pending = -1.0f;
        bool     _open    = false;  ///< open coil demand
        bool     _close   = false;  ///< close coil demand

        /** @} */
        /** @name Internals
         *  @{ */

        /** @brief The direction @p target asks for, or D_NONE inside the deadband. */
        Dir want(float target, const ActuatorConfig& cfg) const;
        /** @brief Advance the estimate by @p ran ms of travel in _dir. */
        void travel(uint32_t ran, const ActuatorConfig& cfg);
        /** @brief Arm a stroke toward @p target, if one is worth making. */
        void start(float target, const ActuatorConfig& cfg);
        /** @brief End of a committed stroke: snap to the seat if it was one. */
        void arrive();
        /** @brief Resolve the resting state from the position estimate. */
        void settle();

        /** @} */

    public:
        /** @name Operation
         *  @{ */

        /** @brief Run one scan.
         *  @param target 0 (closed) to 1 (open); clamped.
         *  @param dt_ms actual ms since the last call - the estimate integrates
         *         it, so a late scan must not be handed the nominal period. */
        void step(float target, const ActuatorConfig& cfg, uint32_t dt_ms);

        /** @brief Drop both coils and abandon the stroke. The estimate stands. */
        void stop();

        State state() const { return (State)_state; }
        /** @brief "UNKNOWN" / "CLOSING" / "CLOSED" / "OPENING" / "OPEN". */
        const char* state_name() const;
        /** @brief Position estimate, 0..1. Only meaningful once known(). */
        float position() const { return _pos; }

        /** @brief Travel since the last datum, in full strokes. Past the
         *  configured allowance the next move re-datums first. */
        float drift() const { return _since_datum; }
        /** @brief Whether the next move will detour to the closed seat. */
        bool  redatum_due(const ActuatorConfig& cfg) const {
            return cfg.redatum_travel > 0.0f && _since_datum >= cfg.redatum_travel;
        }
        bool known() const { return _state != ST_UNKNOWN; }
        /** @brief Whether a coil is energised right now. */
        bool moving() const { return _open || _close; }

        bool open_cmd() const { return _open; }
        /** @brief Never true at the same time as open_cmd(). */
        bool close_cmd() const { return _close; }

        /** @brief Forget the position, so the next command datums again: a manual
         *  crank, hand/off/auto coming back to auto. */
        void home();

        void reset() { home(); }

        /** @brief Declare the position known, skipping the datum stroke. Only
         *  when something else actually establishes it. */
        void preset(float pos);

        /** @} */
    };

    /** @name Family checks
     *  @{ */
    BOTANIK_PLC_BLOCK(Actuator);
    /** @} */

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC_BLK_BASE

#endif // PLC_BLOCKS_ACTUATOR_H
