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
 *
 * @section lib_order Order
 *
 * `Bank` requires the `ROLE_PARAM` rows to be **one contiguous run** - that is
 * what lets the PARAMS page and the shell address them by a dense 0-based index.
 * The library registers first and its parameters come last in its array, so a
 * site's parameter segment abuts them. `Bank::freeze()` checks it and names the
 * row that broke it.
 *
 * @section lib_regs Register space
 *
 * A parameter, status value or alarm carries a hand-assigned Modbus TCP slave
 * register (`Binding::reg`), and an external master's map is a contract. The
 * library reserves **900-999**: parameters and status values grow upward from
 * 900, its alarms take 990-999, and a site's own alarms live in 800-899.
 * `tests/test_site_names.cpp` fails on a collision and on a row outside the
 * block its scope belongs to.
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
