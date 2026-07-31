/**
 * @file test_config_local.cpp
 * @brief Tests for LocalParamStore: the flash/file-backed params backend,
 *        driven through the generic ParamHandler.
 *        Storage points at a throwaway per-test directory. The handler is
 *        templated on a tiny self-contained config payload and driven by a
 *        site-style parameter table (the library ships none); checks the boot
 *        load, edit + persistence, and validation. Config values are ordinary
 *        payload fields now, so there is no library serial-seeding.
 */
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>

#include "doctest.h"

#include "libbotanik/params/store_local.h"

using namespace Botanik;
using namespace Botanik::Params;

namespace {
    // Self-contained config payload (the site's PLC output layout stand-in).
    struct TestCfg {
        uint16_t ntp_poll_s;
        uint8_t  _pad[2];
        char     net_ip[16];
    };

#define OFF(f) (int16_t)offsetof(TestCfg, f)
    const Param PARAMS[] = {
        // field {name, out_offset, type, digits, str, unit, desc, notes}, meta {def, min, max, len, kind, group}
        {{"ntp_poll_s", OFF(ntp_poll_s), SCALAR_U16,    0, nullptr, "s", "", ""}, {"64", 0, 65535, 0,  STR_NONE, "ntp"}},
        {{"net_ip",     OFF(net_ip),     SCALAR_STRING, 0, nullptr, "",  "", ""}, {"",   0, 0,     16, STR_IPV4, "net"}},
    };
#undef OFF
    const uint8_t PARAM_COUNT = (uint8_t)(sizeof(PARAMS) / sizeof(PARAMS[0]));

    /** @brief Point HAL::Storage at a scratch store. It is one FILE holding
     *  every key, not a directory - the one-file-per-key layout is long gone. */
    struct StoreEnv {
        char path[64];
        explicit StoreEnv(const char* where = nullptr) {
            if (where != nullptr) {
                snprintf(path, sizeof(path), "%s", where);
            } else {
                snprintf(path, sizeof(path), "/tmp/botanik_test_local_%ld", (long)getpid());
            }
            setenv("BOTANIK_CONFIG_STORE", path, 1);
        }
        ~StoreEnv() { unlink(path); unsetenv("BOTANIK_CONFIG_STORE"); }
    };
}

TEST_CASE("LocalParamStore: edits apply to the image and validate") {
    StoreEnv env;

    LocalParamStore h_store(nullptr);

    ParamHandler h(&h_store, nullptr);
    h.set_site_params(PARAMS, PARAM_COUNT);
    h.step(0);   // first step runs setup(): load (empty) -> zeroed defaults

    const Param* poll = h.find("ntp_poll_s", 10);
    const Param* ip   = h.find("net_ip", 6);
    REQUIRE(poll != nullptr);
    REQUIRE(ip != nullptr);

    // Edit a scalar: the image (read at out_offset) updates.
    CHECK(h.set(*poll, "120") == Params::SET_OK);
    CHECK(get_int(&h, "ntp_poll_s") == 120);

    // Edit a string (validated as IPv4).
    CHECK(h.set(*ip, "10.0.0.5") == Params::SET_OK);
    CHECK(strcmp(get_str(&h, "net_ip"), "10.0.0.5") == 0);

    // Invalid edits are rejected and leave the image untouched.
    CHECK(h.set(*poll, "99999") == Params::SET_INVALID);       // > u16-bounded max 65535
    CHECK(get_int(&h, "ntp_poll_s") == 120);
    CHECK(h.set(*ip, "999.1.1.1") == Params::SET_INVALID);     // bad octet
    CHECK(strcmp(get_str(&h, "net_ip"), "10.0.0.5") == 0);
}

TEST_CASE("LocalParamStore: values persist across a fresh handler") {
    StoreEnv env;

    {
        LocalParamStore h_store(nullptr);
        ParamHandler h(&h_store, nullptr);
        h.set_site_params(PARAMS, PARAM_COUNT);
        h.step(0);
        h.set(*h.find("ntp_poll_s", 10), "300");
        h.set(*h.find("net_ip", 6), "192.168.9.9");
    }

    // A brand-new handler (simulating a reboot) reloads persisted values.
    LocalParamStore h2_store(nullptr);
    ParamHandler h2(&h2_store, nullptr);
    h2.set_site_params(PARAMS, PARAM_COUNT);
    h2.step(0);
    CHECK(get_int(&h2, "ntp_poll_s") == 300);
    CHECK(strcmp(get_str(&h2, "net_ip"), "192.168.9.9") == 0);
}

TEST_CASE("LocalParamStore: a value that cannot be stored is not a rejected edit") {
    // The distinction the tri-state exists for. Reporting "invalid" when the
    // flash refused a perfectly good value sends the operator to fix their
    // input, which was never the problem.
    StoreEnv env("/nonexistent-dir-botanik-test/store");

    LocalParamStore h_store(nullptr);
    ParamHandler h(&h_store, nullptr);
    h.set_site_params(PARAMS, PARAM_COUNT);
    h.step(0);

    const Param* poll = h.find("ntp_poll_s", 10);
    REQUIRE(poll != nullptr);

    CHECK(h.set(*poll, "120") == Params::SET_NOT_PERSISTED);
    // ...and the value IS live, which is why this is not SET_INVALID.
    CHECK(Params::get_int(&h, "ntp_poll_s", 0) == 120);
    // A genuinely bad value still reads as invalid, storage or no storage.
    CHECK(h.set(*poll, "99999") == Params::SET_INVALID);
}
