#ifndef PLC_BLOCKS_ACTUATOR_H
#define PLC_BLOCKS_ACTUATOR_H

/**
 * @file actuator.h
 * @brief Two-position actuator control: an open/close output pair driven to a
 *        fractional position by time.
 */

#include <cstdint>                    // uint8_t/uint32_t - the five-state enum and the stroke timers

#include "libbotanik_config.h"        // BOTANIK_FEATURE_PLC_BLK_BASE - the gate this whole header sits behind
#if BOTANIK_FEATURE_PLC_BLK_BASE

namespace Botanik { namespace Plc {

    /** @brief One actuator's mechanics: travel times, and how finely it is
     *  worth chasing a target.
     *
     *  @c close_ms defaults to @c open_ms but is separate because the two are
     *  rarely equal - a window that opens against its own weight closes faster,
     *  and one figure for both walks the estimate off a few percent per stroke. */
    struct ActuatorConfig {
        uint32_t open_ms;      ///< full travel, closed -> open
        uint32_t close_ms;     ///< full travel, open -> closed
        uint32_t seat_ms;      ///< extra run at an end, to re-datum
        uint32_t min_run_ms;   ///< shortest run worth energising
        uint32_t reverse_ms;   ///< dead time before driving the other way
        float    deadband;     ///< position error ignored, as a fraction

        constexpr ActuatorConfig(uint32_t open_ms_, uint32_t close_ms_ = 0,
                                 float deadband_ = 0.02f,
                                 uint32_t seat_ms_ = 2000,
                                 uint32_t min_run_ms_ = 250,
                                 uint32_t reverse_ms_ = 500)
            : open_ms(open_ms_),
              close_ms(close_ms_ != 0 ? close_ms_ : open_ms_),
              seat_ms(seat_ms_), min_run_ms(min_run_ms_),
              reverse_ms(reverse_ms_), deadband(deadband_) {}
    };

    /**
     * @brief Drives an open/close coil pair to a fractional target by timing the
     * strokes. No position feedback: a window drive, a shade motor, a motorised
     * valve.
     *
     * Position is an ESTIMATE, integrated from coil-on time against the
     * configured travel, so it drifts. Two rules keep it honest: a stroke to
     * either end runs the estimated travel PLUS @c seat_ms, re-datuming against
     * the mechanical stop; and the estimate is not trusted before the first such
     * stroke - which is what UNKNOWN is for, since after a power cycle the
     * actuator is wherever it was left.
     *
     * CLOSED means ON THE SEAT, which only a stroke to the closed end produces;
     * anything else at rest is OPEN, with position() for the detail. That is the
     * valve convention: "closed" is a claim about the seal, not about a small
     * number.
     *
     * Invariants: the two coils are never both energised, a reversal always
     * passes through @c reverse_ms with both dropped (contactors that make
     * before the other breaks short the motor), and a move under @c min_run_ms
     * is not made at all (a twitch too short to beat stiction wears the gear
     * without moving anything).
     *
     * Control authority is not this block's business - a program that must stop
     * driving calls stop(), and the coil rows drop to safe on their own.
     */
    class Actuator {
    public:
        /** @brief Where the actuator is, as far as the block can tell. */
        enum State {
            ST_UNKNOWN = 0,   ///< position not established since power-on
            ST_CLOSING,       ///< close coil energised
            ST_CLOSED,        ///< at rest on the closed seat
            ST_OPENING,       ///< open coil energised
            ST_OPEN           ///< at rest off the seat (fully or partly open)
        };

    private:
        /** @brief Which coil a stroke is driving. */
        enum Dir { D_NONE = 0, D_OPEN, D_CLOSE };

        /** @name State
         *  @{ */

        float    _pos     = 0.0f;   ///< 0 closed .. 1 open; meaningless if UNKNOWN
        uint32_t _run_ms  = 0;      ///< time left in the committed stroke
        uint32_t _wait_ms = 0;      ///< reversal dead time left
        /** @brief cfg.reverse_ms kept from the last step(), so stop() and
         *  arrive() can apply the dead time without being handed the config. */
        uint32_t _reverse_ms = 0;
        uint8_t  _state   = ST_UNKNOWN;
        uint8_t  _dir     = D_NONE;
        bool     _seating = false;  ///< this stroke ends at a seat, so snap there
        bool     _datum   = false;  ///< ...and it is the one establishing position
        bool     _open    = false;  ///< open coil demand
        bool     _close   = false;  ///< close coil demand

        /** @} */
        /** @name Internals
         *  @{ */

        /** @brief The direction @p target asks for, or D_NONE inside the
         *  deadband. */
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
        /** @brief Whether the actuator has been datumed since power-on. */
        bool known() const { return _state != ST_UNKNOWN; }
        /** @brief Whether a coil is energised right now. */
        bool moving() const { return _open || _close; }

        /** @brief The open coil demand. */
        bool open_cmd() const { return _open; }
        /** @brief The close coil demand. Never true at the same time as
         *  open_cmd(). */
        bool close_cmd() const { return _close; }

        /** @brief Forget the position, so the next command datums again: a
         *  manual crank, hand/off/auto coming back to auto. */
        void home();

        /** @brief Declare the position known, skipping the datum stroke. Only
         *  when something else actually establishes it. */
        void preset(float pos);

        /** @} */
    };

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC_BLK_BASE

#endif // PLC_BLOCKS_ACTUATOR_H
