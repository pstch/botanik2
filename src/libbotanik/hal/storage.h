// REVIEW_35: TODO
#ifndef HAL_STORAGE_H
#define HAL_STORAGE_H

/**
 * @file storage.h
 * @brief Minimal HAL key-value persistence for configuration parameters.
 *
 * Per-key raw-byte blobs that survive a reboot; this is where the config
 * parameters live. The two backends are in storage.cpp: the Mbed global KV
 * store on the OPTA, and a single external file (one `key=hex` record per line)
 * emulating it on the simulator. No Mbed or POSIX headers leak through this
 * header; the TARGET_OPTA / TARGET_SIMULATOR split is legal here (this file is
 * the HAL).
 *
 * Which physical device backs the OPTA store - internal flash or the QSPI - is
 * an Mbed config decision, not ours, and has not been confirmed on hardware.
 * The `storage` shell command exists to check it with a set / power-cycle / get
 * round-trip.
 *
 * Keys are short identifiers (the flat Param names, e.g. "ntp_poll_s"); they
 * must be non-empty and contain no '/', '=' or newline. The OPTA backend
 * prepends Mbed's partition prefix itself. Values are the parameter's raw bytes
 * (scalar width or string capacity).
 */

#include <cstddef>
#include <cstdint>

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

    /** @brief Backend write failures since boot, monotonic.
     *
     *  Counts set() and remove() calls the backend refused - not a bad key
     *  (a programming error) and not a missing key on get() (routine). The
     *  HealthMonitor reads this each beat to raise FLASH_FLT, so a store that
     *  will not accept writes surfaces as a fault rather than as settings that
     *  quietly revert at the next reboot. */
    uint32_t fault_count();

}}}  // namespace Botanik::HAL::Storage

#endif // HAL_STORAGE_H
