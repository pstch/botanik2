/**
 * @file test_storage.cpp
 * @brief The simulator HAL::Storage backend (single external KV file).
 *
 * Each case points $BOTANIK_CONFIG_STORE at a throwaway file, starting empty.
 */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "libbotanik_config.h"

#include "libbotanik/hal/storage.h"

#include "doctest.h"

using namespace Botanik::HAL;
namespace Config = Botanik::Config;

namespace {
    struct StoreEnv {
        char path[64];
        StoreEnv() {
            snprintf(path, sizeof(path), "/tmp/botanik_test_store_%ld", (long)getpid());
            setenv("BOTANIK_CONFIG_STORE", path, 1);
            unlink(path);
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

    const char* s = "192.168.1.5";
    CHECK(Storage::set("k_str", s, strlen(s) + 1));
    char sb[32] = {};
    CHECK(Storage::get("k_str", sb, sizeof(sb), &n));
    CHECK(n == strlen(s) + 1);
    CHECK(strcmp(sb, s) == 0);

    CHECK_FALSE(Storage::get("k_gone", sb, sizeof(sb), &n));
}

// Notes run kilobytes (hex-encoded, twice over); must not evict or be evicted by a parameter.
TEST_CASE("storage: a note-sized value survives the round-trip") {
    StoreEnv env;

    static uint8_t note[Config::NOTES_LEN];
    for (size_t i = 0; i < sizeof(note); i++) note[i] = (uint8_t)(i * 31 + 7);
    CHECK(Storage::set("notes", note, sizeof(note)));

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

// Refused rather than truncated past the file cap, and counted for FLASH_FLT.
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

// Rewritten whole on every write: an oversized write must refuse without touching what's committed.
TEST_CASE("storage: a write that would not fit refuses, and keeps what is there") {
    StoreEnv env;

    static uint8_t blob[Config::STORE_MAX_BYTES / 8];
    memset(blob, 0x5A, sizeof(blob));
    REQUIRE(Storage::set("keep_a", blob, sizeof(blob)));
    REQUIRE(Storage::set("keep_b", blob, sizeof(blob)));
    REQUIRE(Storage::set("keep_c", blob, sizeof(blob)));

    static uint8_t rest[Config::STORE_MAX_BYTES / 2];   // cannot fit alongside them
    memset(rest, 0x33, sizeof(rest));
    const uint32_t before = Storage::fault_count();
    CHECK_FALSE(Storage::set("too_big", rest, sizeof(rest)));
    CHECK(Storage::fault_count() > before);          // counted, so FLASH_FLT can see it

    static uint8_t back[Config::STORE_MAX_BYTES / 8];
    size_t n = 0;
    static const char* const KEPT[] = { "keep_a", "keep_b", "keep_c" };
    for (const char* k : KEPT) {
        INFO("record ", k, " must survive a refused write");
        memset(back, 0, sizeof(back));
        REQUIRE(Storage::get(k, back, sizeof(back), &n));
        CHECK(n == sizeof(blob));
        CHECK(back[0] == 0x5A);
        CHECK(back[n - 1] == 0x5A);
    }
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

// fault_count() drives FLASH_FLT: real backend refusals only, not a malformed key.
TEST_CASE("storage: a refused write is counted, and a bad key is not") {
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
