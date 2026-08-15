#ifndef TESTS_SITE_BANK_H
#define TESTS_SITE_BANK_H

/** @file site_bank.h
 *  @brief One way to stand up a bank over the selected site's rows.
 *
 *  The rows reach a bank by registration now, so a test cannot take the site's
 *  table as a value the way `Site::g_site.vars` used to hand it over. This does
 *  what BotanikFirmware::setup() does, in the same order and for the same
 *  reason: the library's rows first, so its parameters sit at the front of the
 *  parameter block, then the site's segments, then freeze.
 *
 *  Sharing it matters more than the four lines it saves - a test that registered
 *  a different order would be testing a table no firmware ever builds.
 */

#include "libbotanik/vars/model.h"
#include "libbotanik/vars/vars.h"

#include BOTANIK_SITE_HEADER            // Site::params / alarms / io_rows

namespace TestSite {

    /** @brief Register the library's rows then the site's, and freeze. */
    inline void build(Botanik::Vars::Bank& bank) {
        Botanik::Vars::register_library_vars(bank);
        bank.register_vars(Site::params,  Site::PARAMS_COUNT);
        bank.register_vars(Site::alarms,  Site::ALARMS_COUNT);
        bank.register_vars(Site::io_rows, Site::IO_ROWS_COUNT);
        bank.freeze();
    }

}  // namespace TestSite

#endif // TESTS_SITE_BANK_H
