#ifndef MODBUS_MODBUS_H
#define MODBUS_MODBUS_H

/**
 * @file modbus.h
 * @brief Modbus, in one include: the shared vocabulary and both transports.
 *
 * @section modbus_layout One protocol, two transports
 *
 * The directory says which is which, because the two halves share a vocabulary
 * and nothing else:
 *
 *  - @ref modbus/wire.h - the function codes, the exceptions, the quantity
 *    ceilings and the frame arithmetic. Header-only `constexpr`, no state, no
 *    buffers, and no idea which transport is carrying it. **Both** halves agree
 *    here or nowhere.
 *  - `rtu/` - the field-bus MASTER: this unit polling meters and relay boards
 *    over RS-485.
 *  - `tcp/` - the plant-network SLAVE: this unit answering a SCADA master over
 *    Ethernet.
 *
 * They are opposite ends of opposite buses, which is why one file cannot serve
 * both: RTU frames are length-driven with a CRC and no header, TCP frames carry
 * an MBAP header and no checksum, and a master validates answers where a slave
 * validates requests.
 *
 * @section modbus_rtu The RTU client (rtu/)
 *
 *  - @ref modbus/rtu/codec.h - the checksum, the builders, the parser. Loops
 *    over the caller's buffers, so it has a .cpp; still no state.
 *  - @ref modbus/rtu/framing.h - the one thing that has to remember where it is:
 *    accumulating a response and deciding when it is complete.
 *  - @ref modbus/rtu/plan.h - what the bus costs, and the blocks the variable
 *    table turns into. Reads the bank; touches no wire.
 *  - @ref modbus/rtu/image.h - a response becomes readings, and demands become a
 *    frame.
 *  - @ref modbus/rtu/client.h - the master itself: one transaction in flight,
 *    driven from the beat. The only part that touches a serial port.
 *
 * @section modbus_tcp The TCP slave (tcp/)
 *
 *  - @ref modbus/tcp/pdu.h - what MBAP adds: framing a request out of a stream,
 *    and framing an answer back into it.
 *  - @ref modbus/tcp/map.h - what an address MEANS: the register map over
 *    Vars::Bank, derived from each row's declared register.
 *  - @ref modbus/tcp/slave.h - the server itself: the listener, the connections,
 *    and the two phases of the beat it answers across.
 *
 * @section modbus_blocking Why none of it calls a library
 *
 * Every published Modbus library for this platform is synchronous: Arduino's
 * wraps libmodbus, whose `modbus_read_registers()` sends, waits and receives
 * inside one call. One transaction is 21 ms at 19200, a full-length read is
 * 163 ms, and a dead slave is the response timeout - 2, 16 and 50 beats of a
 * 10 ms loop in which nothing else runs. That is the whole reason this is
 * written here as `step()` state machines instead.
 *
 * @see docs/design/modbus-budget.md - what the bus costs, and the four rules the
 *      client is built to.
 * @see docs/design/modbus-tcp-server.md - the register map the slave publishes.
 */

#include "libbotanik/modbus/wire.h"

#include "libbotanik/modbus/rtu/client.h"
#include "libbotanik/modbus/rtu/codec.h"
#include "libbotanik/modbus/rtu/framing.h"
#include "libbotanik/modbus/rtu/image.h"
#include "libbotanik/modbus/rtu/plan.h"

#include "libbotanik/modbus/tcp/map.h"
#include "libbotanik/modbus/tcp/pdu.h"
#include "libbotanik/modbus/tcp/slave.h"

#endif // MODBUS_MODBUS_H
