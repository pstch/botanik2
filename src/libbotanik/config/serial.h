#ifndef LIBBOTANIK_CONFIG_SERIAL_H
#define LIBBOTANIK_CONFIG_SERIAL_H

/** @file serial.h
 *  @brief The two UARTs and their ring buffers.
 *
 *  One concern of the library's configuration. Every value defaults from
 *  an #ifndef-guarded BOTANIK_* macro a site may override; see
 *  libbotanik_config.h, which aggregates these and holds the rules. */

#include "libbotanik/config/base.h"       // the BOTANIK_FEATURE_* gates

// Defined and undefined per file, not in base.h: see base.h.
#define BOTANIK_CFG(type, name) static constexpr type name = BOTANIK_##name

namespace Botanik { namespace Config {

    /** @name Serial interfaces
     *
     *  The two UARTs and the rings behind them. Geometry of what is on the
     *  console wire is further down, under Terminal and console.
     *  @{ */

    /** @brief The FACTORY DEFAULT for the `serial_console_baud` parameter.
     *
     *  The live value is the parameter, so a swapped terminal needs no firmware
     *  build: `params set serial_console_baud 9600` retunes the UART and the
     *  per-render TX budget together on the next beat. This default is what a site pins for
     *  its hardware, and what a wiped store comes back at. */
#ifndef BOTANIK_CONSOLE_BAUD
#  define BOTANIK_CONSOLE_BAUD 4800
#endif
    BOTANIK_CFG(uint32_t, CONSOLE_BAUD);

    /** @brief The factory default for the `serial_monitor_baud` parameter.
     *  @note Effective on the simulator, which throttles its write to it. The
     *        OPTA's MONITOR endpoint is USB CDC and has no line rate, so
     *        begin() there ignores it. */
#ifndef BOTANIK_MONITOR_BAUD
#  define BOTANIK_MONITOR_BAUD 115200
#endif
    BOTANIK_CFG(uint32_t, MONITOR_BAUD);

    /** @brief BufferedSerial ring sizes, per port. Both must be a power of two
     *  (BufferedSerial static_asserts it); usable depth is size-1, so the TX
     *  ring below carries 4095 bytes - the figure SERIAL_TX_PER_STEP's note
     *  quotes. Costs RX+TX bytes of RAM for every port a build opens. */
#ifndef BOTANIK_SERIAL_RX_BUF
#  define BOTANIK_SERIAL_RX_BUF 256
#endif
    BOTANIK_CFG(uint16_t, SERIAL_RX_BUF);
#ifndef BOTANIK_SERIAL_TX_BUF
#  define BOTANIK_SERIAL_TX_BUF 4096
#endif
    BOTANIK_CFG(uint16_t, SERIAL_TX_BUF);

    /** @brief Most bytes BufferedSerial::transmit() hands the driver in one
     *  activation. 0 disables the cap (drain until the ring empties).
     *
     *  A bound, not a throttle. Draining until the ring empties is fine for a
     *  UART, whose availableForWrite() reflects a real FIFO, but the OPTA's
     *  MONITOR port is USB CDC: with no host attached it always answers 1, so
     *  one activation would push the whole 4095-byte ring, and with a host
     *  attached USBCDC::send() blocks per byte - tens of milliseconds inside a
     *  10 ms beat.
     *
     *  256 is far above steady demand on either port (the console's TX ring is
     *  self-capped near 61 bytes by the terminal's depth floor, a log line is
     *  ~90), so it only ever bounds the worst case. */
#ifndef BOTANIK_SERIAL_TX_PER_STEP
#  define BOTANIK_SERIAL_TX_PER_STEP 256
#endif
    BOTANIK_CFG(uint16_t, SERIAL_TX_PER_STEP);

    /** @} */

} }

#undef BOTANIK_CFG

#endif // LIBBOTANIK_CONFIG_SERIAL_H
