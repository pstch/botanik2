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

using namespace Botanik;
using namespace Botanik::Vars;

namespace {
    const uint16_t CAP = 32;

    #define P_NUM(nm, t, lo, hi, dflt) \
        { Scalar::U(lo), Scalar::U(hi), Scalar::U(dflt), nm, "", "", nullptr, \
          { XP_NONE, t, 0, 0, 0, 0, 1.0f, 0.0f }, \
          0, 0, 0, t, ROLE_PARAM, 0, STR_NONE, F_BOUNDED, {} }

    const Variable PARAMS[] = {
        P_NUM("p_bool", T_BOOL, 0, 1, 1),
        P_NUM("p_u16",  T_U16,  0, 3600, 64),
        { Scalar::I(-5000), Scalar::I(5000), Scalar::I(-7), "p_i32", "", "", nullptr,
          { XP_NONE, T_I32, 0, 0, 0, 0, 1.0f, 0.0f },
          0, 0, 0, T_I32, ROLE_PARAM, 0, STR_NONE, F_BOUNDED, {} },
        // A fractional band, which int32 bounds could not express at all.
        { Scalar::F(-90.0f), Scalar::F(90.0f), Scalar::F(48.8566f), "p_f32", "deg", "", nullptr,
          { XP_NONE, T_F32, 0, 0, 0, 0, 1.0f, 0.0f },
          0, 0, 0, T_F32, ROLE_PARAM, 4, STR_NONE, F_BOUNDED, {} },
        { Scalar::U(0), Scalar::U(0), Scalar::S("pool.ntp.org"), "p_str", "", "", nullptr,
          { XP_NONE, T_STR, 0, 0, 0, 0, 1.0f, 0.0f },
          0, 0, CAP, T_STR, ROLE_PARAM, 0, STR_HOST, 0, {} },
    };
    #undef P_NUM

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
        Bank         bank;
        KvParamStore store;
        Params       params;
        Fx() : params(&bank, &store, nullptr) {
            VarTables t = {};
            t.var = PARAMS; t.val = vals; t.count = 5;
            t.pool = pool; t.pool_bytes = CAP;
            t.param_first = 0; t.param_count = 5;
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
