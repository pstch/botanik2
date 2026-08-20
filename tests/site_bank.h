#ifndef TESTS_SITE_BANK_H
#define TESTS_SITE_BANK_H

/** @file site_bank.h
 *  @brief One way to stand up a bank over the selected site's rows.
 *
 *  Registration order is BotanikFirmware::setup()'s: the library's rows first so
 *  its parameters sit at the front of the parameter block, then the site's. A
 *  test registering a different order tests a table no firmware ever builds.
 */

#include "libbotanik/vars/model.h"
#include "libbotanik/vars/vars.h"

#include BOTANIK_SITE_HEADER            // Site::register_all()

namespace TestSite {

    /** @brief Register the library's rows then the site's, and freeze. The
     *  site's order is register_all()'s, so a test cannot register a table no
     *  firmware ever builds. */
    inline void build(Botanik::Vars::Bank& bank) {
        Botanik::Vars::register_library_vars(bank);
        Site::register_all(bank);
        bank.freeze();
    }

}  // namespace TestSite

#endif // TESTS_SITE_BANK_H
