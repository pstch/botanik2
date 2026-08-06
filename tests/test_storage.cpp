/**
 * @file test_storage.cpp
 * @brief Tests for the simulator HAL::Storage backend (a single external KV
 *        file). Points the store at a throwaway per-test file via
 *        $BOTANIK_CONFIG_STORE and starts each case from empty.
 */
#include <cstdint>                  // uint8_t/uint32_t
#include <cstdio>                   // snprintf
#include <cstdlib>                  // setenv / unsetenv
#include <cstring>                  // memcmp / strlen
#include <unistd.h>                 // unlink

#include "libbotanik_config.h"      // Config::STORE_MAX_BYTES

#include "libbotanik/hal/storage.h" // HAL::Storage

#include "doctest.h"                // the test framework (vendored single header)

using namespace Botanik::HAL;
namespace Config = Botanik::Config;

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

// The operator's note is kilobytes, an order of magnitude past anything the
// store used to be asked for, and the sim backend hex-encodes into one file - so
// a note has to fit alongside every parameter, twice over.
TEST_CASE("storage: a note-sized value survives the round-trip") {
    StoreEnv env;

    static uint8_t note[Config::NOTES_LEN];
    for (size_t i = 0; i < sizeof(note); i++) note[i] = (uint8_t)(i * 31 + 7);
    CHECK(Storage::set("notes", note, sizeof(note)));

    // A parameter written afterwards must not evict it, nor be evicted by it.
    uint32_t p = 0x12345678;
    CHECK(Storage::set("ntp_poll_sec", &p, sizeof(p)));

    static uint8_t back[Config::NOTES_LEN];
    size_t n = 0;
    CHECK(Storage::get("notes", back, sizeof(back), &n));
    CHECK(n == sizeof(note));
    CHECK(memcmp(back, note, sizeof(note)) == 0);

    uint32_t pback = 0;
    CHECK(Storage::get("ntp_poll_sec", &pback, sizeof(pback), &n));
    CHECK(pback == p);
}

// Past the file cap the write is refused rather than truncated, and counted, so
// FLASH_FLT tells the operator the note did not reach flash.
TEST_CASE("storage: a value past the store cap is refused and counted") {
    StoreEnv env;

    static uint8_t huge[Config::STORE_MAX_BYTES];
    memset(huge, 0xA5, sizeof(huge));

    uint32_t before = Storage::fault_count();
    CHECK_FALSE(Storage::set("k_huge", huge, sizeof(huge)));   // hex doubles it
    CHECK(Storage::fault_count() == before + 1);

    size_t n = 0;
    CHECK_FALSE(Storage::get("k_huge", huge, sizeof(huge), &n));
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

TEST_CASE("storage: a refused write is counted; a bad key is not") {
    // fault_count() is what raises FLASH_FLT, so it must track real backend
    // refusals only. A malformed key is a programming error and must not put
    // the firmware into a persistent fault.
    const uint32_t before = Storage::fault_count();

    {   // a well-formed key the backend cannot write
        StoreEnv env;
        setenv("BOTANIK_CONFIG_STORE", "/nonexistent-dir-botanik/store", 1);
        CHECK_FALSE(Storage::set("ok_key", "v", 1));
        CHECK(Storage::fault_count() == before + 1);
    }

    {   // a malformed key never reaches the backend
        StoreEnv env;
        const uint32_t n = Storage::fault_count();
        CHECK_FALSE(Storage::set("bad/key", "v", 1));
        CHECK_FALSE(Storage::set("", "v", 1));
        CHECK(Storage::fault_count() == n);
    }

    {   // and a healthy store does not tick it
        StoreEnv env;
        const uint32_t n = Storage::fault_count();
        CHECK(Storage::set("good", "v", 1));
        CHECK(Storage::remove("good"));
        CHECK(Storage::fault_count() == n);
    }
}
