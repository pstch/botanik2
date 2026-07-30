// REVIEW_35: TODO
/**
 * @file storage.cpp
 * @brief HAL::Storage backends: Mbed KVStore on OPTA, a single external file (one
 *        "key=hex" record per line) emulating the KV store on the simulator. The
 *        TARGET_OPTA / TARGET_SIMULATOR split is legal here: this is the HAL.
 */
#include "libbotanik/hal/storage.h"

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
    }  // namespace

}}}  // namespace Botanik::HAL::Storage

#ifdef TARGET_OPTA
// ---- OPTA: not yet wired to flash -------------------------------------------
// HAL::Storage backs LocalParamHandler, used only when BOTANIK_FEATURE_PLC is
// off; the reference site (gh_mlb41) runs PLC-on and never calls this, so the
// OPTA firmware links without a flash backend. A PLC-less OPTA site should wire
// this to the Mbed global KVStore (internal-flash TDBStore) - the module ships
// with the core but is not on this build's curated include path and needs a
// configured default kvstore, so it must be enabled + validated on hardware:
//
//   #include "kvstore_global_api/kvstore_global_api.h"
//   set:    return kv_set(key, buf, len, 0) == MBED_SUCCESS;
//   get:    size_t n; if (kv_get(key, buf, cap, &n)) return false; *out_len = n;
//   remove: int rc = kv_remove(key); return rc == MBED_SUCCESS || rc == MBED_ERROR_ITEM_NOT_FOUND;

namespace Botanik { namespace HAL { namespace Storage {

    bool set(const char* key, const void*, size_t)    { (void)key_ok(key); return false; }
    bool get(const char* key, void*, size_t, size_t*) { (void)key_ok(key); return false; }
    bool remove(const char* key)                      { return key_ok(key); }

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
        if (o + klen + 1 + 2 * len + 1 > sizeof(out)) return false;
        for (size_t j = 0; j < klen; j++) out[o++] = key[j];
        out[o++] = '=';
        const uint8_t* v = static_cast<const uint8_t*>(buf);
        for (size_t j = 0; j < len; j++) {
            out[o++] = hex_digit((unsigned)(v[j] >> 4));
            out[o++] = hex_digit((unsigned)(v[j] & 0xF));
        }
        out[o++] = '\n';
        return store(out, o);
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
        return store(out, o);
    }

}}}  // namespace Botanik::HAL::Storage

#endif // TARGET_OPTA / simulator
