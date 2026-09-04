#ifndef TESTS_SHELL_RIG_H
#define TESTS_SHELL_RIG_H

/**
 * @file shell_rig.h
 * @brief A CommandShell with the whole SystemContext behind it.
 *
 * `test_shell.cpp` wires no context, which is what makes it a test of the
 * dispatcher: every variable command short-circuits on a null bank. This rig is
 * the other half - the site's real rows, the real scan roster and the real
 * alarm table - so the write verbs run all the way through to the model.
 */

#include <cstring>
#include <string>

#include "doctest.h"

#include "libbotanik.h"
#include "libbotanik/alarms/alarms.h"
#include "libbotanik/plc/runtime/runtime.h"
#include "libbotanik/vars/alarm_rows.h"

#include "site_bank.h"
#include "term_fixture.h"

namespace shell_rig {

using namespace Botanik;

#if BOTANIK_FEATURE_SHELL_PAGE && BOTANIK_FEATURE_VARS

/** @brief A ShellWriter that keeps what it was given, line by line. */
struct Sink : Shell::ShellWriter {
    std::string text;
    bool        logs = true;

    size_t write(const char* buf, size_t len) override {
        text.append(buf, len);
        return len;
    }
    void clear() override { text.clear(); }
    void set_logs_enabled(bool on) override { logs = on; }
    bool logs_enabled() const override { return logs; }
};

/** @brief The site's bank, its scan roster and its alarms, behind one shell. */
struct Rig {
    botanik_test::PipeSerial          wire;
    uint64_t                          now = 0;
    Tasks::Registry                   reg;
    Logs::Logger                      log;
    Vars::Bank                        bank;
    Vars::BankAlarms                  alarm_rows;
    Alarms::AlarmController           alarms;
#if BOTANIK_FEATURE_PLC
    Plc::Runtime::Plant               plant;
#endif
    SystemContext                     sys;
    Terminal::Console                 console;
    Terminal::EditorBuffers<2048, 16> store;
    Terminal::TextEditor              editor;
    Sink                              out;
    Shell::CommandShell               shell;

    Rig()
        : bank(SetupContext(reg, &log)),
          alarms(SetupContext(reg, &log), &alarm_rows),
#if BOTANIK_FEATURE_PLC
          plant(SetupContext(reg, &log), bank),
#endif
          console(SetupContext(), wire.serial, &now),
          editor(&console, store.storage()),
          shell(console.terminal(), editor, out, &sys) {
        TestSite::build(bank);
        alarm_rows.bind(&bank);
        alarms.setup(0);            // the SCOPE_A scan is on_setup()
#if BOTANIK_FEATURE_PLC
        plant.wire();
        sys.plant = &plant;
#endif
        sys.vars  = &bank;
        sys.tasks = &reg;
        // `alarms ack` and the log verbs reach their targets this way, not
        // through the context: they MUTATE, and the context is the read side.
        shell.set_diagnostics(&log, &alarms);
    }

    /** @brief Run one command line and hand back everything it printed. */
    const std::string& run(const char* line) {
        out.clear();
        shell.run(line, (uint16_t)std::strlen(line));
        return out.text;
    }

    /** @brief Whether the last run said @p s. */
    bool said(const char* s) const { return out.text.find(s) != std::string::npos; }

    Vars::Var var(const char* name) { return bank.var(bank.find(name)); }
};

#endif // BOTANIK_FEATURE_SHELL_PAGE && BOTANIK_FEATURE_VARS

}  // namespace shell_rig

#endif // TESTS_SHELL_RIG_H
