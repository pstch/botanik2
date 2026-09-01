#ifndef TESTS_SCAN_FIXTURE_H
#define TESTS_SCAN_FIXTURE_H

/**
 * @file scan_fixture.h
 * @brief What a scan-cycle case needs before it can assert anything: a bank
 *        over the site's real tables, and the beat the firmware drives.
 */

#include <cstdint>
#include <cstring>

#include "libbotanik.h"
#include "site_bank.h"

namespace TestScan {

/** @brief What the firmware does with a program table, in one place: hand
 *  each program the bank and its slot, register it, then per beat carry the
 *  Bank's verdict to BOTH halves of the authority edge and dispatch
 *  PHASE_CONTROL through Registry::set_programs_enabled, which is the shipping
 *  call, so this drives the real code. */
struct Cycle {
    Botanik::Tasks::Registry&     reg;
    Botanik::Plc::Program* const* progs;
    uint8_t                       n;
    Botanik::Vars::Bank*          io;

    Cycle(Botanik::Plc::Program* const* p, uint8_t count,
          Botanik::Vars::Bank* bank, Botanik::Tasks::Registry& r)
        : reg(r), progs(p), n(count), io(bank) {
        for (uint8_t i = 0; i < n; i++) {
            if (progs == nullptr || progs[i] == nullptr) continue;
            progs[i]->set_slot(i);
            progs[i]->register_tasks();
        }
        reg.setup(0);          // bind(), off the beat, as bring-up does
        _late_alarm = bank->find("alarm_scan_late");
    }

    void step(uint64_t t) {
        reg.run_phase(Botanik::Tasks::PHASE_CONTROL, t);
        // ...and what the firmware does after the phase: publish the worst
        // lateness onto the alarm row, whose own dwell decides whether it
        // ever annunciates.
        _late = Botanik::Plc::worst_lateness(reg, &_worst);
        io->var(_late_alarm).set(_late > Botanik::Config::SCAN_LATE_MS);
    }

    /** @brief The worst-late program on the last beat, and who owned it. */
    uint32_t late_ms() const     { return _late; }
    const Botanik::Plc::Program* worst() const { return _worst; }

    /** @brief A writer index back to its program, the lookup the firmware hands
     *  MONITOR (BotanikFirmware::register_pages). Bounds-checked, so a row no
     *  program has written names nobody rather than naming program 0. */
    const Botanik::Plc::Program* program(uint8_t w) const {
        return (progs != nullptr && w < n) ? progs[w] : nullptr;
    }

private:
    Botanik::Vars::VarRef        _late_alarm = { Botanik::Vars::VAR_NONE };
    uint32_t                     _late  = 0;
    const Botanik::Plc::Program* _worst = nullptr;
};

/** @brief A bank over the site's real tables, running. */
struct Live {
    Botanik::Tasks::Registry reg;   ///< what the case's programs register in
    Botanik::Vars::Bank      bank;
    Live() {
        TestSite::build(bank);
        believe_all();
    }

    /** @brief Every `_qlt` companion true.
     *
     *  A companion declares FALSE, which is right in the field: nothing is
     *  believed until something measures it. In a rig it is the wrong start,
     *  because whatever the program under test READS comes from a program the
     *  rig is not running. The program's OWN companions are overwritten on its
     *  first scan, so this cannot hide one it fails to publish - a case
     *  rehearsing a dead instrument clears the one it means. */
    void believe_all() {
        for (uint16_t i = 0; i < bank.count(); i++) {
            const char* n = bank.descriptor(i).name;
            const size_t len = strlen(n);
            if (len > 4 && strcmp(n + len - 4, "_qlt") == 0)
                bank.var(Botanik::Vars::VarRef{ i }).set(true);
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

#endif  // TESTS_SCAN_FIXTURE_H
