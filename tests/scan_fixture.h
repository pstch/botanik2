#ifndef TESTS_SCAN_FIXTURE_H
#define TESTS_SCAN_FIXTURE_H

/**
 * @file scan_fixture.h
 * @brief What a scan-cycle case needs before it can assert anything: a bank
 *        over the site's real tables, and the beat the firmware drives.
 */

#include <cstdint>
#include "libbotanik/plc/authority.h"

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
        // Idempotent, and the whole edge: the gate, plus on the way back up the
        // timer reseed and the first-scan flag.
        reg.set_programs_enabled(io->control_enabled(), t);
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
    Botanik::Plc::ControlAuthority auth{bank};
    Live() {
        TestSite::build(bank);
        auth.set_health_hold(false);
        auth.set_control_mode(Botanik::Vars::CONTROL_ENABLED);
    }
    Botanik::Vars::VarRef ref(const char* name) { return bank.find(name); }
};

}  // namespace TestScan

#endif  // TESTS_SCAN_FIXTURE_H
