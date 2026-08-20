/**
 * @file test_bindings.cpp
 * @brief ConfigBinder: when a component is handed its configuration.
 *
 * The rules a component depends on and cannot check for itself, so they are
 * asserted against a target that only counts pushes.
 */

#include <cstdint>

#include "doctest.h"
#include "libbotanik.h"
#include "libbotanik/vars/bindings.h"
#include "libbotanik/vars/declare.h"
#include "libbotanik/vars/model.h"
#include "libbotanik/vars/params.h"
#include "site_bank.h"

namespace bindings {

using namespace Botanik;

namespace {

    struct Target {
        uint32_t pushes    = 0;
        uint32_t announced = 0;
        int32_t  value     = -1;
    };

    void apply_target(void* t, const Vars::Bank& io, bool announce) {
        Target& tgt = *static_cast<Target*>(t);
        tgt.pushes++;
        if (announce) tgt.announced++;
        tgt.value = (int32_t)io.var("grp_value").i();
    }

    /** @brief A staged group and an unstaged row, in one small bank. */
    struct Fixture {
        Vars::Bank         bank;
        Vars::ConfigBinder binder;
        Target             staged;
        Target             loose;

        Fixture() : binder(Botanik::SetupContext(), &bank) {
            using namespace Botanik::Vars;
            static const Descriptor v[] = {
                P("grp_serial", T_U32, 1, 0, 9999, 0, "", 0, "", ""),
                P("grp_value",  T_I32, 7, 0, 9999, 0, "", 1, "", ""),
            };
            bank.register_vars(v, 2);
            bank.freeze();
            binder.add("grp",   &staged, &apply_target);
            binder.add(nullptr, &loose,  &apply_target);
        }

        void set(const char* name, int32_t v) {
            bank.var(bank.find(name)).set((float)v);
        }
    };

}  // namespace

TEST_CASE("binder: the first push is unconditional and does not announce") {
    Fixture f;
    CHECK(f.staged.pushes == 0);          // nothing before the first pass

    f.binder.apply_all();
    CHECK(f.staged.pushes == 1);
    CHECK(f.staged.value == 7);           // the stored value, not the struct default
    // A first push is the store arriving, not an operator changing something.
    CHECK(f.staged.announced == 0);
}

TEST_CASE("binder: a staged group waits for its serial") {
    Fixture f;
    f.binder.apply_all();

    // The edit alone must not reach the component: that is what staging IS.
    f.set("grp_value", 42);
    f.binder.apply_all();
    CHECK(f.staged.pushes == 1);
    CHECK(f.staged.value == 7);

    f.set("grp_serial", 2);
    f.binder.apply_all();
    CHECK(f.staged.pushes == 2);
    CHECK(f.staged.value == 42);
    CHECK(f.staged.announced == 1);       // a real change, so it is announced

    f.binder.apply_all();                 // serial unchanged: nothing more
    CHECK(f.staged.pushes == 2);
}

TEST_CASE("binder: an unstaged binding is pushed every pass") {
    Fixture f;
    f.binder.apply_all();
    f.binder.apply_all();
    f.binder.apply_all();
    CHECK(f.loose.pushes == 3);
    // No serial to wait for, so an edit lands on the next pass.
    f.set("grp_value", 99);
    f.binder.apply_all();
    CHECK(f.loose.value == 99);
}

TEST_CASE("binder: a group with no rows is never pushed at all") {
    // A site that does not configure a component leaves it at its struct
    // defaults; group_serial answers 0, and 0 is not a value to push on.
    Vars::Bank bank;
    Vars::ConfigBinder binder(Botanik::SetupContext(), &bank);
    Target absent;
    bank.freeze();
    binder.add("nosuch", &absent, &apply_target);

    binder.apply_all();
    binder.apply_all();
    CHECK(absent.pushes == 0);
}

TEST_CASE("binder: a binder with no bank pushes nothing") {
    Vars::ConfigBinder binder(Botanik::SetupContext(), nullptr);
    Target t;
    binder.add("grp", &t, &apply_target);
    binder.apply_all();
    CHECK(t.pushes == 0);
}


TEST_CASE("library defaults: the CSV and the no-VARS fallback still agree") {
    // The library's parameter defaults are LITERALS in params.csv, so the file
    // reads as the declaration it is and the generated report shows numbers
    // rather than `MonitorConfig().baud`. The C++ constant did not go away
    // though: it is what the component uses when the row is absent, which is
    // every build with VARS off. Two definitions of one value, and this is the
    // seam between them - the one thing that would otherwise let a build with
    // no variable model boot on a different figure from one with it.
    Vars::Bank bank;
    TestSite::build(bank);

    struct Pair { const char* row; int64_t cpp; };
    const Pair pairs[] = {
        { "monitor_baud",     (int64_t)MonitorConfig().baud },
        { "console_baud",     (int64_t)Terminal::ConsoleConfig().baud },
        { "console_type",     (int64_t)(uint8_t)Terminal::ConsoleConfig().type },
        { "console_cols",     (int64_t)Terminal::ConsoleConfig().cols },
        { "console_rows",     (int64_t)Terminal::ConsoleConfig().rows },
        { "net_enable",       (int64_t)Network::NetworkConfig().enabled },
        { "ntp_poll_sec",     (int64_t)Network::NtpConfig().poll_sec },
        { "ntp_port",         (int64_t)Network::NtpConfig().port },
        { "mqtt_port",        (int64_t)Network::MqttConfig().port },
        { "mqtt_ka_sec",      (int64_t)Network::MqttConfig().ka_sec },
        { "mqtt_tlm_sec",     (int64_t)(Network::TelemetryConfig().period_ms / 1000) },
        { "mqtt_retain",      (int64_t)Network::TelemetryConfig().retain },
        { "mb_port",          (int64_t)Modbus::TcpSlaveConfig().port },
        { "sys_cpu_load_pct", (int64_t)Config::ALARM_CPU_LOAD_PCT },
        { "sys_jitter_pct",   (int64_t)Config::ALARM_CPU_JITTER_PCT },
        { "sys_stack_pct",    (int64_t)Config::ALARM_STACK_PCT },
        { "sys_retain_sec",   (int64_t)(Config::RETAIN_SAVE_MS / 1000) },
        { "sys_log_level",    (int64_t)Config::LOG_MIN_LEVEL },
        { "rtu_timeout_ms",   (int64_t)Config::MODBUS_TIMEOUT_MS },
        { "rtu_retries",      (int64_t)Config::MODBUS_RETRIES },
        { "rtu_backoff_ms",   (int64_t)Config::MODBUS_BACKOFF_MS },
        { "rtu_backoff_max_ms", (int64_t)Config::MODBUS_BACKOFF_MAX_MS },
        { "rtu_write_min_ms", (int64_t)Config::MODBUS_WRITE_MIN_MS },
        { "rtu_rewrite_ms",   (int64_t)Config::MODBUS_REWRITE_MS },
    };

    for (const Pair& p : pairs) {
        const Vars::VarRef r = bank.find(p.row);
        INFO("row '" << p.row << "'");
        REQUIRE(r.index != Vars::VAR_NONE);
        const Vars::Descriptor* d = bank.var(r).meta();
        CHECK(Vars::as_i64(d->type, d->def) == p.cpp);
    }
}

TEST_CASE("library bands: an editable bound never exceeds what the buffer holds") {
    // The console's band was `BOTANIK_TERMINAL_MAX_COLS` and is a literal now,
    // so the CSV reads as the declaration it is. That constant still SIZES the
    // terminal's grid, though, so the band has to stay inside it: a row whose
    // maximum went past it would let an operator set a width the screen buffer
    // cannot hold.
    //
    // Asserted as a DIRECTION, not an equality. A site narrowing the band by
    // restating the row is the supported way to change it, and that must not
    // fail here.
    Vars::Bank bank;
    TestSite::build(bank);

    struct Band { const char* row; int64_t floor; int64_t ceiling; };
    const Band bands[] = {
        { "console_cols", Config::SCREEN_MIN_COLS, Config::TERMINAL_MAX_COLS },
        { "console_rows", Config::SCREEN_MIN_ROWS, Config::TERMINAL_MAX_ROWS },
    };

    for (const Band& b : bands) {
        const Vars::VarRef r = bank.find(b.row);
        INFO("row '" << b.row << "'");
        REQUIRE(r.index != Vars::VAR_NONE);
        const Vars::Descriptor* d = bank.var(r).meta();
        REQUIRE((d->flags & Vars::F_BOUNDED) != 0);
        CHECK(Vars::as_i64(d->type, d->pay.num.min) >= b.floor);
        CHECK(Vars::as_i64(d->type, d->pay.num.max) <= b.ceiling);
    }
}

}  // namespace bindings
