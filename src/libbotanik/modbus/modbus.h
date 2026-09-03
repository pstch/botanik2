#ifndef MODBUS_MODBUS_H
#define MODBUS_MODBUS_H

/**
 * @file modbus.h
 * @brief Modbus, in one include: the shared vocabulary and both transports.
 *
 * @ref modbus/wire.h is the vocabulary both halves agree on (function codes,
 * exceptions, quantity ceilings, frame arithmetic) and @ref modbus/response.h
 * the shared answer-check: the transports differ about FRAMING (RTU is
 * length-driven with a CRC and no header, TCP carries an MBAP header and no
 * checksum) and not about what is inside a frame. `rtu/` is the RS-485 field
 * bus, `tcp/` the plant network, both ends of each:
 *
 *  | class       | flag                   | what it is        |
 *  |-------------|------------------------|-------------------|
 *  | `RtuMaster` | `MODBUS_RTU_CLIENT`    | bus MASTER, polling the field |
 *  | `RtuSlave`  | `MODBUS_RTU_SERVER`    | a device model, answering |
 *  | `TcpSlave`  | `MODBUS_TCP_SERVER`    | the SCADA-facing server |
 *  | `TcpMaster` | `MODBUS_TCP_CLIENT`    | this unit polling another |
 *
 * The classes use the wire documentation's older master/slave spelling and the
 * flags the newer client/server one. Read the middle column as the authority.
 *
 * Everything here is a `step()` state machine because the published libraries
 * are synchronous: one transaction is 21 ms at 19200, a full-length read
 * 163 ms, a dead slave the response timeout - 2, 16 and 50 beats of a 10 ms
 * loop in which nothing else would run.
 *
 * @see @ref dev_modbus - what the bus costs, how a block is planned, and the
 *      register map the TCP slave publishes.
 */

#include "libbotanik/modbus/wire.h"

#include "libbotanik/modbus/response.h"

#include "libbotanik/modbus/rtu/master.h"
#include "libbotanik/modbus/rtu/codec.h"
#include "libbotanik/modbus/rtu/device.h"
#include "libbotanik/modbus/rtu/framing.h"
#include "libbotanik/modbus/rtu/image.h"
#include "libbotanik/modbus/rtu/plan.h"
#include "libbotanik/modbus/rtu/slave.h"

#include "libbotanik/modbus/tcp/map.h"
#include "libbotanik/modbus/tcp/master.h"
#include "libbotanik/modbus/tcp/pdu.h"
#include "libbotanik/modbus/tcp/slave.h"

#endif // MODBUS_MODBUS_H
