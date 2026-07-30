// REVIEW_35: TODO
#ifndef LIBBOTANIK_CORE_CONFIG_H
#define LIBBOTANIK_CORE_CONFIG_H

/**
 * @file config.h
 * @brief Configuration parameter model - the ConfigHandler base over a flat
 *        config image, addressed by the same ScalarField descriptor the
 *        status-variable table uses.
 *
 * Configuration is a flat namespace of parameters (network/NTP/MQTT/geo today).
 * A parameter is just a writable ScalarField: `ConfigParam` is an alias for it,
 * so status variables and config parameters share one descriptor. The value is
 * read from the parameter's @c out_offset in the config image (the site's PLC
 * output payload) and a committed edit is written to its @c in_offset in the
 * input payload mirror; the two are distinct site-defined layouts. The site
 * declares the whole ScalarField[] table (the library ships none) and it drives
 * the CONFIG page and the backends generically - no per-field code.
 *
 * ConfigHandler is the storage-backend base. Two concrete backends derive it:
 * PlcConfigHandler (the PLC owns the values in NVM) and LocalConfigHandler (Mbed
 * flash / a sim file). Both expose the same image() + set() surface, so the
 * config-driven Components and the CONFIG page are backend agnostic.
 */

#include <cstddef>   // offsetof, size_t
#include <cstdint>
#include <cstring>   // strlen/strncmp (find by name)

#include "libbotanik/core/component.h"
#include "libbotanik/core/scalar.h"  // ScalarField / ScalarType (the shared descriptor)

namespace Botanik { namespace Cfg {

    /** @name Parameter descriptor
     *  @{ */

    /** @brief A config parameter is a writable ScalarField (in_offset >=
     *  0). One descriptor for both status variables and parameters. The site
     *  declares every parameter over its own PLC input/output payload; the
     *  library ships no built-in table and reads what it needs by name. */
    using ConfigParam = ScalarField;

    /** @brief How to validate a string parameter's edited text (STR_NONE for
     *  non-string params). The *_OPT variants also accept an empty string (an
     *  unset optional address, e.g. a second DNS server). Held in
     *  ScalarField::str_kind as a plain byte. */
    enum StrKind {
        STR_NONE,
        STR_IPV4, STR_IPV4_OPT,
        STR_HOST, STR_HOST_OPT
    };

    /** @} */

    /** @name Generic read / render / validate (no handler instance needed)
     *  @{ */

    /** @brief True for the text (SCALAR_STRING) type. */
    inline bool is_string(const ConfigParam& p) { return p.type == SCALAR_STRING; }

    /** @brief Short display name for a parameter's type ("u8"/"i32"/"f32"/"enum"/
     *  "str", "b" for a 0/1 boolean). */
    const char* type_name(const ConfigParam& p);

    /** @brief Stored width of a parameter's value in the image (its scalar width,
     *  or the string capacity). */
    size_t param_width(const ConfigParam& p);

    /** @brief Validate SCALAR_STRING edit text @p s per the parameter's StrKind
     *  (IPv4 or hostname, required or optional). */
    bool validate_string(const ConfigParam& p, const char* s);

    /** @brief Read a numeric parameter (any non-string type) from @p base at its
     *  out_offset as a sign-extended int32. Undefined for SCALAR_STRING. */
    int32_t read_scalar(const ConfigParam& p, const void* base);

    /** @brief Render parameter @p p (read from @p base) as display text into
     *  @p buf: enum via str(), f32 to `digits` decimals, string verbatim, else a plain
     *  integer. @return the written length. */
    int format_value(const ConfigParam& p, const void* base, char* buf, size_t cap);

    /** @brief Parse edit text @p s for parameter @p p into @p out (integer types
     *  only) and bounds-check it. @return false on a malformed number or an
     *  out-of-range value. */
    bool parse_scalar(const ConfigParam& p, const char* s, int32_t* out);

    /** @brief Parse edit text @p s for an f32 parameter into @p out and
     *  bounds-check it against [min, max]. @return false on malformed / OOR. */
    bool parse_f32(const ConfigParam& p, const char* s, float* out);

    /** @brief True if @p s is a dotted-decimal IPv4 literal (four 0..255
     *  octets). An empty string is allowed only when @p allow_empty (an unset
     *  optional address, e.g. a second DNS server). */
    bool validate_ipv4(const char* s, bool allow_empty = false);

    /** @brief True if @p s is a plausible hostname (letters/digits/'-'/'.',
     *  non-empty, within @p cap). An empty string is allowed only when
     *  @p allow_empty. */
    bool validate_hostname(const char* s, uint16_t cap, bool allow_empty = false);

    /** @} */

    /** @brief Configuration storage-backend base: owns the parameter registry
     *  (the site table) and the generic read/render/validate/write surface over
     *  a flat config image. Concrete backends supply the image and the write
     *  path. The image is a byte blob addressed by each parameter's offsets - the
     *  site's PLC output payload for a live PLC, a RAM buffer for a local build. */
    class ConfigHandler : public Component {

        /** @name Parameter registry
         *  @{ */

        /** @brief Site parameter table (may be null). Registered via
         *  set_site_params(); must outlive the handler. */
        const ConfigParam* _site       = nullptr;
        uint8_t            _site_count  = 0;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        explicit ConfigHandler(Logs::Logger* logger)
            : Component(logger, "Config") {}

        /** @} */

        /** @name Parameter access
         *  @{ */

        /** @brief Register the site's parameter table. @p table must outlive the
         *  handler; count 0 clears. */
        void set_site_params(const ConfigParam* table, uint8_t count) {
            _site = table; _site_count = count;
        }

        /** @brief Total parameter count. */
        uint8_t count() const { return _site_count; }

        /** @brief The @p i-th parameter. */
        const ConfigParam& param(uint8_t i) const { return _site[i]; }

        /** @} */

        /** @name Backend surface (image + write)
         *  @{ */

        /** @brief The current config image (backend-owned; stable for the
         *  handler's lifetime). Never null. Addressed by parameter byte offset. */
        virtual const void* image() const = 0;

        /** @brief Apply an edited value (as display text) to parameter @p p:
         *  parse + validate, then persist through the backend. @return false
         *  (image unchanged) on invalid input. */
        virtual bool set(const ConfigParam& p, const char* text) = 0;

        /** @brief Render parameter @p i's current value into @p buf. */
        int format(uint8_t i, char* buf, size_t cap) const {
            const void* img = image();
            if (img == nullptr) { if (cap) buf[0] = '\0'; return 0; }
            return format_value(param(i), img, buf, cap);
        }

        /** @brief Render a given parameter's current value into @p buf. */
        int format_param(const ConfigParam& p, char* buf, size_t cap) const {
            const void* img = image();
            if (img == nullptr) { if (cap) buf[0] = '\0'; return 0; }
            return format_value(p, img, buf, cap);
        }

        /** @brief Find a parameter by name, or null. */
        const ConfigParam* find(const char* name, uint16_t len) const {
            for (uint8_t i = 0; i < count(); i++) {
                const ConfigParam& p = param(i);
                if (p.name != nullptr &&
                    std::strlen(p.name) == len && std::strncmp(p.name, name, len) == 0)
                    return &p;
            }
            return nullptr;
        }

        /** @} */

    protected:
        /** @name Shared write path
         *  @{ */

        /** @brief Parse + validate edit text @p text for parameter @p p and
         *  write its bytes into @p base at byte @p offset (numeric by width,
         *  string copied and NUL-padded to capacity). @return false (nothing
         *  written) on invalid input or a read-only parameter. A PLC backend
         *  passes the input-mirror @c in_offset; a local backend the single-image
         *  @c out_offset. */
        static bool write_value(const ConfigParam& p, const char* text, void* base, int16_t offset);

        /** @} */
    };

    /** @brief The config image behind a handler, or null if @p h is null. Shared
     *  by the config-driven components so each keeps no null-guard of its own. */
    inline const void* config_image(const ConfigHandler* h) {
        return h != nullptr ? h->image() : nullptr;
    }

    /** @name Read config by parameter name (null-safe)
     *
     *  The config-driven library components (network / NTP / MQTT / sun) source
     *  their values by *name*, not by dereferencing a typed struct, so the
     *  parameters can live anywhere in the site payload the descriptor points at.
     *  Each is null-safe (a null handler / missing parameter yields the default),
     *  mirroring @ref config_image.
     *  @{ */

    /** @brief Find a parameter by NUL-terminated name (null handler -> null). */
    inline const ConfigParam* cfg_find(const ConfigHandler* h, const char* name) {
        return h != nullptr ? h->find(name, (uint16_t)std::strlen(name)) : nullptr;
    }

    /** @brief Read a numeric parameter by name as int32 (@p dflt if absent). */
    inline int32_t cfg_int(const ConfigHandler* h, const char* name, int32_t dflt = 0) {
        const ConfigParam* p = cfg_find(h, name);
        const void* img = (p != nullptr) ? h->image() : nullptr;
        return img != nullptr ? read_scalar(*p, img) : dflt;
    }

    /** @brief Read an SCALAR_F32 parameter by name (@p dflt if absent / wrong type). */
    inline float cfg_f32(const ConfigHandler* h, const char* name, float dflt = 0.0f) {
        const ConfigParam* p = cfg_find(h, name);
        const void* img = (p != nullptr && p->type == SCALAR_F32) ? h->image() : nullptr;
        return img != nullptr ? scalar_read_f32(*p, img) : dflt;
    }

    /** @brief Pointer to an SCALAR_STRING parameter's text by name ("" if absent).
     *  Valid for the handler's lifetime (points into its image). */
    inline const char* cfg_str(const ConfigHandler* h, const char* name) {
        const ConfigParam* p = cfg_find(h, name);
        const void* img = (p != nullptr && p->type == SCALAR_STRING) ? h->image() : nullptr;
        return img != nullptr ? scalar_read_string(*p, img) : "";
    }

    /** @brief The change serial for a config group: the value of the parameter
     *  named @p group (its commit serial), read from the output image. 0 if
     *  absent. A consumer watches this to re-apply after the user commits an
     *  edit by bumping that serial. */
    inline uint32_t cfg_group_serial(const ConfigHandler* h, const char* group) {
        const ConfigParam* p = cfg_find(h, group);
        const void* img = (p != nullptr) ? h->image() : nullptr;
        return img != nullptr ? (uint32_t)read_scalar(*p, img) : 0;
    }

    /** @} */

}}

#endif // LIBBOTANIK_CORE_CONFIG_H
