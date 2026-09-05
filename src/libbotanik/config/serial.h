#ifndef LIBBOTANIK_CONFIG_SERIAL_H
#define LIBBOTANIK_CONFIG_SERIAL_H

/** @file serial.h
 *  @brief The two UARTs and their ring buffers. */

#include "libbotanik/config/base.h"

// Defined and undefined per file, not in base.h: see base.h.
#define BOTANIK_CFG(type, name) static constexpr type name = BOTANIK_##name

namespace Botanik { namespace Config {

    /** @name Serial interfaces
     *
     *  The two UARTs and the rings behind them. Geometry of what is on the
     *  console wire is further down, under Terminal and console.
     *  @{ */

    // Both ports' rates are Terminal::ConsoleConfig / MonitorConfig, not macros:
    // a swapped terminal needs no firmware build, and the number a wiped store
    // comes back at lives next to the component that runs at it.

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
     *  The OPTA's MONITOR port is USB CDC: unattached, availableForWrite()
     *  always answers 1, and attached, USBCDC::send() blocks per byte - tens of
     *  ms inside a 10 ms beat. 256 is far above steady demand on either port,
     *  so it only ever bounds that worst case. */
#ifndef BOTANIK_SERIAL_TX_PER_STEP
#  define BOTANIK_SERIAL_TX_PER_STEP 256
#endif
    BOTANIK_CFG(uint16_t, SERIAL_TX_PER_STEP);

    /** @} */

} }

#undef BOTANIK_CFG

#endif // LIBBOTANIK_CONFIG_SERIAL_H
