#ifndef LIBBOTANIK_VARS_VARS_H
#define LIBBOTANIK_VARS_VARS_H

/**
 * @file vars/vars.h
 * @brief The variable rows libbotanik itself declares.
 *
 * A site's table is not only the site's: the library declares rows of its own -
 * alarms it raises, readings it publishes, parameters it reads - and everything
 * addresses a row by one flat index, so the two must read as ONE table.
 *
 * They are not one array. The library's rows live in `vars/vars.cpp` and are
 * handed to the bank by `register_library_vars()`; a site hands over its own the
 * same way. `Bank` runs one index space across every segment in registration
 * order, so nothing outside it knows there is more than one array.
 *
 * It used to be ONE array, spliced together by a macro (`BOTANIK_LIBRARY_VARS`)
 * because C++11 cannot concatenate two constant arrays without a template or a
 * copy into RAM. That macro is why every row in this library had to be written
 * @c ::Botanik::Vars::P() with a backslash on the end of each line - a macro
 * expands into a namespace it cannot see. Registering segments costs one
 * compare in `Bank::desc()` and buys back ordinary C++: these rows are now
 * written with the same unqualified factories a site writes its own with.
 * @note The rationale behind this file is @ref lib_order, @ref lib_regs.
 */

#include <cstdint>

#include "libbotanik/config/net.h"

namespace Botanik { namespace Vars {

    class Bank;

    /** @brief Hand the library's own rows to @p bank.
     *
     *  Called by the bank itself during bring-up, BEFORE a site registers
     *  anything, so the library's parameters sit at the front of the parameter
     *  block. A site never calls this and never names the array: the count stays
     *  inside vars.cpp, where `sizeof` can reach it, so there is no row total
     *  written down in two places. */
    void register_library_vars(Bank& bank);

    /** @brief Pool bytes the library's own text parameters need: eight rows
     *  (net_ip, net_mask, net_gw, net_dns1, net_dns2, ntp_srv1, ntp_srv2,
     *  mqtt_host), each BOTANIK_HOST_STR_LEN. A site sizing Config::STR_POOL_BYTES
     *  needs at least this plus its own text rows. */
    constexpr uint16_t LIBRARY_STR_BYTES = 8u * BOTANIK_HOST_STR_LEN;

}}  // namespace Botanik::Vars

#endif // LIBBOTANIK_VARS_VARS_H
