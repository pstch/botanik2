#ifndef IO_PULSE_INPUT_H
#define IO_PULSE_INPUT_H

/**
 * @file pulse_input.h
 * @brief One edge-counted local terminal, as a site declares it.
 *
 * Its own header because both ends need it and neither should reach the other:
 * the site descriptor carries the table, and LocalInputs publishes from it.
 */

#include <cstdint>

#include "libbotanik/hal/platform.h"

namespace Botanik {

    /** @brief A local input counted by an interrupt rather than polled. The
     *  channel here is the one its row declares, and the firmware attaches
     *  what it finds here: naming it twice is what has to agree.
     *  @note @c count is the row the raw EDGE COUNT is published into, or null
     *  for a terminal nothing needs the total of. A counter only C++ can read
     *  is one no ST program can meter with, which is why it is a row rather
     *  than a HAL call at the point of use. */
    struct PulseInput {
        const char*      name;
        uint8_t          channel;
        HAL::Pulse::Edge edge;
        const char*      count;
    };

}  // namespace Botanik

#endif // IO_PULSE_INPUT_H
