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

    /**
     * @brief The mechanics of one actuator: travel times and how finely it is
     * worth chasing a target.
     *
     * @c close_ms defaults to @c open_ms, but they are separate because they
     * usually are not equal - a window that opens against its own weight closes
     * faster, and using one figure for both walks the position estimate off by
     * a few percent per stroke.
     */
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
     * @brief Drives a two-position actuator - one open coil, one close coil,
     * no position feedback - to a fractional target, by timing the strokes.
     *
     * A window drive, a shade motor, a motorised valve: the field gives you two
     * contactors and nothing back. Position is therefore an ESTIMATE, integrated
     * from how long each coil was energised against the configured travel times,
     * and it drifts. Two things keep it honest:
     *
     *  - a stroke to either end runs the estimated travel PLUS @c seat_ms, so
     *    every full stroke re-datums against the mechanical stop rather than
     *    against the estimate;
     *  - the estimate is never trusted before the first such stroke.
     *
     * That is what UNKNOWN is for. After a power cycle the actuator is wherever
     * it was left, so the first command drives fully to an end before doing
     * anything else - to the closed end unless the target IS the open end, in
     * which case that stroke datums just as well.
     *
     * @section actuator_states The five states
     *
     * UNKNOWN -> CLOSING/OPENING while a coil is energised -> CLOSED or OPEN at
     * rest. CLOSED means the block believes the actuator is ON ITS SEAT, which
     * only a stroke to the closed end produces; anything else at rest is OPEN,
     * with position() saying how far. That is the valve convention, and it is
     * the useful one: "closed" is a claim about the seal, not about a number
     * being small.
     *
     * @section actuator_safety What it will not do
     *
     * The two outputs are never both true, and a reversal always passes through
     * @c reverse_ms of dead time with both dropped - contactors that make
     * before the other breaks are a short across the motor. A move smaller than
     * @c min_run_ms is not made at all: a twitch too short to overcome
     * stiction wears the gear without moving anything.
     *
     * Control authority is NOT this block's business. A task that must stop
     * driving calls stop(); the outputs it publishes go through the usual
     * ROLE_OUTPUT rows, which drop to safe on their own when control is
     * withdrawn.
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
        /** @brief cfg.reverse_ms, kept from the last step() so a stroke can be
         *  ended - by stop(), or by reaching its seat - with the dead time
         *  still applied, without the caller having to hand the config to
         *  every entry point. */
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
         *  @param dt_ms actual ms since the last call - this is what the
         *         position estimate integrates, so a late scan must not be
         *         handed the nominal period. */
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

        /** @brief Forget the position: the next command datums again. What to
         *  call when something says the actuator moved without us (a manual
         *  crank, a hand/off/auto switch coming back to auto). */
        void home();

        /** @brief Declare the position known and equal to @p pos, skipping the
         *  datum stroke. Only when something else actually establishes it. */
        void preset(float pos);

        /** @} */
    };

}}  // namespace Botanik::Plc

#endif // BOTANIK_FEATURE_PLC_BLK_BASE

#endif // PLC_BLOCKS_ACTUATOR_H
