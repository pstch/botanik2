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
 * On the OPTA the store is a TDBStore on the EXTERNAL QSPI NOR flash. Arduino's
 * PlatformStorage defines BlockDevice::get_default_instance() as a
 * QSPIFBlockDevice, and the variant builds with COMPONENT_QSPIF and
 * STORAGE_TYPE=default, which selects TDB_EXTERNAL. So erase granularity is the
 * NOR's subsector, not the H747's 128 KB internal sector, and a value is bounded
 * by the region rather than by any per-key cap.
 *
 * @warning Placement is Mbed's default and has not been confirmed on hardware:
 * TDB_EXTERNAL_EXTERNAL_BASE_ADDRESS and _SIZE are both 0, so the store covers
 * the whole QSPI from offset 0 - the same flash Arduino's tooling partitions for
 * the WiFi firmware, OTA and a LittleFS user area. Pinning it needs an override
 * of the weak kv_init_storage_config(); the MBED_CONF_* macros are baked into
 * the prebuilt kv_config.o, so -D does nothing.
 *
 * Keys are short identifiers (the flat Param names, e.g. "ntp_poll_sec"); they
 * must be non-empty and contain no '/', '=' or newline. The OPTA backend
 * prepends Mbed's partition prefix itself. Values are the parameter's raw bytes
 * (scalar width or string capacity).
 */

#include <cstddef>
#include <cstdint>

namespace Botanik { namespace HAL { namespace Storage {

    /** @brief Longest key, NUL included, counting the partition prefix the
     *  backend adds. The PLATFORM sets it - Mbed's KVStore takes 128 characters
     *  - so it is fixed here rather than configured: every caller that formats a
     *  key ("ovr.<name>", "<name>_serial", ...) sizes its scratch from this one
     *  number, and a build that shrank it would truncate keys differently
     *  depending on which path wrote them. */
    static constexpr size_t KEY_MAX = 129;

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
