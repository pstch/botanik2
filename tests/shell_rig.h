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
#if BOTANIK_FEATURE_NETWORK
#include "libbotanik/network/network.h"
#include "libbotanik/network/resolver.h"
#endif
#if BOTANIK_FEATURE_NTP
#include "libbotanik/network/ntp.h"
#endif
#if BOTANIK_FEATURE_MQTT
#include "libbotanik/network/mqtt.h"
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
#include "libbotanik/modbus/rtu/master.h"
#endif

#include "site_bank.h"
#include "term_fixture.h"

namespace tests { namespace shell_rig {

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
    /** @brief The durable half, over a store a case binds if it wants one: the
     *  save verbs go through this, and with no store they refuse. */
    Vars::Persist                     persist;
    Vars::BankAlarms                  alarm_rows;
    Alarms::AlarmController           alarms;
#if BOTANIK_FEATURE_PLC
    Plc::Runtime::Plant               plant;
#endif
#if BOTANIK_FEATURE_NETWORK
    Network::NameResolver             dns;
    Network::NetworkHandler           net;
#endif
#if BOTANIK_FEATURE_NTP
    Network::NtpClient                ntp;
#endif
#if BOTANIK_FEATURE_MQTT
    Network::MqttClient               mqtt;
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
    Modbus::RtuMaster                 rtu;
#endif
    /** @brief What `stats` reports on. A bare struct rather than a
     *  LoopScheduler, so a case can state the metrics it asserts about: a real
     *  scheduler would only ever hold whatever the host did. */
    Scheduler::SchedulerContext       sched;
    SystemContext                     sys;
    Terminal::Console                 console;
    Terminal::EditorBuffers<2048, 16> store;
    Terminal::TextEditor              editor;
    Sink                              out;
    Shell::CommandShell               shell;

    Rig()
        : bank(SetupContext(reg, &log)),
          persist(SetupContext(reg, &log), bank),
          alarms(SetupContext(reg, &log), &alarm_rows),
#if BOTANIK_FEATURE_PLC
          plant(SetupContext(reg, &log), bank),
#endif
#if BOTANIK_FEATURE_NETWORK
          net(SetupContext(reg, &log), &dns),
#endif
#if BOTANIK_FEATURE_NTP
          ntp(SetupContext(reg, &log), &dns, &net),
#endif
#if BOTANIK_FEATURE_MQTT
          mqtt(SetupContext(reg, &log), &dns, &net),
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
          rtu(SetupContext(reg, &log), &bank, Site::g_site.devices),
#endif
          console(SetupContext(), wire.serial, &now),
          editor(&console, store.storage()),
          shell(console.terminal(), editor, out, &sys) {
        // 80x24, the console's own default. Without this the terminal is one
        // usable cell until PHASE_CONFIG runs, and _field() drops every detail
        // column as not fitting - so the reports would be asserted in a shape
        // no operator ever sees.
        console.terminal().reconfigure(Terminal::Ansi::TerminalType::VT102, 80, 24);
        TestSite::build(bank);
        alarm_rows.bind(&bank, &persist);
        alarms.setup(0);            // the K_ALARM scan is on_setup()
#if BOTANIK_FEATURE_PLC
        plant.wire();
        sys.plant = &plant;
#endif
        sys.vars      = &bank;
        sys.persist   = &persist;
        sys.tasks     = &reg;
        sys.scheduler = &sched;
#if BOTANIK_FEATURE_NETWORK
        sys.network = &net;
#endif
#if BOTANIK_FEATURE_NTP
        sys.ntp = &ntp;
#endif
#if BOTANIK_FEATURE_MQTT
        sys.mqtt = &mqtt;
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
        sys.modbus_rtu = &rtu;
#endif
        // `alarms ack` and the log verbs reach their targets this way, not
        // through the context: they mutate, and the context is the read side.
        shell.set_diagnostics(&log, &alarms);
    }

    /** @brief Put the components' work in the task table, so the reporting
     *  verbs have rows to report on. Opt-in: a case about the variable verbs
     *  wants an empty beat, and registering also runs each on_setup(). */
    void register_all() {
        bank.register_tasks();
        persist.register_tasks();
        alarms.register_tasks();
#if BOTANIK_FEATURE_PLC
        plant.register_tasks();
#endif
#if BOTANIK_FEATURE_NTP
        shell.set_ntp(&ntp);
#endif
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

}}  // namespace tests::shell_rig

#endif // TESTS_SHELL_RIG_H
