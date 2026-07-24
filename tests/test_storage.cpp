/**
 * @file test_storage.cpp
 * @brief Tests for the simulator HAL::Storage backend (a single external KV
 *        file). Points the store at a throwaway per-test file via
 *        $BOTANIK_CONFIG_STORE and starts each case from empty.
 */
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>

#include "doctest.h"

#include "libbotanik/hal/storage.h"

using namespace Botanik::HAL;

namespace {
    struct StoreEnv {
        char path[64];
        StoreEnv() {
            snprintf(path, sizeof(path), "/tmp/botanik_test_store_%ld", (long)getpid());
            setenv("BOTANIK_CONFIG_STORE", path, 1);
            unlink(path);   // start from an empty store
        }
        ~StoreEnv() {
            unlink(path);
            unsetenv("BOTANIK_CONFIG_STORE");
        }
    };
}

TEST_CASE("storage: set/get round-trip and missing key") {
    StoreEnv env;

    uint32_t v = 0xDEADBEEF, back = 0;
    CHECK(Storage::set("k_scalar", &v, sizeof(v)));
    size_t n = 0;
    CHECK(Storage::get("k_scalar", &back, sizeof(back), &n));
    CHECK(n == sizeof(v));
    CHECK(back == v);

    // A string value.
    const char* s = "192.168.1.5";
    CHECK(Storage::set("k_str", s, strlen(s) + 1));
    char sb[32] = {};
    CHECK(Storage::get("k_str", sb, sizeof(sb), &n));
    CHECK(n == strlen(s) + 1);
    CHECK(strcmp(sb, s) == 0);

    // Missing key.
    CHECK_FALSE(Storage::get("k_gone", sb, sizeof(sb), &n));
}

TEST_CASE("storage: value larger than the caller buffer fails cleanly") {
    StoreEnv env;
    uint8_t big[10];
    for (int i = 0; i < 10; i++) big[i] = (uint8_t)i;
    CHECK(Storage::set("k_big", big, sizeof(big)));

    uint8_t small[4] = {};
    size_t n = 0;
    CHECK_FALSE(Storage::get("k_big", small, sizeof(small), &n));
}

TEST_CASE("storage: remove is idempotent") {
    StoreEnv env;
    uint8_t x = 7;
    CHECK(Storage::set("k_scalar", &x, 1));
    CHECK(Storage::remove("k_scalar"));
    size_t n = 0;
    CHECK_FALSE(Storage::get("k_scalar", &x, 1, &n));
    CHECK(Storage::remove("k_scalar"));   // already gone: still ok
}

TEST_CASE("storage: bad keys rejected") {
    StoreEnv env;
    uint8_t x = 1;
    CHECK_FALSE(Storage::set("", &x, 1));
    CHECK_FALSE(Storage::set("has/slash", &x, 1));
    size_t n = 0;
    CHECK_FALSE(Storage::get("has/slash", &x, 1, &n));
}
