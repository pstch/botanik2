#ifndef MODBUS_RTU_CLIENT_H
#define MODBUS_RTU_CLIENT_H

/**
 * @file modbus/rtu/client.h
 * @brief The RTU master: one transaction in flight, driven from the beat.
 *
 * @section cl_split Why it is stepped twice
 *
 * on_input() runs at the top of the beat, so a control program sees the
 * freshest image; on_output() runs after the programs have executed, so a demand
 * written this beat reaches the wire without waiting for the next one. Stepping
 * it once would cost a beat of latency in one direction or the other.
 *
 * @section cl_gate Collect is not gated with the control phase
 *
 * on_output() runs whatever control authority says. Withholding control drops
 * every output to its safe state and stops the scan, but those safe values sit
 * in the image until a transaction carries them out: gating the collector too
 * would leave the actuators energised at exactly the moment control was
 * withdrawn.
 *
 * @section cl_rank What goes out next
 *
 * ONE clock. Every block is ranked on how far it is through the lateness it is
 * allowed, in permille, and the highest wins. Reads and writes differ only in
 * how that pair is DERIVED (`_slot_of()`: a poll period on one side, the minimum
 * write interval and MODBUS_WRITE_DEADLINE_MS on the other), never in how they
 * are compared.
 *
 * Relative and not absolute, because ranked by milliseconds the fast rungs hold
 * the bus for ever: they re-arm constantly while a slow block's lateness only
 * creeps up. There is no tier ladder, and the per-block starvation floor is not
 * a jump between tiers but simply the point where urgency passes 1000.
 *
 * The one absolute pre-empt sits AHEAD of the scale rather than on it: a dirty
 * write while a safe state is owed. A scale has no top, so a late enough block
 * could otherwise climb into any rank reserved above it.
 *
 * @section cl_dead A silent device must not take the bus down
 *
 * An unplugged device costs a full response timeout, and left alone that is paid
 * on EVERY sweep until the live devices lose their rates to the dead ones (the
 * arithmetic is in @ref dev_modbus). Three things stop it, and the third is the
 * one that matters at scale:
 *
 *  - Each device carries its own health: consecutive failures, and the earliest
 *    time it may be spoken to again. One retry, then backoff doubling from
 *    MODBUS_BACKOFF_MS to MODBUS_BACKOFF_MAX_MS.
 *  - A miss does not push a read's retry out by a whole poll period
 *    (MODBUS_RETRY_DELAY_MS caps it), so how long a device takes to be declared
 *    dead is a property of the fault and not of the rung it happens to sit on.
 *  - At most ONE device that is not answering is offered the bus at a time (a
 *    cursor, round-robin), and not until MODBUS_PROBE_SPACING_MS has passed
 *    since the last transaction that failed (a gate). Their share of the bus is
 *    therefore timeout/(timeout + spacing), INDEPENDENT of how many are down.
 *
 * Without the interleave a probe is enormously late against its own period and
 * wins every comparison it enters, so the least valuable and most expensive
 * transaction on the bus goes first, and N dead devices arrive as one contiguous
 * burst of response timeouts every backoff interval.
 *
 * The bound is on the STEADY STATE. A device that has never missed is presumed
 * good, so its first failure cannot be gated - there is no way to know a device
 * is down without trying it - and discovering N of them costs N timeouts once.
 *
 * A REFUSAL clears the device's count: an exception means it heard us,
 * understood us and declined, so it is alive and its register map is the
 * problem. The BLOCK backs off instead, doubling to MODBUS_REFUSAL_MAX_MS, since
 * a refused write stays dirty and nothing else would ever slow the retry down.
 *
 * @section cl_frame A frame goes out whole or not at all
 *
 * The driver enable is hardware-timed: the USART drops DE when its shift
 * register empties, so a frame handed to the UART in pieces would have DE fall
 * mid-frame and every slave on the bus would see a truncated frame - which reads
 * as a wiring fault, not a firmware one. A `static_assert` in client.cpp proves
 * a full FC16 fits the transmit ring (MODBUS_TX_RING_BYTES), and on_output() will
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

namespace Botanik { namespace Tasks { struct Tick; } }

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

        /** @name The probe interleave
         *
         *  One device that is not answering is offered the bus at a time, and
         *  not until MODBUS_PROBE_SPACING_MS of live traffic has been owed since
         *  the last transaction that failed. Together those bound what devices
         *  that are not answering can cost at timeout/(timeout + spacing),
         *  INDEPENDENT of how many of them there are - which is the whole point,
         *  since twelve dead sensors used to need 7.2 s of bus to be declared
         *  dead and simply took it.
         *
         *  A gate and a cursor here rather than a state in the transaction FSM:
         *  a probe is an ordinary transaction over the same framer, settle path
         *  and gap, and only the SCHEDULING of it differs.
         *  @{ */

        /** @brief No suspect is eligible before this. Set by any transaction
         *  that failed, not only by a probe: the first miss of each device is
         *  what a mass outage is made of, and gating only confirmed-dead ones
         *  would let the whole detection burst through ungated. */
        uint64_t _probe_gate_ms = 0;
        /** @brief Where the round-robin over devices that are not answering
         *  resumes, so the rotation is a guarantee and not a by-product of which
         *  suspect happens to be latest. */
        uint8_t  _probe_cursor  = 0;

        /** @} */
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
        Vars::VarRef _line_ref;
        Vars::VarRef _dead_ref;
        Vars::VarRef _crc_ref;
        Vars::VarRef _stray_ref;

        /** @brief The `rtu_` tuning rows, resolved the same way.
         *
         *  Bound rather than looked up per use: the scoring pass reads two of
         *  them for every block on every beat, and a name lookup is a binary
         *  search over the whole table. Read live off the bound row, so a change
         *  lands on the next transaction with nothing to re-initialise - which
         *  is why the group carries no commit serial.
         *  @see modbus/rtu/params.h */
        Vars::VarRef _p_timeout;
        Vars::VarRef _p_retries;
        Vars::VarRef _p_backoff;
        Vars::VarRef _p_backoff_max;
        Vars::VarRef _p_write_min;
        Vars::VarRef _p_rewrite;

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
        /** @brief What a peer index means "none". */
        static const uint8_t  NO_PEER  = 0xFFu;

        /** @param devices what each unit on the segment will answer; null plans
         *  every unit as generic_model(). Borrowed and flash-resident. */
        RtuClient(const SetupContext& ctx, Vars::Bank* io,
                  const DeviceTable* devices = nullptr)
            : Component(ctx, "ModbusRTU"), _io(io), _devices(devices) {}

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
            // The same threshold the backoff runs on, read the same way: this is
            // what the shell and the health rows report, and an accessor saying
            // a device is dead while the rotation still retries it would be a
            // display disagreeing with the bus.
            return (i < _plan.device_count()) &&
                   _peer[i].misses > _retries();
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

    public:
        /** @brief Two tasks, at opposite ends of the beat: take the field in at
         *  PHASE_INPUT, issue the next request at PHASE_OUTPUT. Two entries, so
         *  each half keeps its own cost on the TASKS page. */
        void register_tasks() override;

    private:
        /** @name Registration handlers
         *  Static MEMBERS: the hooks they call are protected. @{ */
        static void _input_task(Component* c, const Tasks::Tick& t);
        static void _output_task(Component* c, const Tasks::Tick& t);
        /** @} */

    protected:
        /** @brief Bring the port up and plan the bus. */
        void on_setup(uint64_t now_ms) override;
        /** @brief Parse and commit whatever answered (PHASE_INPUT), so the
         *  programs run on this beat's readings. */
        void on_input(uint64_t now_ms) override;
        /** @brief Issue the next request (PHASE_OUTPUT), so a demand written
         *  this beat reaches the wire without waiting for the next. */
        void on_output(uint64_t now_ms) override;

    private:
        /** @brief One `rtu_` row's live value, or @p dflt when the row is absent
         *  (a site without the library splice) or reads invalid.
         *
         *  The Config constant is the fallback as well as the row's factory
         *  value, so a build with no rows behaves exactly as it did before they
         *  existed. */
        uint32_t _tune(Vars::VarRef r, uint32_t dflt) const;

        uint32_t _timeout_ms()     const { return _tune(_p_timeout,     Config::MODBUS_TIMEOUT_MS); }
        uint8_t  _retries()        const { return (uint8_t)_tune(_p_retries, Config::MODBUS_RETRIES); }
        uint32_t _backoff_ms()     const { return _tune(_p_backoff,     Config::MODBUS_BACKOFF_MS); }
        uint32_t _backoff_max_ms() const { return _tune(_p_backoff_max, Config::MODBUS_BACKOFF_MAX_MS); }
        uint32_t _write_min_ms()   const { return _tune(_p_write_min,   Config::MODBUS_WRITE_MIN_MS); }
        uint32_t _rewrite_ms()     const { return _tune(_p_rewrite,     Config::MODBUS_REWRITE_MS); }

        /** @brief Enter @p s, stamping when. Every transition goes through here,
         *  so a state cannot be changed without its clock being reset. */
        void _enter(State s, uint64_t now_ms);
        /** @brief When a block asks for the bus, and how long it may then wait.
         *  The two numbers every block is ranked on. @see _slot_of */
        struct Slot {
            uint64_t due;        ///< not offered the bus before this
            uint32_t deadline;   ///< the lateness it is allowed, as the normaliser
        };
        /** @brief @p b's due time and deadline.
         *
         *  The ONE place reads and writes differ. A read's pair comes from its
         *  poll period; a write's from the minimum interval and the delivery
         *  deadline. Everything downstream compares the same number. */
        Slot _slot_of(const Block& b) const;

        /** @brief Which block goes out next, or NO_BLOCK. One urgency per block
         *  in one pass, highest wins; see the note in the .cpp. */
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
        /** @brief The one device that is not answering which may be offered the
         *  bus now, or NO_PEER.
         *
         *  The first at or after the cursor that has misses against it and is
         *  out of its backoff, wrapping. Healthy peers are skipped rather than
         *  blocking the rotation, which is why the cursor is a starting point
         *  and not an index. */
        uint8_t _next_suspect(uint64_t now_ms) const;

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
