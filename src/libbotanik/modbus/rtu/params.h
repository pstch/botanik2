#ifndef MODBUS_RTU_PARAMS_H
#define MODBUS_RTU_PARAMS_H

/** @file modbus/rtu/params.h
 *  @brief The field bus master's own tuning rows, for a site's variable table.
 *
 *  `RtuClient` reads these BY NAME (the `rtu` group), so the library declares
 *  them rather than leaving every site to spell them correctly. Same rule as the
 *  net_ / ntp_ / mqtt_ / mb_ rows.
 *
 *  @section rtu_why_these Why these six and not the rest
 *
 *  What is here is what an INSTALLATION varies: a cable run, a device mix, a
 *  slave-side failsafe timer somebody else configured. What is not here is
 *  everything the bus itself fixes.
 *
 *  Three kinds stayed constants deliberately:
 *
 *  - **Anything sizing an array** - MODBUS_MAX_ROWS, _MAX_BLOCKS,
 *    _MAX_REGS_PER_BLOCK, _TX_RING_BYTES. There is nothing to reallocate at
 *    runtime, and the last two are load-bearing in a `static_assert` that proves
 *    a full block still fits one UART write.
 *  - **MODBUS_GAP_MS** - the protocol's own 3.5-character silence, not a plant
 *    choice, and `DeviceModel::min_gap_ms` already overrides it per model where
 *    a device really needs more.
 *  - **Anything the PLANNER reads** - MODBUS_BAUD, _TURNAROUND_MS,
 *    _BLOCK_STAGGER_MS. Those feed `Plan::plan()`, which runs once at bring-up,
 *    so moving them would mean re-planning a running bus and resetting every
 *    block's place in the poll ladder. Decided against: the line rate is settled
 *    when the segment is wired, and re-planning to change it is a reboot's worth
 *    of disruption for something a reboot already does. They stay constants.
 *
 *  No commit serial: every row below is read where it is used, on the beat that
 *  needs it, and none has a peer to reconnect or a half-applied state to avoid.
 *  A group serial would be ceremony over a plain read.
 *
 *  Registers 980-985, inside the library's 900-999 reserve: above the sys_ rows
 *  and clear of the alarms at its top.
 */

#include "libbotanik/config/modbus.h"   // the rows' factory defaults
#include "libbotanik/vars/declare.h"    // the P() factory these rows are written with

/** @brief The field-bus master's tuning rows. One group of
 *  BOTANIK_LIBRARY_VARS.
 *
 *  Bands exclude the values that are actively harmful rather than merely
 *  unusual: an interval of 0 would put a write on the bus every beat, and a
 *  timeout under a slave's own turnaround declares a healthy device dead.
 *
 *  Fully qualified, because a site table may be in a namespace that has not
 *  pulled `Botanik::Vars` in; each row carries its own trailing comma, so an
 *  empty group vanishes cleanly. */
#define BOTANIK_MODBUS_RTU_PARAM_VARS                                          \
    /* rtu_: RtuClient reads these by name, so the library declares them */    \
    ::Botanik::Vars::P("rtu_timeout_ms", ::Botanik::Vars::T_U32,               \
        BOTANIK_MODBUS_TIMEOUT_MS, 20, 5000, 0, "ms", 980,                     \
        "how long a slave has to answer",                                      \
        "the bus default; a device model may declare its own"),                \
    ::Botanik::Vars::P("rtu_retries", ::Botanik::Vars::T_U8,                   \
        BOTANIK_MODBUS_RETRIES, 0, 5, 0, "", 981,                              \
        "immediate retries before a device is backed off",                     \
        "each costs a full timeout on a device that is really gone"),          \
    ::Botanik::Vars::P("rtu_backoff_ms", ::Botanik::Vars::T_U32,               \
        BOTANIK_MODBUS_BACKOFF_MS, 100, 60000, 0, "ms", 982,                   \
        "first backoff after a device fails its retries",                      \
        "doubles per further failure, up to rtu_backoff_max_ms"),              \
    ::Botanik::Vars::P("rtu_backoff_max_ms", ::Botanik::Vars::T_U32,           \
        BOTANIK_MODBUS_BACKOFF_MAX_MS, 1000, 600000, 0, "ms", 983,             \
        "longest a backed-off device waits between probes",                    \
        "what decides whether one dead device costs the whole bus"),           \
    ::Botanik::Vars::P("rtu_write_min_ms", ::Botanik::Vars::T_U32,             \
        BOTANIK_MODBUS_WRITE_MIN_MS, 20, 60000, 0, "ms", 984,                  \
        "shortest interval between writes to one block",                       \
        "what stops a chattering demand monopolising the bus"),                \
    ::Botanik::Vars::P("rtu_rewrite_ms", ::Botanik::Vars::T_U32,               \
        BOTANIK_MODBUS_REWRITE_MS, 1000, 600000, 0, "ms", 985,                 \
        "how often an output block is written anyway",                         \
        "MATCH the slave's own failsafe timer; it is watching for this"),

#endif // MODBUS_RTU_PARAMS_H
