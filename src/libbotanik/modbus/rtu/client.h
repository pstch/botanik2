#ifndef MODBUS_RTU_CLIENT_H
#define MODBUS_RTU_CLIENT_H

/**
 * @file modbus/rtu/client.h
 * @brief The RTU master: one transaction in flight, driven from the beat.
 *
 * @section cl_split Why it is stepped twice
 *
 * Like the serial pump, whose halves the loop drives at the phases they belong
 * to. receive_step() runs at the top of the beat, so a control program sees the
 * freshest image; transmit_step() runs after the programs have executed, so a
 * demand written this beat reaches the wire without waiting for the next one.
 * Stepping it once, at either end, would cost a beat of latency in one direction
 * or the other.
 *
 * @section cl_gate Collect is not gated with execute
 *
 * transmit_step() runs whatever control authority says. disable_control() drops
 * every output to its safe state and inhibits the scan, but those safe values are
 * only in the image until a transaction carries them out. Gating the collector
 * alongside the programs would leave the actuators energised at exactly the
 * moment control was withdrawn - which is the one outcome the safe state exists
 * to prevent.
 *
 * @section cl_dead What a silent device costs, and what is done about it
 *
 * A device that is unplugged answers nothing, and the transaction it was asked
 * for costs a full response timeout. Left alone, that is paid on EVERY sweep:
 * three dead slaves at a 300 ms timeout burn 2.7 s against a 555 ms budget for
 * the whole plant (docs/design/modbus-budget.md), so the live devices lose their
 * rates to the dead ones. This is what actually takes a field bus down - well
 * ahead of register counts.
 *
 * So each device carries its own health: consecutive failures, and the earliest
 * time it may be spoken to again. One immediate retry (weather), then backoff
 * doubling from MODBUS_BACKOFF_MS to MODBUS_BACKOFF_MAX_MS, and `_pick()` simply
 * does not offer the bus to a device that is not due. The rest of the plan keeps
 * running at its own rates throughout.
 *
 * A REFUSAL clears the count. An exception response means the device heard us,
 * understood us and declined - it is alive, and its register map is the problem.
 * Treating that as silence would back off a device that is answering perfectly.
 *
 * @section cl_frame A frame goes out whole or not at all
 *
 * The driver enable is hardware-timed: the USART drops DE when its shift
 * register empties. So a frame handed to the UART in pieces would have DE fall
 * in the middle of it, and every slave on the bus would see a truncated frame -
 * which reads as a wiring fault, not a firmware one.
 *
 * Two things make that unreachable rather than merely unlikely, because the
 * claim used to rest on a comment. A `static_assert` in client.cpp proves a full
 * FC16 fits the transmit ring (MODBUS_TX_RING_BYTES), and on_step() will not
 * begin a transaction until fieldbus_tx_idle() says the ring has DRAINED - not
 * merely that it has room for one more byte. A refused byte is then impossible,
 * and is logged as a critical if it ever happens anyway.
 */

#include <cstdint>

#include "libbotanik/core/component.h"
// The real header, not a forward declaration: on OPTA the Arduino core
// macro-expands `Serial`, so `namespace Serial { class Raw; }` written here
// declares a DIFFERENT type from the one HAL::Serial::get() returns, and the two
// only fail to meet at the link. Same reason utils/serial.h includes it.
#include "libbotanik/hal/platform.h"
#include "libbotanik/modbus/rtu/framing.h"
#include "libbotanik/modbus/rtu/image.h"
#include "libbotanik/modbus/rtu/plan.h"
#include "libbotanik/vars/model.h"       // Vars::VarRef - the alarm and status rows, bound by name

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

namespace Botanik { namespace Vars { class Bank; } }

namespace Botanik { namespace Modbus {

    /**
     * @brief Keeps the variable table in step with the field bus.
     *
     * Half duplex and strictly request/response, so there is exactly one
     * transaction in flight and the state machine is a small ring: pick work,
     * send it, wait for the wire to clear, listen, leave a gap, pick again.
     *
     * Shaped like NtpClient - a state, an entry stamp, one non-blocking action
     * per beat - because the constraint is the same: nothing may block, and every
     * timeout is a comparison against the stamp rather than a sleep.
     */
    class RtuClient : public Component {

        /** @name State
         *  @{ */

        Vars::Bank*         _io;
        HAL::Serial::Raw*   _port = nullptr;   ///< resolved at setup, not construction
        Planner             _plan;
        RtuFramer           _framer;

        /** @brief The request being sent. Also the scratch the payload is
         *  collected into, since the two never overlap in time. */
        uint8_t             _frame[ADU_MAX];
        uint16_t            _frame_len = 0;
        Expect              _expect = Expect();

        /** @brief Which block is in flight, or NO_BLOCK. */
        uint16_t            _active = NO_BLOCK;

        /** @brief One device's health. Populated from the plan at bring-up, so
         *  there is no entry for a unit the plan never talks to. */
        struct Peer {
            uint8_t  unit     = 0;
            uint8_t  misses   = 0;   ///< consecutive failures; 0 = answering
            uint64_t probe_ms = 0;   ///< do not speak to it before this
        };
        Peer     _peer[Config::MODBUS_MAX_DEVICES];
        uint8_t  _peer_count = 0;
        uint8_t  _dead       = 0;    ///< how many are in backoff right now
        /** @brief The library's field-bus rows, resolved by name once at
         *  bring-up - the same way HealthMonitor drives the platform alarms.
         *  See modbus/rtu/alarms.h. */
        Vars::VarRef _alarm_ref;
        Vars::VarRef _dead_ref;
        uint64_t            _since_ms = 0;     ///< when the current state was entered

        /** @name Counters
         *  What the shell and the comms alarms will read. Kept apart because
         *  they mean different things to whoever is holding a multimeter.
         *  @{ */
        uint32_t _txns      = 0;   ///< transactions that completed
        uint32_t _faults    = 0;   ///< that did not: no answer, bad frame, refusal
        uint32_t _refusals  = 0;   ///< a slave answered, and said no
        uint32_t _tx_short  = 0;   ///< the UART would not take a whole frame
        /** @} */

        enum State : uint8_t {
            MB_IDLE,    ///< nothing in flight; looking for work
            MB_DRAIN,   ///< waiting for it to finish leaving the wire
            MB_RX,      ///< listening for the answer
            MB_GAP      ///< the silence a frame is entitled to before the next
        };
        State _state = MB_IDLE;

        /** @} */

    public:
        /** @brief What Block index means "none". */
        static const uint16_t NO_BLOCK = 0xFFFFu;

        RtuClient(Vars::Bank* io, Logs::Logger* logger = nullptr)
            : Component(logger, "ModbusRTU"), _io(io) {}

        /** @name Operation
         *  @{ */

        /** @brief Drain the receive path and commit anything complete.
         *  PHASE_INPUT, ahead of Bank::expire_stale(). */
        void receive_step(uint64_t now_ms);

        /** @} */

        /** @name Status
         *  @{ */

        bool healthy() const override;
        /** @brief A short phrase for the status bar and the shell. */
        const char* status_str() const;

        const Planner& plan() const { return _plan; }
        uint32_t txns() const      { return _txns; }
        uint32_t faults() const    { return _faults; }
        uint32_t refusals() const  { return _refusals; }
        uint32_t tx_short() const  { return _tx_short; }
        const RtuFramer& framer() const { return _framer; }
        /** @brief Devices in backoff right now, and how many are tracked. */
        uint8_t dead_count() const  { return _dead; }
        uint8_t peer_count() const  { return _peer_count; }
        /** @brief Unit id of tracked device @p i, and whether it is answering.
         *  For the shell: "which one" is the operator's next question. */
        uint8_t peer_unit(uint8_t i) const {
            return (i < _peer_count) ? _peer[i].unit : 0;
        }
        bool peer_dead(uint8_t i) const {
            return (i < _peer_count) && _peer[i].misses > Config::MODBUS_RETRIES;
        }

        /** @} */

    protected:
        /** @brief Bring the port up and plan the bus. */
        void on_setup(uint64_t now_ms) override;
        /** @brief Collect, send, and advance the transaction. PHASE_OUTPUT. */
        void on_step(uint64_t now_ms) override;

    private:
        /** @brief Enter @p s, stamping when. Every transition goes through here,
         *  so a state cannot be changed without its clock being reset. */
        void _enter(State s, uint64_t now_ms);
        /** @brief The block that most wants the bus, or NO_BLOCK.
         *
         *  Writes first: a demand that has changed is worth more than a reading
         *  that is about to be taken again anyway, and a safe state is worth more
         *  than either. */
        uint16_t _pick(uint64_t now_ms) const;
        /** @brief Build the request for @p block into _frame. @return false if it
         *  could not be built, which is a planning defect rather than a wire
         *  event. */
        bool _build(uint16_t block, uint64_t now_ms);
        /** @brief Hand the whole frame to the UART. @return false on a short
         *  write, which fails the transaction. */
        bool _send();
        /** @brief A completed response: commit it, or count the refusal. */
        void _settle(uint64_t now_ms);

        /** @name Device health
         *  @{ */
        /** @brief @p unit's entry, or null when the plan never talks to it. */
        Peer* _peer_of(uint8_t unit);
        const Peer* _peer_of(uint8_t unit) const;
        /** @brief Whether @p unit may be spoken to now. An untracked unit may:
         *  refusing would silently drop a block the planner asked for. */
        bool _due(uint8_t unit, uint64_t now_ms) const;
        /** @brief @p unit answered: clear its miss count. */
        void _answered(uint8_t unit);
        /** @brief @p unit did not: retry once, then back off, doubling. */
        void _missed(uint8_t unit, uint64_t now_ms);
        /** @brief Recount the dead and write the alarm and the status row. */
        void _publish_health();
        /** @} */
    };

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT
#endif // MODBUS_RTU_CLIENT_H
