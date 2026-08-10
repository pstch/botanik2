#ifndef MODBUS_RTU_CLIENT_H
#define MODBUS_RTU_CLIENT_H

/**
 * @file modbus/rtu/client.h
 * @brief The RTU master: one transaction in flight, driven from the beat.
 *
 * @section cl_split Why it is stepped twice
 *
 * receive_step() runs at the top of the beat, so a control program sees the
 * freshest image; on_step() runs after the programs have executed, so a demand
 * written this beat reaches the wire without waiting for the next one. Stepping
 * it once would cost a beat of latency in one direction or the other.
 *
 * @section cl_gate Collect is not gated with execute
 *
 * on_step() runs whatever control authority says. disable_control() drops every
 * output to its safe state and inhibits the scan, but those safe values sit in
 * the image until a transaction carries them out: gating the collector too would
 * leave the actuators energised at exactly the moment control was withdrawn.
 *
 * @section cl_dead A silent device must not take the bus down
 *
 * An unplugged device costs a full response timeout, and left alone that is paid
 * on EVERY sweep until the live devices lose their rates to the dead ones (the
 * arithmetic is in docs/design/modbus-budget.md). So each device carries its own
 * health: consecutive failures, and the earliest time it may be spoken to again.
 * One immediate retry, then backoff doubling from MODBUS_BACKOFF_MS to
 * MODBUS_BACKOFF_MAX_MS, and `_pick()` does not offer the bus to a device that
 * is not due. The rest of the plan keeps running at its own rates throughout.
 *
 * A REFUSAL clears the count: an exception response means the device heard us,
 * understood us and declined, so it is alive and its register map is the problem.
 *
 * @section cl_frame A frame goes out whole or not at all
 *
 * The driver enable is hardware-timed: the USART drops DE when its shift
 * register empties, so a frame handed to the UART in pieces would have DE fall
 * mid-frame and every slave on the bus would see a truncated frame - which reads
 * as a wiring fault, not a firmware one. A `static_assert` in client.cpp proves
 * a full FC16 fits the transmit ring (MODBUS_TX_RING_BYTES), and on_step() will
 * not begin a transaction until fieldbus_tx_idle() says the ring has DRAINED,
 * not merely that it has room for one more byte. A refused byte is then
 * impossible, and is logged as a critical if it ever happens anyway.
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
     * A state, an entry stamp, one non-blocking action per beat: nothing may
     * block, so every timeout is a comparison against the stamp, not a sleep.
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

        /** @brief One device's health, indexed by Block::peer.
         *
         *  Parallel to the planner's device set rather than a table of its own:
         *  the planner assigns every block a peer index, so a block that exists
         *  always has an entry. The unit address is not repeated here. */
        struct Peer {
            uint8_t  misses   = 0;   ///< consecutive failures; 0 = answering
            uint64_t probe_ms = 0;   ///< do not speak to it before this
        };
        Peer     _peer[Config::MODBUS_MAX_BLOCKS];
        uint8_t  _dead       = 0;    ///< how many are in backoff right now
        /** @brief Whether control authority was up on the previous beat, so the
         *  withdrawal EDGE can be seen. */
        bool     _control_was = false;
        /** @brief A safe state is owed to the field and has not gone out yet.
         *  While set, a dirty write pre-empts everything and ignores the
         *  ordinary write interval. */
        bool     _urgent = false;

        /** @brief What each unit answers. Borrowed; may be null. */
        const DeviceTable* _devices = nullptr;
        /** @brief Silence owed after the transaction just finished, from the
         *  model that answered it. */
        uint32_t _gap_ms = Config::MODBUS_GAP_MS;

        /** @brief The library's field-bus rows, resolved by name once at
         *  bring-up. @see modbus/rtu/alarms.h */
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
        /** @brief Bytes discarded from the receive path before a frame went out.
         *
         *  Non-zero means something answered after its turn had passed - a slow
         *  device replying past its timeout, or a second master on the segment.
         *  Discarding them is what keeps the next transaction aligned; counting
         *  them is what stops that being silent. */
        uint32_t _stale_rx  = 0;
        /** @} */

        enum State : uint8_t {
            MB_IDLE,    ///< nothing in flight; looking for work
            MB_RX,      ///< listening for the answer
            MB_GAP      ///< the silence a frame is entitled to before the next
        };
        State _state = MB_IDLE;

        /** @} */

    public:
        /** @brief What Block index means "none". */
        static const uint16_t NO_BLOCK = 0xFFFFu;

        /** @param devices what each unit on the segment will answer; null plans
         *  every unit as generic_model(). Borrowed and flash-resident. */
        RtuClient(Vars::Bank* io, Logs::Logger* logger = nullptr,
                  const DeviceTable* devices = nullptr)
            : Component(logger, "ModbusRTU"), _io(io), _devices(devices) {}

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

        /** @brief Which block would go out if a transaction started now, or
         *  NO_BLOCK.
         *
         *  Public for a diagnostic and for tests that pin the priority rules
         *  without a wire. Not const: it also samples control authority, which
         *  is where the safe-state urgency is raised, and that has to happen on
         *  the beat control is withdrawn. */
        uint16_t next_block(uint64_t now_ms) {
            _track_control(now_ms);
            return _pick(now_ms);
        }

        /** @brief Whether a safe state is owed to the field and has not gone out
         *  yet. While true a dirty write pre-empts everything. */
        bool safe_state_pending() const { return _urgent; }

        const Planner& plan() const { return _plan; }
        uint32_t txns() const      { return _txns; }
        uint32_t faults() const    { return _faults; }
        uint32_t refusals() const  { return _refusals; }
        uint32_t tx_short() const  { return _tx_short; }
        /** @brief Bytes dropped before a frame, i.e. answers that arrived too
         *  late to be anyone's. @see _stale_rx */
        uint32_t stale_rx() const  { return _stale_rx; }
        const RtuFramer& framer() const { return _framer; }
        /** @brief Devices in backoff right now, and how many are tracked. */
        uint8_t dead_count() const  { return _dead; }
        uint8_t peer_count() const  { return _plan.device_count(); }
        /** @brief Unit id of tracked device @p i, and whether it is answering.
         *  For the shell: "which one" is the operator's next question. */
        uint8_t peer_unit(uint8_t i) const { return _plan.device_at(i); }
        /** @brief Consecutive failures for tracked device @p i; 0 = answering.
         *  The count behind peer_dead(), for a display that wants to say how
         *  bad it is rather than just that it is bad. */
        uint8_t peer_misses(uint8_t i) const {
            return (i < _plan.device_count()) ? _peer[i].misses : 0;
        }
        /** @brief The device table this client was planned against, so a caller
         *  can name a unit without being handed the site's descriptor. */
        const DeviceTable* devices() const { return _devices; }

        bool peer_dead(uint8_t i) const {
            return (i < _plan.device_count()) &&
                   _peer[i].misses > Config::MODBUS_RETRIES;
        }

        /**
         * @brief Owe every output on tracked device @p peer to the field again.
         *
         * What a device that was declared down and came back gets: it has been
         * through its own power-on defaults meanwhile (a relay board reboots
         * with its coils open while the firmware still believes it commanded
         * them shut), and a level-driven scan never disagrees because the demand
         * has not moved. Values are untouched, only their delivery is owed again.
         */
        void reassert_outputs(uint8_t peer);

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
        /** @brief Which block goes out next, or NO_BLOCK. Scores every block in
         *  one pass and takes the highest; see the tier table in the .cpp. */
        uint16_t _pick(uint64_t now_ms) const;
        /** @brief Watch control authority, and raise the safe-state urgency on
         *  the beat it is withdrawn. */
        void     _track_control(uint64_t now_ms);
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
        /** @brief Whether @p unit may be spoken to now. An untracked unit may:
         *  refusing would silently drop a block the planner asked for. */
        bool _due(const Block& b, uint64_t now_ms) const;
        /** @brief @p unit answered: clear its miss count. */
        void _answered(const Block& b);
        /** @brief @p unit did not: retry once, then back off, doubling. */
        void _missed(const Block& b, uint64_t now_ms);
        /** @brief Recount the dead and write the alarm and the status row. */
        void _publish_health();
        /** @} */
    };

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT
#endif // MODBUS_RTU_CLIENT_H
