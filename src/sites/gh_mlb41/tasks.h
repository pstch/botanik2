#ifndef SITE_TASKS_H
#define SITE_TASKS_H

/**
 * @file tasks.h
 * @brief HAND-WRITTEN - the gh_mlb41 control programs.
 *
 * Not generated: this is the one place a site expresses *behaviour* rather than
 * schema.
 *
 * A program is a Plc::Program subclass. It holds its function blocks and its
 * resolved variable references as members, resolves the names it uses once in
 * bind(), and reads and drives variables in run(). Three rules the scaffolding
 * guarantees, and which the programs below rely on:
 *
 *  - every program in a beat sees the same instant (inputs refresh once, before
 *    any of them runs)
 *  - @c dt_ms is the REAL time since this program last ran, so a timer keeps
 *    correct time even when the scan was late
 *  - nothing runs at all while control authority is withheld, and the first
 *    activation after it returns says so (@c c.first_scan), which is where a
 *    program drops whatever the outage made stale
 */

#include <cstdint>

#include "libbotanik.h"

#include "vars.h"

namespace Site {

using namespace Botanik;

/**
 * @brief Supply watchdog: hold the windows shut while the incoming mains is
 * unhealthy.
 *
 * A worked example of the intended shape rather than the final control law: it
 * reads two variables, applies a debounce and drives four coils, with no clock
 * read, no direct IO and no name lookup once it is running.
 */
class SupplyWatch : public Plc::Program {

    /** @name Blocks
     *  @{ */

    Plc::Ton _bad_for;    ///< debounce: ignore a momentary dropout
    Plc::Sr  _lockout;

    /** @} */
    /** @name Variables
     *  @{ */

    Vars::VarRef _ln      = { Vars::VAR_NONE };
    Vars::VarRef _locked  = { Vars::VAR_NONE };
    Vars::VarRef _win[4]  = { { Vars::VAR_NONE }, { Vars::VAR_NONE },
                              { Vars::VAR_NONE }, { Vars::VAR_NONE } };

    /** @} */

public:
    SupplyWatch(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "supply_watch", Tasks::GROUP_SLOW, io) {}

protected:
    void bind(Vars::Bank& io) override {
        _ln     = io.find("grid_LN_V");
        _locked = io.find("sys_supply_lockout");
        _win[0] = io.find("cli_window1_opn");
        _win[1] = io.find("cli_window2_opn");
        _win[2] = io.find("cli_window3_opn");
        _win[3] = io.find("cli_window4_opn");
    }

    void run(Plc::Context& c) override {
        // Both blocks are a claim about the supply over TIME, and not being
        // called means we stopped watching it. Start the debounce again rather
        // than resuming a dwell that was counting an outage we did not see.
        if (c.first_scan) {
            _bad_for.reset();
            _lockout.reset();
        }

        // A meter that has stopped answering reads as its default (0 V), which
        // is out of band and therefore unhealthy - the fallback does the right
        // thing here without the program testing valid() at all.
        const bool unhealthy = !Plc::in_range(c.bank.var(_ln).as_f32(), 200.0f, 260.0f);

        // Reset-dominant: recovery wins, so the lockout cannot stick once the
        // supply is good again.
        _lockout(_bad_for(unhealthy, 5000, c.dt_ms), !unhealthy);

        // Publish the decision as an internal, so the console, the shell and
        // telemetry can all see WHY the windows are held - a coil reading 0
        // does not distinguish "not commanded" from "locked out".
        c.bank.drive(_locked, _lockout.q());

        if (_lockout.q()) {
            for (const Vars::VarRef& w : _win) c.bank.drive(w, false);
        }
    }

};

/**
 * @brief Report the incoming supply frequency as a slow, deadbanded reading.
 *
 * Exists mostly to show a second rate: it runs a tenth as often as the supply
 * watchdog, and the scan drives both from the same table.
 */
class SupplyReport : public Plc::Program {

    /** @name Blocks
     *  @{ */

    Plc::Deadband _hz;

    /** @} */
    /** @name Variables
     *  @{ */

    Vars::VarRef _out = { Vars::VAR_NONE };
    Vars::VarRef _raw = { Vars::VAR_NONE };

    /** @} */

public:
    SupplyReport(const SetupContext& ctx, Vars::Bank& io)
        : Program(ctx, "supply_report", Tasks::GROUP_BACKGROUND, io) {}

protected:
    void bind(Vars::Bank& io) override {
        _out = io.find("sys_supply_hz");
        _raw = io.find("grid_Hz");
    }

    void run(Plc::Context& c) override {
        c.bank.drive(_out, _hz(c.bank.var(_raw).as_f32(), 0.05f));
    }

    // No reset(): the deadband holds the last REPORTED reading, and a stale
    // frequency reading is corrected by the next activation rather than being
    // dangerous in the meantime.
};

}  // namespace Site

#endif // SITE_TASKS_H
