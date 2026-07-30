// REVIEW_35: TODO
#ifndef HAL_STORAGE_H
#define HAL_STORAGE_H

/**
 * @file storage.h
 * @brief Minimal HAL key-value persistence for configuration held off the PLC.
 *
 * Portable surface used by LocalParamHandler when BOTANIK_FEATURE_PLC is off
 * (the PLC otherwise owns config in its own NVM). Per-key raw-byte blobs that
 * survive a reboot. The two backends live in storage.cpp: Mbed KVStore on the
 * OPTA (internal-flash TDBStore), and a single external file (one `key=hex`
 * record per line) emulating that KV store on the simulator. No Mbed or POSIX
 * headers leak through this header; the TARGET_OPTA / TARGET_SIMULATOR split is
 * legal here (this file is the HAL).
 *
 * Keys are short identifiers (the flat Param names, e.g. "ntp_poll_s");
 * they must be non-empty and contain no '/'. Values are the parameter's raw
 * bytes (scalar width or string capacity).
 */

#include <cstddef>

namespace Botanik { namespace HAL { namespace Storage {

    /** @brief Persist @p len bytes at @p buf under @p key (overwriting any prior
     *  value). @return false on a bad key or a backend error. */
    bool set(const char* key, const void* buf, size_t len);

    /** @brief Read @p key into @p buf (at most @p cap bytes), reporting the
     *  stored length in @p out_len (may be null). @return false if the key is
     *  missing, the value does not fit in @p cap, or on a backend error. */
    bool get(const char* key, void* buf, size_t cap, size_t* out_len);

    /** @brief Delete @p key if present (idempotent). @return false only on a
     *  backend error, not on a missing key. */
    bool remove(const char* key);

}}}  // namespace Botanik::HAL::Storage

#endif // HAL_STORAGE_H
