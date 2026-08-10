#ifndef LIBBOTANIK_CONFIG_MODBUS_H
#define LIBBOTANIK_CONFIG_MODBUS_H

/** @file modbus.h
 *  @brief The two Modbus transports: the RS-485 field bus this unit masters,
 *         and the TCP slave it serves to a SCADA master.
 *
 *  One concern of the library's configuration. Every value defaults from
 *  an #ifndef-guarded BOTANIK_* macro a site may override; see
 *  libbotanik_config.h, which aggregates these and holds the rules.
 *
 *  The protocol's own fixed numbers (ADU ceilings, quantity limits, frame
 *  arithmetic) are NOT here: an installation does not vary a protocol, so they
 *  live beside the arithmetic that depends on them, in modbus/wire.h. Here is
 *  what a plant varies: line rate, deadlines, and how much the firmware holds. */

#include <cstdint>                        // uint8_t/uint16_t/uint32_t

#include "libbotanik/config/base.h"       // the BOTANIK_FEATURE_* gates

// Defined and undefined per file, not in base.h: see base.h.
#define BOTANIK_CFG(type, name) static constexpr type name = BOTANIK_##name

namespace Botanik { namespace Config {

    /** @name Modbus
     *  @{ */

    /** @brief How many published rows the map can hold. One `uint16_t` each, so
     *  the default costs 512 bytes; a site whose table outgrows it is told at
     *  bring-up (Map::overflow()) rather than quietly serving a short map. */
#ifndef BOTANIK_MODBUS_TCP_MAX_ROWS
#  define BOTANIK_MODBUS_TCP_MAX_ROWS 256
#endif
    BOTANIK_CFG(uint16_t, MODBUS_TCP_MAX_ROWS);

    /** @brief Port the slave listens on when the `mb_port` parameter is absent.
     *  502 is the registered one; the simulator serves it at 10502, since a
     *  process without CAP_NET_BIND_SERVICE cannot bind below 1024. */
#ifndef BOTANIK_MODBUS_TCP_PORT
#  define BOTANIK_MODBUS_TCP_PORT 502
#endif
    BOTANIK_CFG(uint16_t, MODBUS_TCP_PORT);

    /** @brief The unit id this slave answers as. It ACCEPTS any and echoes what
     *  it was sent - a TCP slave that refuses unit 0 or 255 fails against half
     *  the diagnostic tools out there - and counts the ones that do not match
     *  this, which is what makes a misaddressed master visible. */
#ifndef BOTANIK_MODBUS_TCP_UNIT_ID
#  define BOTANIK_MODBUS_TCP_UNIT_ID 1
#endif
    BOTANIK_CFG(uint8_t, MODBUS_TCP_UNIT_ID);

    /** @brief Concurrent masters. Each costs two ADU buffers (~520 bytes), and
     *  two is a SCADA plus somebody's laptop. */
#ifndef BOTANIK_MODBUS_TCP_MAX_CONN
#  define BOTANIK_MODBUS_TCP_MAX_CONN 2
#endif
    BOTANIK_CFG(uint8_t, MODBUS_TCP_MAX_CONN);

    /** @brief A connection with no complete request within this is reaped. What
     *  it defends against is a master that opens sockets and forgets them: the
     *  slots are few, so one leak is half the capacity. */
#ifndef BOTANIK_MODBUS_TCP_IDLE_TIMEOUT_MS
#  define BOTANIK_MODBUS_TCP_IDLE_TIMEOUT_MS 60000
#endif
    BOTANIK_CFG(uint32_t, MODBUS_TCP_IDLE_TIMEOUT_MS);

    /** @brief First coil of the acknowledgement block: acking alarm `reg` is a
     *  write to `MODBUS_TCP_ACK_COIL_BASE + reg`, and the base itself acks the lot.
     *
     *  Its own block rather than a meaning overloaded onto the alarm's own coil:
     *  an alarm is published read-only, and an ack is a write. 0x1000 is well
     *  clear of the row numbers (the library reserves up to 999). */
#ifndef BOTANIK_MODBUS_TCP_ACK_COIL_BASE
#  define BOTANIK_MODBUS_TCP_ACK_COIL_BASE 0x1000
#endif
    BOTANIK_CFG(uint16_t, MODBUS_TCP_ACK_COIL_BASE);

    /** @brief How long the MASTER waits for a connect before starting over.
     *  Short: the peer is a PLC on the same plant network, not a name to
     *  resolve, and a connect that has not completed in a second is not going
     *  to. */
#ifndef BOTANIK_MODBUS_TCP_CONNECT_TIMEOUT_MS
#  define BOTANIK_MODBUS_TCP_CONNECT_TIMEOUT_MS 1000
#endif
    BOTANIK_CFG(uint32_t, MODBUS_TCP_CONNECT_TIMEOUT_MS);

    /** @brief How long the MASTER waits for an answer before giving up on the
     *  connection.
     *
     *  The connection, not just the transaction: a late answer arrives while the
     *  next request is in flight, and the transaction id only says it is stale
     *  after it has been read. Starting clean is cheaper. */
#ifndef BOTANIK_MODBUS_TCP_RESPONSE_TIMEOUT_MS
#  define BOTANIK_MODBUS_TCP_RESPONSE_TIMEOUT_MS 1000
#endif
    BOTANIK_CFG(uint32_t, MODBUS_TCP_RESPONSE_TIMEOUT_MS);

    /** @brief Connections accepted per beat. One: accepting is cheap, but the
     *  beat is the budget and a burst of them belongs spread over beats. */
#ifndef BOTANIK_MODBUS_TCP_ACCEPT_PER_STEP
#  define BOTANIK_MODBUS_TCP_ACCEPT_PER_STEP 1
#endif
    BOTANIK_CFG(uint8_t, MODBUS_TCP_ACCEPT_PER_STEP);

    /** @brief Bus line rate.
     *
     *  19200 carries the reference plant at about a fifth of the bus and
     *  survives long greenhouse cable runs; 115200 returns barely 2x on paper,
     *  less once the beat quantises each transaction, for real signal-integrity
     *  risk. 38400 is the first step up. See docs/design/modbus-budget.md. */
#ifndef BOTANIK_MODBUS_BAUD
#  define BOTANIK_MODBUS_BAUD 19200
#endif
    BOTANIK_CFG(uint32_t, MODBUS_BAUD);

    /** @brief How long a slave is assumed to think before it answers.
     *
     *  Not a timeout: this is the figure the planner COSTS a transaction with,
     *  and it decides how wide a gap is worth reading across. It dominates
     *  everything - the link speed has no effect on it at all - so it is worth
     *  measuring on a real device rather than trusting. 10 ms is fair for a
     *  modern RS-485 meter; cheap ones are 20-50. */
#ifndef BOTANIK_MODBUS_TURNAROUND_MS
#  define BOTANIK_MODBUS_TURNAROUND_MS 10
#endif
    BOTANIK_CFG(uint32_t, MODBUS_TURNAROUND_MS);

    /** @brief Most XP_MODBUS rows the planner will hold. Sized above the
     *  reference site's 70; rows past it are dropped and counted, never
     *  silently ignored. */
#ifndef BOTANIK_MODBUS_MAX_ROWS
#  define BOTANIK_MODBUS_MAX_ROWS 128
#endif
    BOTANIK_CFG(uint16_t, MODBUS_MAX_ROWS);

    /** @brief Most blocks one plan may contain.
     *
     *  A backstop, not the real bound: the poll ladder already limits the count
     *  to devices x function codes x rungs. If this ever binds, the table has
     *  scattered registers rather than the plan being too ambitious. */
#ifndef BOTANIK_MODBUS_MAX_BLOCKS
#  define BOTANIK_MODBUS_MAX_BLOCKS 32
#endif
    BOTANIK_CFG(uint16_t, MODBUS_MAX_BLOCKS);

    /** @brief Most registers one block may span.
     *
     *  Under the protocol's own 125, so that an FC16 of a full block stays
     *  inside the 255 bytes the UART can hold in one go: the driver enable is
     *  hardware-timed and drops when the shift register empties, so a frame that
     *  does not fit in the buffer would be cut in half on the wire. Coils are
     *  not capped here - eight to a byte, their own ceiling binds first. */
#ifndef BOTANIK_MODBUS_MAX_REGS_PER_BLOCK
#  define BOTANIK_MODBUS_MAX_REGS_PER_BLOCK 120
#endif
    BOTANIK_CFG(uint16_t, MODBUS_MAX_REGS_PER_BLOCK);

    /** @brief How often to try binding the slave's port again after a failure.
     *
     *  A port already taken is usually a configuration mistake. Giving up for
     *  good would leave the slave silently absent while healthy() said yes, so
     *  it is retried slowly: whatever holds the port may let go. */
#ifndef BOTANIK_MODBUS_TCP_BIND_RETRY_MS
#  define BOTANIK_MODBUS_TCP_BIND_RETRY_MS 30000
#endif
    BOTANIK_CFG(uint32_t, MODBUS_TCP_BIND_RETRY_MS);

    /** @brief How many parameter commits may wait to reach flash.
     *
     *  A full FC16 covers 61 rows, so this holds one at least: a legitimate
     *  bulk download must not be refused for want of room. Past it the slave
     *  answers EX_SLAVE_BUSY, which a master already knows how to retry. */
#ifndef BOTANIK_MODBUS_PERSIST_QUEUE
#  define BOTANIK_MODBUS_PERSIST_QUEUE 64
#endif
    BOTANIK_CFG(uint16_t, MODBUS_PERSIST_QUEUE);

    /** @brief How many of those may actually be written per beat.
     *
     *  Storage is synchronous (docs/dev/concurrency.md): a TDBStore write is a
     *  record append plus a page program, and a GC pass adds a sector erase -
     *  milliseconds each, hundreds in the worst case. 61 of them in one beat
     *  would let a remote master stall the field bus past its own response
     *  deadline, so they are paced, as SERIAL_TX_PER_STEP paces the console. */
#ifndef BOTANIK_MODBUS_PERSIST_PER_STEP
#  define BOTANIK_MODBUS_PERSIST_PER_STEP 2
#endif
    BOTANIK_CFG(uint16_t, MODBUS_PERSIST_PER_STEP);

    /** @brief How many bytes the field-bus UART's transmit ring holds.
     *
     *  Not a tunable: it MIRRORS the platform's own buffer, and exists so the
     *  client can prove at build time that a full frame fits in one go rather
     *  than assuming it in a comment. On OPTA that buffer is Mbed's
     *  MBED_CONF_DRIVERS_UART_SERIAL_TXBUF_SIZE, 256 by default; the simulator's
     *  socketpair holds far more. Lower this and the static_assert in
     *  modbus/rtu/client.cpp names the block size that stops fitting. */
#ifndef BOTANIK_MODBUS_TX_RING_BYTES
#  define BOTANIK_MODBUS_TX_RING_BYTES 256
#endif
    BOTANIK_CFG(uint16_t, MODBUS_TX_RING_BYTES);

    /** @brief Step between the due times of blocks sharing a rung, so a ladder
     *  full of one-second groups does not fire them all on the same beat. Same
     *  rule the task registry and the scan cycle use. */
#ifndef BOTANIK_MODBUS_BLOCK_STAGGER_MS
#  define BOTANIK_MODBUS_BLOCK_STAGGER_MS 20
#endif
    BOTANIK_CFG(uint32_t, MODBUS_BLOCK_STAGGER_MS);

    /** @brief How long a slave has to answer before the transaction is lost.
     *
     *  Worst turnaround plus the response time, which is about 300 ms at 19200.
     *  Tight on purpose - libraries default to a second - and modbus/rtu/client.h
     *  (@c cl_dead) is where what that costs a bus is worked out. */
#ifndef BOTANIK_MODBUS_TIMEOUT_MS
#  define BOTANIK_MODBUS_TIMEOUT_MS 300
#endif
    BOTANIK_CFG(uint32_t, MODBUS_TIMEOUT_MS);

    /** @brief Shortest interval between two writes to the same block, so a
     *  chattering demand cannot monopolise the bus. */
#ifndef BOTANIK_MODBUS_WRITE_MIN_MS
#  define BOTANIK_MODBUS_WRITE_MIN_MS 200
#endif
    BOTANIK_CFG(uint32_t, MODBUS_WRITE_MIN_MS);

    /** @brief How often an output block is written even with nothing to say.
     *
     *  Two jobs: it re-drives a slave that rebooted and came back at its power-on
     *  defaults, and it is the keepalive that slave's own failsafe timer watches
     *  for. Configure the slave-side failsafe as well - it is the only mechanism
     *  that still works when the master is the thing that failed. */
#ifndef BOTANIK_MODBUS_REWRITE_MS
#  define BOTANIK_MODBUS_REWRITE_MS 5000
#endif
    BOTANIK_CFG(uint32_t, MODBUS_REWRITE_MS);

    /** @brief Immediate retries before a device is backed off.
     *
     *  One. A lost frame on a long RS-485 run is weather - retrying at once
     *  costs a single transaction and usually works. Retrying a device that is
     *  actually unplugged costs a full response timeout every sweep, which is
     *  what takes a bus down, so the second failure is where patience ends. */
#ifndef BOTANIK_MODBUS_RETRIES
#  define BOTANIK_MODBUS_RETRIES 1
#endif
    BOTANIK_CFG(uint8_t, MODBUS_RETRIES);

    /** @brief First backoff after a device has failed its retry; each further
     *  failure doubles it, up to MODBUS_BACKOFF_MAX_MS.
     *
     *  The number that decides whether one dead device takes the whole bus with
     *  it; modbus/rtu/client.h (@c cl_dead) does the arithmetic. */
#ifndef BOTANIK_MODBUS_BACKOFF_MS
#  define BOTANIK_MODBUS_BACKOFF_MS 1000
#endif
    BOTANIK_CFG(uint32_t, MODBUS_BACKOFF_MS);

    /** @brief Longest a backed-off device waits between probes. 30 s: long
     *  enough that a dead trunk costs nothing, short enough that plugging a
     *  device back in is noticed while the engineer is still standing there. */
#ifndef BOTANIK_MODBUS_BACKOFF_MAX_MS
#  define BOTANIK_MODBUS_BACKOFF_MAX_MS 30000
#endif
    BOTANIK_CFG(uint32_t, MODBUS_BACKOFF_MAX_MS);

    /** @brief Registers and coils a simulated field device holds.
     *
     *  Sized for the reference plant's meters and not for the protocol's 65536:
     *  these are RAM, one image per device, and only ever instantiated by the
     *  simulator.
     *
     *  512 covers the SDM630's published map, which runs to 0x018E (its THD and
     *  line-to-line blocks live in the 200s and 300s). Below that the simulated
     *  device answers exception 02 to a read the real one serves. */
#ifndef BOTANIK_MODBUS_SIM_REGS
#  define BOTANIK_MODBUS_SIM_REGS 512
#endif
    BOTANIK_CFG(uint16_t, MODBUS_SIM_REGS);

#ifndef BOTANIK_MODBUS_SIM_COILS
#  define BOTANIK_MODBUS_SIM_COILS 64
#endif
    BOTANIK_CFG(uint16_t, MODBUS_SIM_COILS);

    /** @brief Silence held between one transaction and the next.
     *
     *  The protocol asks for 3.5 characters, which is 2 ms at 19200 and less
     *  above it - under a single beat either way, so this is really only honest
     *  bookkeeping. It stops being free if the beat ever shortens. */
#ifndef BOTANIK_MODBUS_GAP_MS
#  define BOTANIK_MODBUS_GAP_MS 3
#endif
    BOTANIK_CFG(uint32_t, MODBUS_GAP_MS);

    /** @} */

}}  // namespace Botanik::Config

#undef BOTANIK_CFG

#endif // LIBBOTANIK_CONFIG_MODBUS_H
