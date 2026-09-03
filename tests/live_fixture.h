#ifndef TESTS_LIVE_FIXTURE_H
#define TESTS_LIVE_FIXTURE_H

/**
 * @file live_fixture.h
 * @brief What a case needs before it can assert anything: a bank over the
 *        site's real tables, with every quality companion believed.
 */

#include <cstdint>
#include <cstring>

#include "libbotanik.h"
#include "site_bank.h"

namespace TestScan {

struct Live {
    Botanik::Tasks::Registry reg;   ///< what the case's components register in
    Botanik::Vars::Bank      bank;
    Live() {
        TestSite::build(bank);
        believe_all();
    }

    /** @brief Every DERIVED row's `_qlt` companion at Q_VALID.
     *
     *  A companion declares Q_NO_DATA, which is right in the field: nothing is
     *  believed until something measures it. In a rig it is the wrong start for
     *  a derived row, because whatever the program under test READS comes from
     *  a program the rig is not running. The program's OWN companions are
     *  overwritten on its first scan, so this cannot hide one it fails to
     *  publish - a case rehearsing a dead instrument clears the one it means.
     *
     *  A row a TRANSPORT fills is left alone: its quality is the driver's, and
     *  publish() is what writes it. Believing one here would let a vote count
     *  probes that have never answered. */
    void believe_all() {
        for (uint16_t i = 0; i < bank.count(); i++) {
            const Botanik::Vars::Descriptor& d = bank.descriptor(i);
            if (d.scope != Botanik::Vars::SCOPE_Q) continue;
            char base[48];
            if (!Botanik::Vars::quality_base(d.name, base, sizeof base)) continue;
            const Botanik::Vars::VarRef r = bank.find(base);
            if (r.index == Botanik::Vars::VAR_NONE) continue;
            if (bank.descriptor(r.index).bind.transport != Botanik::Vars::XP_NONE)
                continue;
            bank.var(Botanik::Vars::VarRef{ i }).set_i(Botanik::Vars::Q_VALID);
        }
    }
    Botanik::Vars::VarRef ref(const char* name) { return bank.find(name); }
};

/** @brief The terminal a PI row declares, ATTACHED. The channel and the edge
 *  live in the row, so nothing keeps a second table of them.
 *
 *  `Io::LocalInputs` attaches it at bring-up; a case injecting before it has
 *  ever stepped needs it done first, and attach() is idempotent, so asking
 *  twice for the same edge costs nothing. Here rather than in one rig's
 *  header, because both of them and the simulated field want it. */
inline uint8_t pulse_line(Live& f, const char* row) {
    const Botanik::Vars::Descriptor& d = f.bank.descriptor(f.ref(row).index);
    (void)Botanik::HAL::Pulse::attach(Botanik::Io::pulse_channel(d),
                                      Botanik::Io::pulse_edge(d));
    return Botanik::Io::pulse_channel(d);
}

}  // namespace TestScan

#endif  // TESTS_LIVE_FIXTURE_H
