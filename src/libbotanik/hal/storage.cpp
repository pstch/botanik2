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
// ---- OPTA: not yet wired to flash -------------------------------------------
// HAL::Storage backs LocalConfigHandler, used only when BOTANIK_FEATURE_PLC is
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
