#ifndef TESTS_SITE_BANK_H
#define TESTS_SITE_BANK_H

/** @file site_bank.h
 *  @brief One way to stand up a bank over the selected site's table.
 *
 *  Off the DESCRIPTOR, which is what BotanikFirmware::setup() hands the bank,
 *  so a test cannot stand up a table no firmware ever builds.
 */

#include "libbotanik/vars/model.h"

#include BOTANIK_SITE_HEADER            // Site::g_site

namespace tests { namespace TestSite {

    inline void build(Botanik::Vars::Bank& bank) {
        bank.init(Site::g_site.rows, Site::g_site.order, Site::g_site.row_count,
                  Site::g_site.param_first, Site::g_site.param_count);
    }

    /** @brief A bank over an AD-HOC table - the handful of rows a unit case
     *  declares. No generated name order and no generated parameter bounds, so
     *  find() walks and the block is located here. */
    inline void build(Botanik::Vars::Bank& bank,
                      const Botanik::Vars::Descriptor* rows, uint16_t n) {
        uint16_t first = 0, count = 0;
        for (uint16_t i = 0; i < n; i++) {
            if (rows[i].kind != Botanik::Vars::K_PARAM) continue;
            if (count == 0) first = i;
            count++;
        }
        bank.init(rows, nullptr, n, first, count);
    }

}}  // namespace tests::TestSite

#endif // TESTS_SITE_BANK_H
