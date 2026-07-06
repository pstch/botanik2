// REVIEW_35: TODO
/**
 * @file storage.cpp
 * @brief HAL::Storage backends: Mbed KVStore on OPTA, a per-process directory of
 *        one-file-per-key on the simulator. The TARGET_OPTA / TARGET_SIMULATOR
 *        split is legal here: this file is part of the HAL.
 */
#include "libbotanik/hal/storage.h"

#include <cstring>

namespace Botanik { namespace HAL { namespace Storage {

    namespace {
        // A valid key is non-empty and path-safe (no '/', so it maps to a single
        // file / a flat KVStore entry without traversal).
        bool key_ok(const char* key) {
            if (key == nullptr || key[0] == 0) return false;
            for (const char* p = key; *p != 0; ++p)
                if (*p == '/') return false;
            return true;
        }
    }  // namespace

}}}  // namespace Botanik::HAL::Storage

#ifdef TARGET_OPTA
// ---- OPTA: Mbed global KVStore (internal-flash TDBStore) --------------------
// NOTE: compiled only on the OPTA build (needs Mbed KVStore configured); not
// exercised by the host test suite. Validate on hardware.
#include "kvstore_global_api/kvstore_global_api.h"

namespace Botanik { namespace HAL { namespace Storage {

    bool set(const char* key, const void* buf, size_t len) {
        if (!key_ok(key)) return false;
        return kv_set(key, buf, len, 0) == MBED_SUCCESS;
    }

    bool get(const char* key, void* buf, size_t cap, size_t* out_len) {
        if (!key_ok(key)) return false;
        size_t actual = 0;
        if (kv_get(key, buf, cap, &actual) != MBED_SUCCESS) return false;
        if (out_len != nullptr) *out_len = actual;
        return true;
    }

    bool remove(const char* key) {
        if (!key_ok(key)) return false;
        int rc = kv_remove(key);
        return rc == MBED_SUCCESS || rc == MBED_ERROR_ITEM_NOT_FOUND;
    }

}}}  // namespace Botanik::HAL::Storage

#else
// ---- Simulator: one file per key under a per-process directory --------------
// The directory is derived from the PID, so concurrent sims stay isolated and a
// clean launch starts empty, while an in-process execve reboot (same PID) keeps
// the store. Override the location with $BOTANIK_CONFIG_STORE (used by tests).
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>

namespace Botanik { namespace HAL { namespace Storage {

    namespace {
        const char* store_dir() {
            const char* env = getenv("BOTANIK_CONFIG_STORE");
            if (env != nullptr && env[0] != 0) return env;
            static char buf[64];
            static bool built = false;
            if (!built) {
                snprintf(buf, sizeof(buf), "/tmp/botanik_cfg_%ld", (long)getpid());
                built = true;
            }
            return buf;
        }

        bool key_path(const char* key, char* out, size_t cap) {
            int n = snprintf(out, cap, "%s/%s", store_dir(), key);
            return n > 0 && (size_t)n < cap;
        }
    }  // namespace

    bool set(const char* key, const void* buf, size_t len) {
        if (!key_ok(key)) return false;
        mkdir(store_dir(), 0700);   // ignore EEXIST; fopen below reports real failures
        char path[256];
        if (!key_path(key, path, sizeof(path))) return false;
        FILE* f = fopen(path, "wb");
        if (f == nullptr) return false;
        size_t w = (len > 0) ? fwrite(buf, 1, len, f) : 0;
        bool ok = (w == len);
        return (fclose(f) == 0) && ok;
    }

    bool get(const char* key, void* buf, size_t cap, size_t* out_len) {
        if (!key_ok(key)) return false;
        char path[256];
        if (!key_path(key, path, sizeof(path))) return false;
        FILE* f = fopen(path, "rb");
        if (f == nullptr) return false;
        size_t n = (cap > 0) ? fread(buf, 1, cap, f) : 0;
        // A byte still readable means the stored value is larger than cap: the
        // caller's buffer can't hold it, so report failure (partial read).
        bool overflow = (fgetc(f) != EOF);
        fclose(f);
        if (overflow) return false;
        if (out_len != nullptr) *out_len = n;
        return true;
    }

    bool remove(const char* key) {
        if (!key_ok(key)) return false;
        char path[256];
        if (!key_path(key, path, sizeof(path))) return false;
        unlink(path);   // missing key is not an error
        return true;
    }

}}}  // namespace Botanik::HAL::Storage

#endif // TARGET_OPTA / simulator
