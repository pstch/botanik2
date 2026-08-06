/**
 * @file test_params.cpp
 * @brief Parameters as variables: persistence round-trip per type, the edit
 *        path with its tri-state result, and the by-name reads the
 *        config-driven components use.
 */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include "doctest.h"

#include "libbotanik/hal/storage.h"
#include "libbotanik/utils/text.h"
#include "libbotanik/vars/params.h"
#include "libbotanik/vars/declare.h"
#include "libbotanik/vars/access.h"

using namespace Botanik;
using namespace Botanik::Vars;

namespace {
    const uint16_t CAP = 32;

    const Variable PARAMS[] = {
        //name      type    default   min    max   dgt unit   reg
        P("p_bool", T_BOOL, 1,        0,     1,    0,  "",    1, "", ""),
        P("p_u16",  T_U16,  64,       0,     3600, 0,  "",    2, "", ""),
        P("p_i32",  T_I32,  -7,       -5000, 5000, 0,  "",    3, "", ""),
        // A fractional band, which int32 bounds could not express at all.
        P("p_f32",  T_F32,  48.8566,  -90,   90,   4,  "deg", 4, "", ""),
        PS("p_str", "pool.ntp.org", CAP, STR_HOST, 5, "", ""),
    };

    struct StoreEnv {
        char path[64];
        explicit StoreEnv(const char* where = nullptr) {
            if (where) snprintf(path, sizeof(path), "%s", where);
            else       snprintf(path, sizeof(path), "/tmp/botanik_test_prm_%ld", (long)getpid());
            setenv("BOTANIK_CONFIG_STORE", path, 1);
            unlink(path);
        }
        ~StoreEnv() { unlink(path); unsetenv("BOTANIK_CONFIG_STORE"); }
    };

    struct Fx {
        Value        vals[5]{};
        char         pool[CAP]{};
        uint16_t     index[5]{};
        Bank         bank;
        KvParamStore store;
        Params       params;
        Fx() : params(&bank, &store, nullptr) {
            VarTables t = {};
            t.var = PARAMS; t.val = vals; t.count = 5; t.index = index;
            t.pool = pool; t.pool_bytes = CAP;
            bank = Bank(t);
            bank.init();
            params = Params(&bank, &store, nullptr);
        }
        Var by(const char* n) { return bank.var(bank.find(n)); }
    };
}

TEST_CASE("params: a parameter starts at its factory default, and valid") {
    StoreEnv env;
    Fx f;
    CHECK(f.by("p_u16").i() == 64);
    CHECK(f.by("p_u16").valid());
    CHECK(f.by("p_f32").value() == doctest::Approx(48.8566f));
    CHECK(Utils::compare_str(get_str(&f.bank, "p_str"), "pool.ntp.org"));
}

TEST_CASE("params: an edit applies, persists, and survives a reload") {
    StoreEnv env;
    {
        Fx f;
        CHECK(f.params.set(f.by("p_u16"), "120") == SET_OK);
        CHECK(f.params.set(f.by("p_i32"), "-4200") == SET_OK);
        CHECK(f.params.set(f.by("p_f32"), "-12.25") == SET_OK);
        CHECK(f.params.set(f.by("p_bool"), "0") == SET_OK);
        CHECK(f.params.set(f.by("p_str"), "ntp.example.org") == SET_OK);
    }
    {   // a fresh bank, loading from the same store
        Fx f;
        f.params.step(0);           // first step runs setup(): the boot load
        CHECK(f.by("p_u16").i() == 120);
        CHECK(f.by("p_i32").i() == -4200);          // sign survives the narrow width
        CHECK(f.by("p_f32").value() == doctest::Approx(-12.25f));
        CHECK_FALSE(f.by("p_bool").b());
        CHECK(Utils::compare_str(get_str(&f.bank, "p_str"), "ntp.example.org"));
    }
}

TEST_CASE("params: a missing key keeps the declared default") {
    StoreEnv env;
    Fx f;
    f.params.step(0);
    CHECK(f.by("p_u16").i() == 64);
    CHECK(Utils::compare_str(get_str(&f.bank, "p_str"), "pool.ntp.org"));
}

TEST_CASE("params: stored values keep the type's NARROW width") {
    // Not sizeof(Scalar): a u16 must occupy 2 bytes, or every existing record
    // changes encoding and HAL::Storage::get starts rejecting them.
    StoreEnv env;
    Fx f;
    CHECK(f.params.set(f.by("p_u16"), "300") == SET_OK);
    CHECK(f.params.set(f.by("p_bool"), "0") == SET_OK);
    CHECK(f.params.set(f.by("p_f32"), "1.5") == SET_OK);
    uint8_t buf[16]; size_t n = 0;
    REQUIRE(HAL::Storage::get("p_u16", buf, sizeof(buf), &n));
    CHECK(n == 2);
    REQUIRE(HAL::Storage::get("p_bool", buf, sizeof(buf), &n));
    CHECK(n == 1);
    REQUIRE(HAL::Storage::get("p_f32", buf, sizeof(buf), &n));
    CHECK(n == 4);
}

TEST_CASE("params: edits are bounds-checked in the parameter's own type") {
    StoreEnv env;
    Fx f;
    CHECK(f.params.set(f.by("p_u16"), "3601") == SET_INVALID);
    CHECK(f.by("p_u16").i() == 64);                 // unchanged
    CHECK(f.params.set(f.by("p_i32"), "-6000") == SET_INVALID);
    // A fractional bound is a real bound now: 90.5 is out, 89.5 is in.
    CHECK(f.params.set(f.by("p_f32"), "90.5") == SET_INVALID);
    CHECK(f.params.set(f.by("p_f32"), "89.5") == SET_OK);
    // ...and junk is junk.
    CHECK(f.params.set(f.by("p_u16"), "12abc") == SET_INVALID);
}

TEST_CASE("params: a string edit is validated by its declared kind") {
    StoreEnv env;
    Fx f;
    CHECK(f.params.set(f.by("p_str"), "ntp.example.org") == SET_OK);
    CHECK(f.params.set(f.by("p_str"), "bad host") == SET_INVALID);
    // Must fit the slot including the NUL.
    CHECK(f.params.set(f.by("p_str"), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") == SET_INVALID);
    CHECK(Utils::compare_str(get_str(&f.bank, "p_str"), "ntp.example.org"));
}

TEST_CASE("params: a refused persist still applies the value") {
    // The distinction the tri-state exists for: reporting "invalid" when the
    // storage refused a perfectly good value sends the operator to fix their
    // input, which was never the problem.
    StoreEnv env("/nonexistent-dir-botanik-test/store");
    Fx f;
    CHECK(f.params.set(f.by("p_u16"), "120") == SET_NOT_PERSISTED);
    CHECK(f.by("p_u16").i() == 120);
    CHECK(f.params.set(f.by("p_u16"), "99999") == SET_INVALID);   // still invalid
}

TEST_CASE("params: by-name reads are null-safe and type-aware") {
    StoreEnv env;
    Fx f;
    CHECK(get_int(&f.bank, "p_u16", 7) == 64);
    CHECK(get_f32(&f.bank, "p_f32", 0.0f) == doctest::Approx(48.8566f));
    CHECK(get_int(&f.bank, "nope", 7) == 7);
    CHECK(get_int(nullptr, "p_u16", 7) == 7);
    CHECK(Utils::compare_str(get_str(&f.bank, "nope"), ""));
    // A non-string read as a string yields "", not the bytes of a number.
    CHECK(Utils::compare_str(get_str(&f.bank, "p_u16"), ""));
    // The commit serial is just the parameter named after the group.
    CHECK(group_serial(&f.bank, "nope") == 0);
}

TEST_CASE("params: find only matches parameters, not any variable") {
    StoreEnv env;
    Fx f;
    CHECK(f.params.find("p_u16", 5).index != VAR_NONE);
    CHECK(f.params.find("nope", 4).index == VAR_NONE);
}

// ---- Persistent overrides ---------------------------------------------------

namespace {
    // A table with one row per role, so the output exclusion is testable.
    const Variable OVR_VARS[] = {
        MI("sensor", T_F32, 5, 3, 0, 2, SF(-50.0f), SF(150.0f), SF(1.5f), 5000, 1,
                     "C", "a reading", ""),
        G ("derived", T_F32, SF(0.0f), 1, "", "a computed value", ""),
        RO("coil",   31, 0, false, false, "an actuator", ""),
        P ("setting", T_U16, 64, 0, 3600, 0, "s", 1, "a parameter", ""),
    };

    /** @brief A bank over OVR_VARS, rebuilt from scratch to simulate a reboot -
     *  same store, fresh RAM. */
    struct OvrFx {
        Value        vals[4]{};
        uint16_t     index[4]{};
        Bank         bank;
        KvParamStore store;
        OvrFx() {
            VarTables t = {};
            t.var = OVR_VARS; t.val = vals; t.count = 4; t.index = index;
            bank = Bank(t);
            bank.set_store(&store, nullptr);
            bank.init();
            bank.enable_control(Bank::INHIBIT_HEALTH);   // so an output is drivable
            bank.load_overrides();
        }
    };
}

TEST_CASE("override: a forced value survives a reboot") {
    StoreEnv env("/tmp/botanik_test_ovr_a");
    {
        OvrFx f;
        REQUIRE(f.bank.override_set(f.bank.var("sensor"), "42.5") == SET_OK);
        CHECK(f.bank.var("sensor").value() == doctest::Approx(42.5f));
    }
    {
        OvrFx f;   // fresh RAM, same store
        CHECK(f.bank.var("sensor").overridden());
        CHECK(f.bank.var("sensor").value() == doctest::Approx(42.5f));
        CHECK(f.bank.var("sensor").valid());
        CHECK(f.bank.override_count() == 1);
    }
}

TEST_CASE("override: the validity it reads as survives too") {
    StoreEnv env("/tmp/botanik_test_ovr_b");
    {
        OvrFx f;
        // "force it, and make it read as a dead sensor" - the two are separate
        // facts and both have to come back.
        REQUIRE(f.bank.override_set(f.bank.var("sensor"), "42.5", false) == SET_OK);
        CHECK_FALSE(f.bank.var("sensor").valid());
    }
    {
        OvrFx f;
        CHECK(f.bank.var("sensor").overridden());
        CHECK_FALSE(f.bank.var("sensor").valid());
        // The forced value is still what it reads - forcing the QUALITY is a
        // separate thing from simulating the timeout fallback (see Value's
        // header note), and both halves came back from storage.
        CHECK(f.bank.var("sensor").value() == doctest::Approx(42.5f));
        // Clearing it hands the live value back.
        f.bank.override_clear(f.bank.var("sensor"));
        CHECK_FALSE(f.bank.var("sensor").overridden());
    }
}

TEST_CASE("override: releasing one stops it coming back") {
    StoreEnv env("/tmp/botanik_test_ovr_c");
    {
        OvrFx f;
        REQUIRE(f.bank.override_set(f.bank.var("derived"), "7") == SET_OK);
    }
    {
        OvrFx f;
        REQUIRE(f.bank.var("derived").overridden());
        f.bank.override_clear(f.bank.var("derived"));
    }
    {
        OvrFx f;
        CHECK_FALSE(f.bank.var("derived").overridden());
        CHECK(f.bank.override_count() == 0);
    }
}

TEST_CASE("override: `override clear` clears the stored copies too") {
    StoreEnv env("/tmp/botanik_test_ovr_d");
    {
        OvrFx f;
        REQUIRE(f.bank.override_set(f.bank.var("sensor"), "1") == SET_OK);
        REQUIRE(f.bank.override_set(f.bank.var("derived"), "2") == SET_OK);
        CHECK(f.bank.clear_overrides() == 2);
    }
    {
        OvrFx f;
        CHECK(f.bank.override_count() == 0);
    }
}

TEST_CASE("override: an actuator override persists like any other") {
    // An override means one thing whatever row it is on - "a person has taken
    // this by hand" - so an actuator's survives the reboot the same way a
    // sensor's does. Carving out an exception made it mean two things, and left
    // an operator wondering why the valve they forced came back and the reading
    // they forced did not.
    StoreEnv env("/tmp/botanik_test_ovr_e");
    {
        OvrFx f;
        REQUIRE(f.bank.override_set(f.bank.var("coil"), "1") == SET_OK);
        CHECK(f.bank.var("coil").overridden());
    }
    {
        OvrFx f;
        CHECK(f.bank.var("coil").overridden());
        CHECK(f.bank.var("coil").b());
        CHECK(f.bank.override_count() == 1);
    }
    // ...and the system withdrawing control still takes it away, stored copy
    // included, so the restore cannot bring it back a second time.
    {
        OvrFx f;
        f.bank.disable_control(Bank::INHIBIT_HEALTH);
        CHECK_FALSE(f.bank.var("coil").overridden());
    }
    {
        OvrFx f;
        CHECK_FALSE(f.bank.var("coil").overridden());
        CHECK(f.bank.override_count() == 0);
    }
}

TEST_CASE("override: the system withdrawing control wipes the stored copy") {
    // A health withdrawal releases output overrides; a stored copy would hand
    // one back at the next boot, which is the hole the release exists to shut.
    StoreEnv env("/tmp/botanik_test_ovr_f");
    {
        OvrFx f;
        REQUIRE(f.bank.override_set(f.bank.var("sensor"), "5") == SET_OK);
        f.bank.disable_control(Bank::INHIBIT_HEALTH);
        // An input's override is NOT released by a control withdrawal - control
        // authority is about driving the field, not about reading it.
        CHECK(f.bank.var("sensor").overridden());
    }
    {
        OvrFx f;
        CHECK(f.bank.var("sensor").overridden());
    }
}

TEST_CASE("override: a parameter override shadows without disturbing the setting") {
    StoreEnv env("/tmp/botanik_test_ovr_g");
    {
        OvrFx f;
        REQUIRE(f.bank.set(f.bank.var("setting"), "100") != SET_INVALID);
        REQUIRE(f.bank.override_set(f.bank.var("setting"), "7") == SET_OK);
        CHECK(f.bank.var("setting").i() == 7);
    }
    {
        OvrFx f;
        f.bank.load_params();
        CHECK(f.bank.var("setting").i() == 7);         // the override still wins
        f.bank.override_clear(f.bank.var("setting"));
        CHECK(f.bank.var("setting").i() == 100);       // the setting was untouched
    }
}

TEST_CASE("override: forcing only the quality persists the quality") {
    // Regression. `override <name> invalid` froze through the bank (persisting
    // the CURRENT quality) and then forced the new quality through the HANDLE,
    // which cannot reach the store - so the reboot handed back the old one.
    StoreEnv env("/tmp/botanik_test_ovr_q");
    {
        OvrFx f;
        f.bank.var("sensor").publish(Scalar::F(21.0f), 1000);
        REQUIRE(f.bank.var("sensor").valid());
        f.bank.override_freeze(f.bank.var("sensor"));
        f.bank.override_set(f.bank.var("sensor"), f.bank.var("sensor").raw(), false);
        CHECK_FALSE(f.bank.var("sensor").valid());
    }
    {
        OvrFx f;
        CHECK(f.bank.var("sensor").overridden());
        CHECK_FALSE(f.bank.var("sensor").valid());
        CHECK(f.bank.var("sensor").value() == doctest::Approx(21.0f));
    }
}

TEST_CASE("override: every release path also forgets the stored copy") {
    // Regression. Var's mutators were public and cleared RAM only, so a release
    // through the handle left the record behind and the override came back at
    // the next boot. They are Bank's now, which is why this can only be spelled
    // one way.
    StoreEnv env("/tmp/botanik_test_ovr_r");
    {
        OvrFx f;
        REQUIRE(f.bank.override_set(f.bank.var("sensor"), "9") == SET_OK);
        f.bank.override_clear(f.bank.var("sensor"));
        CHECK_FALSE(f.bank.var("sensor").overridden());
    }
    {
        OvrFx f;
        CHECK_FALSE(f.bank.var("sensor").overridden());
        CHECK(f.bank.override_count() == 0);
    }
}
