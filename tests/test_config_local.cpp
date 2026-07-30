/**
 * @file test_config_local.cpp
 * @brief Tests for LocalConfigHandler: the flash/file-backed config backend.
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

#include "libbotanik/core/config_local.h"

using namespace Botanik;
using namespace Botanik::Cfg;

namespace {
    // Self-contained config payload (the site's PLC output layout stand-in).
    struct TestCfg {
        uint16_t ntp_poll_s;
        uint8_t  _pad[2];
        char     net_ip[16];
    };

#define OFF(f) (int16_t)offsetof(TestCfg, f)
    const ConfigParam PARAMS[] = {
        // field {name, out_offset, in_offset, type, digits, str, unit, desc, notes}, meta {def, min, max, len, kind, group}
        {{"ntp_poll_s", OFF(ntp_poll_s), OFF(ntp_poll_s), SCALAR_U16,    0, nullptr, "s", "", ""}, {"64", 0, 65535, 0,  STR_NONE, "ntp"}},
        {{"net_ip",     OFF(net_ip),     OFF(net_ip),     SCALAR_STRING, 0, nullptr, "",  "", ""}, {"",   0, 0,     16, STR_IPV4, "net"}},
    };
#undef OFF
    const uint8_t PARAM_COUNT = (uint8_t)(sizeof(PARAMS) / sizeof(PARAMS[0]));

    struct StoreEnv {
        char dir[64];
        StoreEnv() {
            snprintf(dir, sizeof(dir), "/tmp/botanik_test_local_%ld", (long)getpid());
            setenv("BOTANIK_CONFIG_STORE", dir, 1);
        }
        ~StoreEnv() {
            const char* keys[] = {"ntp_poll_s", "net_ip"};
            char path[128];
            for (const char* k : keys) {
                snprintf(path, sizeof(path), "%s/%s", dir, k);
                unlink(path);
            }
            rmdir(dir);
            unsetenv("BOTANIK_CONFIG_STORE");
        }
    };
}

TEST_CASE("LocalConfigHandler: edits apply to the image and validate") {
    StoreEnv env;

    LocalConfigHandler<TestCfg> h(nullptr);
    h.set_site_params(PARAMS, PARAM_COUNT);
    h.step(0);   // first step runs setup(): load (empty) -> zeroed defaults

    const ConfigParam* poll = h.find("ntp_poll_s", 10);
    const ConfigParam* ip   = h.find("net_ip", 6);
    REQUIRE(poll != nullptr);
    REQUIRE(ip != nullptr);

    // Edit a scalar: the image (read at out_offset) updates.
    CHECK(h.set(*poll, "120"));
    CHECK(cfg_int(&h, "ntp_poll_s") == 120);

    // Edit a string (validated as IPv4).
    CHECK(h.set(*ip, "10.0.0.5"));
    CHECK(strcmp(cfg_str(&h, "net_ip"), "10.0.0.5") == 0);

    // Invalid edits are rejected and leave the image untouched.
    CHECK_FALSE(h.set(*poll, "99999"));       // > u16-bounded max 65535
    CHECK(cfg_int(&h, "ntp_poll_s") == 120);
    CHECK_FALSE(h.set(*ip, "999.1.1.1"));     // bad octet
    CHECK(strcmp(cfg_str(&h, "net_ip"), "10.0.0.5") == 0);
}

TEST_CASE("LocalConfigHandler: values persist across a fresh handler") {
    StoreEnv env;

    {
        LocalConfigHandler<TestCfg> h(nullptr);
        h.set_site_params(PARAMS, PARAM_COUNT);
        h.step(0);
        h.set(*h.find("ntp_poll_s", 10), "300");
        h.set(*h.find("net_ip", 6), "192.168.9.9");
    }

    // A brand-new handler (simulating a reboot) reloads persisted values.
    LocalConfigHandler<TestCfg> h2(nullptr);
    h2.set_site_params(PARAMS, PARAM_COUNT);
    h2.step(0);
    CHECK(cfg_int(&h2, "ntp_poll_s") == 300);
    CHECK(strcmp(cfg_str(&h2, "net_ip"), "192.168.9.9") == 0);
}
