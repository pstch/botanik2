#ifndef LIBBOTANIK_CONFIG_MODBUS_H
#define LIBBOTANIK_CONFIG_MODBUS_H

/** @file modbus.h
 *  @brief The two Modbus transports: the RS-485 field bus this unit masters,
 *         and the TCP slave it serves to a SCADA master.
 *  @note Only what a PLANT varies - line rate, deadlines, capacities. The
 *        protocol's own fixed numbers are in modbus/wire.h. */

#include <cstdint>

#include "libbotanik/config/base.h"

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


    /** @brief The unit id this slave answers as. It ACCEPTS any and echoes what
     *  it was sent, counting the mismatches: refusing unit 0 or 255 fails
     *  against half the diagnostic tools out there. */
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

    /** @brief First coil of the acknowledgement block: acking alarm `reg` writes
     *  `MODBUS_TCP_ACK_COIL_BASE + reg`, and the base itself acks the lot. Its
     *  own block because an alarm is published read-only and an ack is a write;
     *  0x1000 clears the row numbers, which reach 999. */
#ifndef BOTANIK_MODBUS_TCP_ACK_COIL_BASE
#  define BOTANIK_MODBUS_TCP_ACK_COIL_BASE 0x1000
#endif
    BOTANIK_CFG(uint16_t, MODBUS_TCP_ACK_COIL_BASE);

    /** @brief How long the MASTER waits for a connect before starting over.
     *  Short: the peer is a PLC on the same plant network. */
#ifndef BOTANIK_MODBUS_TCP_CONNECT_TIMEOUT_MS
#  define BOTANIK_MODBUS_TCP_CONNECT_TIMEOUT_MS 1000
#endif
    BOTANIK_CFG(uint32_t, MODBUS_TCP_CONNECT_TIMEOUT_MS);

    /** @brief How long the MASTER waits for an answer before dropping the
     *  CONNECTION, not just the transaction: a late answer arrives while the next
     *  request is in flight, and starting clean is cheaper. */
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

    /** @brief Bus line rate. 19200 carries the reference plant at about a fifth
     *  of the bus and survives long greenhouse runs; 115200 returns barely 2x for
     *  real signal-integrity risk. 38400 is the first step up. @ref dev_modbus */
#ifndef BOTANIK_MODBUS_BAUD
#  define BOTANIK_MODBUS_BAUD 19200
#endif
    BOTANIK_CFG(uint32_t, MODBUS_BAUD);

    /** @brief How long a slave is assumed to think before it answers. NOT a
     *  timeout: the figure the planner costs a transaction with, so it decides
     *  how wide a gap is worth reading across. It dominates everything - link
     *  speed does not affect it - so measure it. 10 ms for a modern meter,
     *  20-50 for a cheap one. */
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

    /** @brief Most blocks one plan may contain. A backstop: the poll ladder
     *  already bounds the count. If this binds, the table has scattered
     *  registers. */
#ifndef BOTANIK_MODBUS_MAX_BLOCKS
#  define BOTANIK_MODBUS_MAX_BLOCKS 32
#endif
    BOTANIK_CFG(uint16_t, MODBUS_MAX_BLOCKS);

    /** @brief Most registers one block may span. Under the protocol's own 125,
     *  so an FC16 of a full block fits the 255 bytes the UART holds in one go:
     *  the driver enable is hardware-timed and drops when the shift register
     *  empties, cutting an oversized frame in half on the wire. */
#ifndef BOTANIK_MODBUS_MAX_REGS_PER_BLOCK
#  define BOTANIK_MODBUS_MAX_REGS_PER_BLOCK 120
#endif
    BOTANIK_CFG(uint16_t, MODBUS_MAX_REGS_PER_BLOCK);

    /** @brief How often to try binding the slave's port again after a failure.
     *  Retried slowly rather than abandoned, which would leave the slave
     *  silently absent while healthy() said yes. */
#ifndef BOTANIK_MODBUS_TCP_BIND_RETRY_MS
#  define BOTANIK_MODBUS_TCP_BIND_RETRY_MS 30000
#endif
    BOTANIK_CFG(uint32_t, MODBUS_TCP_BIND_RETRY_MS);

    /** @brief How many parameter commits may wait to reach flash. A full FC16
     *  covers 61 rows, so this holds at least one: a legitimate bulk download
     *  must not be refused. Past it the slave answers EX_SLAVE_BUSY. */
#ifndef BOTANIK_MODBUS_PERSIST_QUEUE
#  define BOTANIK_MODBUS_PERSIST_QUEUE 64
#endif
    BOTANIK_CFG(uint16_t, MODBUS_PERSIST_QUEUE);

    /** @brief How many of those may actually be written per beat. Storage is
     *  synchronous and a TDBStore write costs milliseconds, so 61 in one beat
     *  would let a remote master stall the field bus past its own response
     *  deadline. @see docs/dev/concurrency.md */
#ifndef BOTANIK_MODBUS_PERSIST_PER_STEP
#  define BOTANIK_MODBUS_PERSIST_PER_STEP 2
#endif
    BOTANIK_CFG(uint16_t, MODBUS_PERSIST_PER_STEP);

    /** @brief How many bytes the field-bus UART's transmit ring holds. NOT a
     *  tunable: it mirrors the platform's own buffer so the client can prove at
     *  build time that a full frame fits in one go. Lower it and the
     *  static_assert in modbus/rtu/client.cpp names what stops fitting. */
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
     *  Worst turnaround plus response time, about 300 ms at 19200 - tight on
     *  purpose, libraries defaulting to a second. @see @c cl_dead */
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

    /** @brief How often an output block is written even with nothing to say: it
     *  re-drives a slave that rebooted to its power-on defaults, and is the
     *  keepalive that slave's failsafe timer watches for.
     *  @note Configure the slave-side failsafe too - it is the only mechanism
     *  that still works when the MASTER is what failed. */
#ifndef BOTANIK_MODBUS_REWRITE_MS
#  define BOTANIK_MODBUS_REWRITE_MS 5000
#endif
    BOTANIK_CFG(uint32_t, MODBUS_REWRITE_MS);

    /** @brief How long a demand that has moved may sit behind reads. NOT a
     *  throttle: it is the DENOMINATOR a write block's lateness is measured
     *  against, so shorter means higher priority against the read rungs, not a
     *  faster retry.
     *  @note The one value here with a PLANT consequence rather than a bus one:
     *  at 500 ms a demand reaches the field inside half a second. */
#ifndef BOTANIK_MODBUS_WRITE_DEADLINE_MS
#  define BOTANIK_MODBUS_WRITE_DEADLINE_MS 500
#endif
    BOTANIK_CFG(uint32_t, MODBUS_WRITE_DEADLINE_MS);

    /** @brief Longest a write block waits after being refused. An exception is
     *  not silence, so only this block backs off, doubling from
     *  MODBUS_WRITE_MIN_MS. Without the penalty a mismatched register map costs
     *  about an eighth of the bus for the life of the boot. */
#ifndef BOTANIK_MODBUS_REFUSAL_MAX_MS
#  define BOTANIK_MODBUS_REFUSAL_MAX_MS 60000
#endif
    BOTANIK_CFG(uint32_t, MODBUS_REFUSAL_MAX_MS);

    /** @brief Immediate retries before a device is backed off. One: a lost frame
     *  on a long run is weather and retrying costs one transaction, but retrying
     *  an unplugged device costs a full response timeout every sweep. */
#ifndef BOTANIK_MODBUS_RETRIES
#  define BOTANIK_MODBUS_RETRIES 1
#endif
    BOTANIK_CFG(uint8_t, MODBUS_RETRIES);

    /** @brief First backoff after a device has failed its retry; each further
     *  failure doubles it, up to MODBUS_BACKOFF_MAX_MS. Decides whether one dead
     *  device takes the bus with it. @see @c cl_dead */
#ifndef BOTANIK_MODBUS_BACKOFF_MS
#  define BOTANIK_MODBUS_BACKOFF_MS 1000
#endif
    BOTANIK_CFG(uint32_t, MODBUS_BACKOFF_MS);

    /** @brief Longest a backed-off device waits between probes. A device that is
     *  down is a work order, and the probe is bounded by MODBUS_PROBE_SPACING_MS
     *  anyway, so a longer cap only slows rediscovery. */
#ifndef BOTANIK_MODBUS_BACKOFF_MAX_MS
#  define BOTANIK_MODBUS_BACKOFF_MAX_MS 60000
#endif
    BOTANIK_CFG(uint32_t, MODBUS_BACKOFF_MAX_MS);

    /** @brief Live traffic owed between one unanswered transaction and the next.
     *  The whole bound on what non-answering devices may cost: at most one is
     *  ever offered the bus, so their share is
     *  `timeout / (timeout + spacing) = 300 / 1800 = 17%`, independent of how
     *  many are down.
     *  @note In TIME, not transactions: a probe costs a full response timeout
     *  and a live read 22 ms, so "one probe in four" would be 77% of the bus. */
#ifndef BOTANIK_MODBUS_PROBE_SPACING_MS
#  define BOTANIK_MODBUS_PROBE_SPACING_MS 1500
#endif
    BOTANIK_CFG(uint32_t, MODBUS_PROBE_SPACING_MS);

    /** @brief Longest a block waits before its retry after a missed answer.
     *  Uncapped, a read's retry comes round on its own poll period: two minutes
     *  to declare a device dead on the 60 s rung. Capping makes detection
     *  latency a property of the fault rather than of the rung.
     *  @warning Safe only because MODBUS_PROBE_SPACING_MS bounds the cost. */
#ifndef BOTANIK_MODBUS_RETRY_DELAY_MS
#  define BOTANIK_MODBUS_RETRY_DELAY_MS 500
#endif
    BOTANIK_CFG(uint32_t, MODBUS_RETRY_DELAY_MS);

    /** @brief Registers and coils a simulated field device holds. RAM, one image
     *  per device, simulator only. 512 covers the SDM630's published map (to
     *  0x018E); below that the simulated device answers exception 02 to a read
     *  the real one serves. */
#ifndef BOTANIK_MODBUS_SIM_REGS
#  define BOTANIK_MODBUS_SIM_REGS 512
#endif
    BOTANIK_CFG(uint16_t, MODBUS_SIM_REGS);

#ifndef BOTANIK_MODBUS_SIM_COILS
#  define BOTANIK_MODBUS_SIM_COILS 64
#endif
    BOTANIK_CFG(uint16_t, MODBUS_SIM_COILS);

    /** @brief Silence held between one transaction and the next. The protocol
     *  asks 3.5 characters, 2 ms at 19200 - under a beat either way, so it is
     *  bookkeeping until the beat shortens. */
#ifndef BOTANIK_MODBUS_GAP_MS
#  define BOTANIK_MODBUS_GAP_MS 3
#endif
    BOTANIK_CFG(uint32_t, MODBUS_GAP_MS);

    /** @} */

}}  // namespace Botanik::Config

#undef BOTANIK_CFG

#endif // LIBBOTANIK_CONFIG_MODBUS_H
