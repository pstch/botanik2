/**
 * @file test_config_local.cpp
 * @brief Tests for LocalConfigHandler: the flash/file-backed config backend.
 *        Storage points at a throwaway per-test directory; checks the boot load,
 *        serial seeding, edit + group-serial bump, validation, and persistence
 *        across a fresh handler.
 */
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
    struct StoreEnv {
        char dir[64];
        StoreEnv() {
            snprintf(dir, sizeof(dir), "/tmp/botanik_test_local_%ld", (long)getpid());
            setenv("BOTANIK_CONFIG_STORE", dir, 1);
        }
        ~StoreEnv() {
            const char* keys[] = {"ntp_poll_s", "net_ip", "geo_latitude_e7"};
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

TEST_CASE("LocalConfigHandler: boot seeds serials, edits persist + bump") {
    StoreEnv env;

    LocalConfigHandler h(nullptr);
    h.step(0);   // first step runs setup(): load (empty) + seed serials to 1

    // Every group serial seeded to 1 so consumers apply their default once.
    CHECK(h.image()->net_serial  == 1);
    CHECK(h.image()->ntp_serial  == 1);
    CHECK(h.image()->mqtt_serial == 1);
    CHECK(h.image()->geo_serial  == 1);

    // Edit a scalar: image updates and the group serial bumps.
    const ConfigParam* poll = nullptr;
    const ConfigParam* ip = nullptr;
    for (uint8_t i = 0; i < h.count(); i++) {
        if (strcmp(h.param(i).name, "ntp_poll_s") == 0) poll = &h.param(i);
        if (strcmp(h.param(i).name, "net_ip") == 0)     ip = &h.param(i);
    }
    REQUIRE(poll != nullptr);
    REQUIRE(ip != nullptr);

    CHECK(h.set(*poll, "120"));
    CHECK(h.image()->ntp_poll_s == 120);
    CHECK(h.image()->ntp_serial == 2);        // bumped

    // Edit a string (validated as IPv4).
    CHECK(h.set(*ip, "10.0.0.5"));
    CHECK(strcmp(h.image()->net_ip, "10.0.0.5") == 0);
    CHECK(h.image()->net_serial == 2);

    // Invalid edits are rejected and leave the image untouched.
    CHECK_FALSE(h.set(*poll, "99999"));       // > u16-bounded max 65535
    CHECK(h.image()->ntp_poll_s == 120);
    CHECK_FALSE(h.set(*ip, "999.1.1.1"));     // bad octet
    CHECK(strcmp(h.image()->net_ip, "10.0.0.5") == 0);
}

TEST_CASE("LocalConfigHandler: values persist across a fresh handler") {
    StoreEnv env;

    const ConfigParam* poll = nullptr;
    const ConfigParam* ip = nullptr;
    {
        LocalConfigHandler h(nullptr);
        h.step(0);
        for (uint8_t i = 0; i < h.count(); i++) {
            if (strcmp(h.param(i).name, "ntp_poll_s") == 0) poll = &h.param(i);
            if (strcmp(h.param(i).name, "net_ip") == 0)     ip = &h.param(i);
        }
        h.set(*poll, "300");
        h.set(*ip, "192.168.9.9");
    }

    // A brand-new handler (simulating a reboot) reloads persisted values from
    // storage; serials are freshly seeded to 1.
    LocalConfigHandler h2(nullptr);
    h2.step(0);
    CHECK(h2.image()->ntp_poll_s == 300);
    CHECK(strcmp(h2.image()->net_ip, "192.168.9.9") == 0);
    CHECK(h2.image()->ntp_serial == 1);
    CHECK(h2.image()->net_serial == 1);
}
