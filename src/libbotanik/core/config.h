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
 * A parameter is just a writable ScalarField: `ConfigParam` is an alias
 * for it, so status variables and config parameters share one descriptor. The
 * value is read from the parameter's @c out_offset in the flat
 * PlcIO::PlcOutputConfig_t image and a committed edit is written to its
 * @c in_offset in the input mirror (same layout, so in == out for a real param).
 * A ScalarField[] table (library built-ins + an optional site table) drives the
 * CONFIG page and the backends generically - no per-field code.
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
#include "libbotanik/plcio/config.h"           // PlcOutputConfig_t (the config image)
#include "libbotanik/core/scalar.h"  // ScalarField / ScalarType (the shared descriptor)

namespace Botanik { namespace Cfg {

    /** @name Parameter descriptor
     *  @{ */

    /** @brief A config parameter is a writable ScalarField (in_offset >=
     *  0). One descriptor for both status variables and parameters. */
    using ConfigParam = ScalarField;

    /** @brief Parameter group. Only used to pick which `<group>_serial` word to
     *  bump on a committed edit (the consumers re-apply per group, not per
     *  parameter). Held in ScalarField::group as a plain byte. */
    enum ConfigGroup {
        CFG_NET, CFG_NTP, CFG_MQTT, CFG_GEO,
        CFG_GROUP_COUNT
    };

    /** @brief How to validate a string parameter's edited text (STR_NONE for
     *  non-string params). The *_OPT variants also accept an empty string (an
     *  unset optional address, e.g. a second DNS server). Held in
     *  ScalarField::str_kind as a plain byte. */
    enum StrKind {
        STR_NONE,
        STR_IPV4, STR_IPV4_OPT,
        STR_HOST, STR_HOST_OPT
    };

    /** @brief The library built-in parameters (net/ntp/mqtt/geo), flat. */
    extern const ConfigParam CONFIG_PARAMS[];
    extern const uint8_t     CONFIG_PARAM_COUNT;

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

    /** @brief Byte offset of a group's serial word in the config image. */
    uint16_t group_serial_offset(uint8_t group);

    /** @brief The contiguous byte range [*start, *start + *len) of a group's
     *  fields in the image (its serial leads the range). Groups are laid out
     *  contiguously, so a whole group can be copied in one memcpy. */
    void group_byte_range(uint8_t group, uint16_t* start, uint16_t* len);

    /** @brief Validate SCALAR_STRING edit text @p s per the parameter's StrKind
     *  (IPv4 or hostname, required or optional). */
    bool validate_string(const ConfigParam& p, const char* s);

    /** @brief Read a numeric parameter (any non-string type) from @p base at its
     *  out_offset as a sign-extended int32. Undefined for SCALAR_STRING. */
    int32_t read_scalar(const ConfigParam& p, const void* base);

    /** @brief Render parameter @p p (read from @p base) as display text into
     *  @p buf: enum via str(), f32 via format_f32, string verbatim, else a plain
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
     *  (library + optional site table) and the generic read/render/validate/
     *  write surface over a flat config image. Concrete backends supply the
     *  image and the write path. */
    class ConfigHandler : public Component {

        /** @name Parameter registry
         *  @{ */

        /** @brief Site parameter table appended after the library set (may be
         *  null). Registered via set_site_params(); must outlive the handler. */
        const ConfigParam* _site       = nullptr;
        uint8_t            _site_count  = 0;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        explicit ConfigHandler(Logs::Logger* logger)
            : Component(logger, "Config") {}

        /** @} */

        /** @name Parameter access (library table then site table)
         *  @{ */

        /** @brief Register the site's extra parameters, addressed after the
         *  library set. @p table must outlive the handler; count 0 clears. */
        void set_site_params(const ConfigParam* table, uint8_t count) {
            _site = table; _site_count = count;
        }

        /** @brief Total parameter count (library + site). */
        uint8_t count() const {
            return (uint8_t)(CONFIG_PARAM_COUNT + _site_count);
        }

        /** @brief The @p i-th parameter (library entries first, then site). */
        const ConfigParam& param(uint8_t i) const {
            return (i < CONFIG_PARAM_COUNT)
                ? CONFIG_PARAMS[i]
                : _site[i - CONFIG_PARAM_COUNT];
        }

        /** @} */

        /** @name Backend surface (image + write)
         *  @{ */

        /** @brief The current config image (backend-owned; stable for the
         *  handler's lifetime). Never null. */
        virtual const PlcIO::PlcOutputConfig_t* image() const = 0;

        /** @brief Apply an edited value (as display text) to parameter @p p:
         *  parse + validate, then persist through the backend and bump the
         *  parameter's group serial. @return false (image unchanged) on invalid
         *  input. */
        virtual bool set(const ConfigParam& p, const char* text) = 0;

        /** @brief Render parameter @p i's current value into @p buf. */
        int format(uint8_t i, char* buf, size_t cap) const {
            return format_value(param(i), image(), buf, cap);
        }

        /** @brief Render a given parameter's current value into @p buf. */
        int format_param(const ConfigParam& p, char* buf, size_t cap) const {
            return format_value(p, image(), buf, cap);
        }

        /** @brief Find a parameter by name (library + site), or null. */
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
         *  write its bytes into @p base at p.in_offset (numeric by width, string
         *  copied and NUL-padded to capacity). @return false (nothing written)
         *  on invalid input. Backends call this against their writable target
         *  (the local image, or the PLC input mirror). */
        static bool write_value(const ConfigParam& p, const char* text, void* base);

        /** @} */
    };

    /** @brief The config image behind a handler, or null if @p h is null. Shared
     *  by the config-driven components so each keeps no null-guard of its own
     *  (mirrors PlcIO::plc_config for the sync context). */
    inline const PlcIO::PlcOutputConfig_t* config_image(const ConfigHandler* h) {
        return h != nullptr ? h->image() : nullptr;
    }

}}

#endif // LIBBOTANIK_CORE_CONFIG_H
