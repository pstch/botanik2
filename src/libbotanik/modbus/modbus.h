#ifndef MODBUS_MODBUS_H
#define MODBUS_MODBUS_H

/**
 * @file modbus.h
 * @brief The Modbus RTU client, in one include.
 *
 * Three layers, and the split is the same one plc/ makes:
 *
 *  - @ref wire.h    the vocabulary and the frame arithmetic. Header-only
 *                   `constexpr`, no state, no buffers.
 *  - @ref codec.h   building a request and taking a response apart. Loops over
 *                   the caller's buffers, so it has a .cpp, but still no state.
 *  - @ref framing.h the one thing that has to remember where it is: accumulating
 *                   a response and deciding when it is complete.
 *  - @ref plan.h    what the bus costs, and the blocks the variable table turns
 *                   into. Reads the bank; still touches no wire.
 *
 * None of it knows the variable model, the scheduler, or a serial port exists.
 * The driver that joins them to the bank is a later stage; until then this is a
 * self-contained protocol library that a test, or the simulator's slave bank,
 * can drive with nothing but byte arrays.
 *
 * @see docs/design/modbus-budget.md for what the bus costs and the four rules
 *      the client is built to.
 */

#include "wire.h"
#include "codec.h"
#include "framing.h"
#include "plan.h"

#endif // MODBUS_MODBUS_H
