// REVIEW_35: TODO
/**
 * @file storage.cpp
 * @brief HAL::Storage backends: Mbed KVStore on OPTA, a single external file (one
 *        "key=hex" record per line) emulating the KV store on the simulator. The
 *        TARGET_OPTA / TARGET_SIMULATOR split is legal here: this is the HAL.
 */
#include "storage.h"

#include <cstring>

namespace Botanik { namespace HAL { namespace Storage {

    namespace {
        // A valid key is a non-empty simple identifier: no '/' (path-safe / flat
        // KVStore entry), and no '=' or newline (the sim store's record
        // delimiters).
        bool key_ok(const char* key) {
            if (key == nullptr || key[0] == 0) return false;
            for (const char* p = key; *p != 0; ++p)
                if (*p == '/' || *p == '=' || *p == '\n') return false;
            return true;
        }

        /** @brief Backend write failures since boot. Not static-in-function:
         *  both backend blocks below reopen this namespace and increment it. */
        uint32_t g_faults = 0;

        /** @brief Tally a backend result. Only wraps calls that have already
         *  passed key_ok - a malformed key is a programming error, not a
         *  storage fault, and counting it would raise FLASH_FLT for a typo. */
        bool note(bool ok) {
            if (!ok && g_faults != 0xFFFFFFFFu) g_faults++;
            return ok;
        }
    }  // namespace

    uint32_t fault_count() { return g_faults; }

}}}  // namespace Botanik::HAL::Storage

#ifdef TARGET_OPTA
// ---- OPTA: the Mbed global KV store ------------------------------------------
// kv_set/kv_get/kv_remove resolve to the configured default kvstore, a TDBStore
// over BlockDevice::get_default_instance(). Which device that is - internal
// flash or the QSPI - is decided by the Mbed config, not by us; the variant
// ships STORAGE_TYPE=default with the TDB region base and size both 0, so Mbed
// places it. Confirm the placement on hardware before trusting it: the `storage`
// shell command exists for exactly that.
#include "platform/mbed_error.h"
#include "kvstore_global_api/kvstore_global_api.h"

namespace Botanik { namespace HAL { namespace Storage {

    namespace {
        // Mbed wants "/<partition>/<key>", while a HAL key is the bare
        // identifier (key_ok even rejects '/'). MBED_CONF_STORAGE_DEFAULT_KV is
        // the partition name; "kv" is the variant's value.
        const char PARTITION[] = "/kv/";
        // KVStore caps a full key at 128 chars including the partition.
        const size_t FULL_KEY_MAX = 129;

        bool full_key(char* out, size_t cap, const char* key) {
            if (!key_ok(key)) return false;
            const size_t plen = sizeof(PARTITION) - 1;
            const size_t klen = strlen(key);
            if (plen + klen + 1 > cap) return false;
            memcpy(out, PARTITION, plen);
            memcpy(out + plen, key, klen + 1);
            return true;
        }
    }  // namespace

    bool set(const char* key, const void* buf, size_t len) {
        char full[FULL_KEY_MAX];
        if (!full_key(full, sizeof(full), key)) return false;
        return note(kv_set(full, buf, len, 0) == MBED_SUCCESS);
    }

    bool get(const char* key, void* buf, size_t cap, size_t* out_len) {
        char full[FULL_KEY_MAX];
        if (!full_key(full, sizeof(full), key)) return false;
        size_t n = 0;
        if (kv_get(full, buf, cap, &n) != MBED_SUCCESS) return false;
        if (out_len != nullptr) *out_len = n;
        return true;
    }

    bool remove(const char* key) {
        char full[FULL_KEY_MAX];
        if (!full_key(full, sizeof(full), key)) return false;
        // Absent is not an error: remove() is documented idempotent.
        const int rc = kv_remove(full);
        return note(rc == MBED_SUCCESS || rc == MBED_ERROR_ITEM_NOT_FOUND);
    }

}}}  // namespace Botanik::HAL::Storage

#else
// ---- Simulator: one external file, "key=hex" one record per line ------------
// A single file emulates the OPTA's KV store (one flash region holding every
// key). It lives at a stable path so config persists across sim runs, like the
// real NVM; override with $BOTANIK_CONFIG_STORE (used by tests). Values are hex
// so arbitrary bytes survive a line-based text file. The whole store is small
// (a handful of config parameters), so each op rewrites it in full.
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace Botanik { namespace HAL { namespace Storage {

    namespace {
        constexpr size_t STORE_MAX = 8192;   // whole-file cap (config is tiny)

        const char* store_path() {
            const char* env = getenv("BOTANIK_CONFIG_STORE");
            if (env != nullptr && env[0] != 0) return env;
            static char buf[256];
            static bool built = false;
            if (!built) {
                const char* home = getenv("HOME");
                if (home != nullptr && home[0] != 0)
                    snprintf(buf, sizeof(buf), "%s/.botanik_config", home);
                else
                    snprintf(buf, sizeof(buf), "/tmp/botanik_config");
                built = true;
            }
            return buf;
        }

        // Read the whole store into buf; returns its length (0 if absent).
        size_t load(char* buf, size_t cap) {
            FILE* f = fopen(store_path(), "rb");
            if (f == nullptr) return 0;
            size_t n = fread(buf, 1, cap, f);
            fclose(f);
            return n;
        }

        bool store(const char* buf, size_t len) {
            FILE* f = fopen(store_path(), "wb");
            if (f == nullptr) return false;
            size_t w = (len > 0) ? fwrite(buf, 1, len, f) : 0;
            bool ok = (w == len);
            return (fclose(f) == 0) && ok;
        }

        char hex_digit(unsigned v) { return (char)(v < 10 ? '0' + v : 'a' + v - 10); }
        int  hex_val(char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        // Does the line s[ls..le) hold "<key>=..."?
        bool line_is_key(const char* s, size_t ls, size_t le,
                         const char* key, size_t klen) {
            return (le - ls > klen) && s[ls + klen] == '=' &&
                   memcmp(s + ls, key, klen) == 0;
        }

        // Append every line of the store except @p key's into @p out.
        size_t copy_without(const char* in, size_t n, const char* key,
                            size_t klen, char* out, size_t cap) {
            size_t o = 0, i = 0;
            while (i < n) {
                size_t ls = i;
                while (i < n && in[i] != '\n') i++;
                if (i < n) i++;                        // consume the newline
                size_t le = (i > ls && in[i - 1] == '\n') ? i - 1 : i;
                if (line_is_key(in, ls, le, key, klen)) continue;
                if (o + (i - ls) > cap) break;         // overflow: drop the tail
                memcpy(out + o, in + ls, i - ls);
                o += (i - ls);
            }
            return o;
        }
    }  // namespace

    bool set(const char* key, const void* buf, size_t len) {
        if (!key_ok(key)) return false;
        size_t klen = strlen(key);
        char in[STORE_MAX];
        size_t n = load(in, sizeof(in));
        char out[STORE_MAX];
        size_t o = copy_without(in, n, key, klen, out, sizeof(out));
        // Append "key=<hex>\n" (a raw record, not a NUL-terminated string).
        if (o + klen + 1 + 2 * len + 1 > sizeof(out)) return note(false);
        for (size_t j = 0; j < klen; j++) out[o++] = key[j];
        out[o++] = '=';
        const uint8_t* v = static_cast<const uint8_t*>(buf);
        for (size_t j = 0; j < len; j++) {
            out[o++] = hex_digit((unsigned)(v[j] >> 4));
            out[o++] = hex_digit((unsigned)(v[j] & 0xF));
        }
        out[o++] = '\n';
        return note(store(out, o));
    }

    bool get(const char* key, void* buf, size_t cap, size_t* out_len) {
        if (!key_ok(key)) return false;
        size_t klen = strlen(key);
        char in[STORE_MAX];
        size_t n = load(in, sizeof(in));
        size_t i = 0;
        while (i < n) {
            size_t ls = i;
            while (i < n && in[i] != '\n') i++;
            size_t le = i;
            if (i < n) i++;
            if (!line_is_key(in, ls, le, key, klen)) continue;
            size_t hs = ls + klen + 1, hlen = le - hs;
            if (hlen % 2 != 0) return false;           // malformed record
            size_t vlen = hlen / 2;
            if (vlen > cap) return false;              // caller's buffer too small
            uint8_t* dst = static_cast<uint8_t*>(buf);
            for (size_t j = 0; j < vlen; j++) {
                int hi = hex_val(in[hs + 2 * j]), lo = hex_val(in[hs + 2 * j + 1]);
                if (hi < 0 || lo < 0) return false;
                dst[j] = (uint8_t)((hi << 4) | lo);
            }
            if (out_len != nullptr) *out_len = vlen;
            return true;
        }
        return false;                                  // missing key
    }

    bool remove(const char* key) {
        if (!key_ok(key)) return false;
        size_t klen = strlen(key);
        char in[STORE_MAX];
        size_t n = load(in, sizeof(in));
        if (n == 0) return true;                       // nothing stored
        char out[STORE_MAX];
        size_t o = copy_without(in, n, key, klen, out, sizeof(out));
        if (o == n) return true;                        // key absent: idempotent
        return note(store(out, o));
    }

}}}  // namespace Botanik::HAL::Storage

#endif // TARGET_OPTA / simulator
