// REVIEW_35: TODO
#ifndef LIBBOTANIK_CORE_CONFIG_H
#define LIBBOTANIK_CORE_CONFIG_H

/**
 * @file config.h
 * @brief Configuration parameter model - the ConfigParam descriptor table and
 *        the ConfigHandler base that owns a flat config image.
 *
 * Configuration is a flat namespace of parameters (network/NTP/MQTT/geo today),
 * the writable sibling of the telemetry "status variable table". The image is
 * the flat PlcIO::PlcOutputConfig_t; a ConfigParam[] table (library built-ins +
 * an optional site table) maps each parameter name to its byte offset, type and
 * bounds, so the CONFIG page and the backends read/render/validate/write any
 * parameter generically - no per-field code (mirrors the telemetry ScalarField
 * table and the alarm-condition table).
 *
 * ConfigHandler is the storage-backend base. Two concrete backends derive it
 * (built in a later step): PlcConfigHandler (the PLC owns the values in NVM) and
 * LocalConfigHandler (Mbed flash / a sim file). Both expose the same image() +
 * set() surface, so the config-driven Components and the CONFIG page are backend
 * agnostic. Its own ConfigType (not the network ScalarType) keeps this always-on
 * core header free of the network-gated telemetry include.
 */

#include <cstddef>   // offsetof, size_t
#include <cstdint>

#include "libbotanik/core/component.h"
#include "libbotanik/plcio/config.h"   // PlcOutputConfig_t (the config image)

namespace Botanik { namespace Cfg {

    /** @name Parameter descriptor
     *  @{ */

    /** @brief How to read + render a ConfigParam's bytes (its width and sign);
     *  CFG_ENUM is a u8 shown through ConfigParam::str, CFG_STRING a NUL-
     *  terminated char buffer. */
    enum ConfigType {
        CFG_U8, CFG_U16, CFG_U32,   ///< unsigned integer
        CFG_I16, CFG_I32,           ///< signed integer
        CFG_ENUM,                   ///< u8 -> string via ConfigParam::str
        CFG_STRING                  ///< NUL-terminated text (e.g. an IP or host)
    };

    /** @brief Parameter group. Only used to pick which `<group>_serial` word to
     *  bump on a committed edit (the consumers re-apply per group, not per
     *  parameter). Order is independent of the flat parameter order. */
    enum ConfigGroup {
        CFG_NET, CFG_NTP, CFG_MQTT, CFG_GEO,
        CFG_GROUP_COUNT
    };

    /** @brief How to validate a CFG_STRING parameter's edited text (STR_NONE for
     *  non-string params). The *_OPT variants also accept an empty string (an
     *  unset optional address, e.g. a second DNS server). */
    enum StrKind {
        STR_NONE,
        STR_IPV4, STR_IPV4_OPT,
        STR_HOST, STR_HOST_OPT
    };

    /** @brief One configuration parameter: a flat, PLC-matching @p name at byte
     *  @p offset in the config image, its @p type, display scaling and edit
     *  bounds. Numeric types validate against [@p min, @p max]; CFG_STRING uses
     *  @p capacity (bytes incl. NUL) and a type-specific validator. @p group
     *  selects the serial bumped on commit. Static, site-/library-defined; the
     *  handler holds no per-parameter state. */
    struct ConfigParam {
        const char* name;       ///< flat name, == the image field name
        uint16_t    offset;     ///< byte offset into PlcOutputConfig_t
        ConfigType  type;
        uint32_t    divisor;    ///< 0/1 = integer; else fixed-point (value/divisor)
        uint8_t     decimals;   ///< fixed-point precision (divisor > 1)
        uint8_t     group;      ///< ConfigGroup: which <group>_serial to bump
        int32_t     min;        ///< inclusive lower bound (numeric types)
        int32_t     max;        ///< inclusive upper bound (numeric types)
        uint16_t    capacity;   ///< CFG_STRING buffer size incl. NUL (else 0)
        uint8_t     str_kind;   ///< StrKind: CFG_STRING validator (else STR_NONE)
        const char* (*str)(uint8_t);   ///< CFG_ENUM only (else null)
    };

    /** @brief The library built-in parameters (net/ntp/mqtt/geo), flat. */
    extern const ConfigParam CONFIG_PARAMS[];
    extern const uint8_t     CONFIG_PARAM_COUNT;

    /** @} */

    /** @name Generic read / render / validate (no handler instance needed)
     *  @{ */

    /** @brief True for the text (CFG_STRING) type. */
    inline bool is_string(const ConfigParam& p) { return p.type == CFG_STRING; }

    /** @brief Stored width of a parameter's value in the image (its scalar width,
     *  or the string capacity). */
    size_t param_width(const ConfigParam& p);

    /** @brief Byte offset of a group's serial word in the config image. */
    uint16_t group_serial_offset(uint8_t group);

    /** @brief The contiguous byte range [*start, *start + *len) of a group's
     *  fields in the image (its serial leads the range). Groups are laid out
     *  contiguously, so a whole group can be copied in one memcpy. */
    void group_byte_range(uint8_t group, uint16_t* start, uint16_t* len);

    /** @brief Validate CFG_STRING edit text @p s per the parameter's StrKind
     *  (IPv4 or hostname, required or optional). */
    bool validate_string(const ConfigParam& p, const char* s);

    /** @brief Read a numeric parameter (any non-string type) from @p base as a
     *  sign-extended int32. Undefined for CFG_STRING. */
    int32_t read_scalar(const ConfigParam& p, const void* base);

    /** @brief Pointer to a CFG_STRING parameter's text within @p base. */
    const char* read_string(const ConfigParam& p, const void* base);

    /** @brief Render parameter @p p (read from @p base) as display text into
     *  @p buf: enum via str(), fixed-point via divisor/decimals, string
     *  verbatim, else a plain integer. @return the written length. */
    int format_value(const ConfigParam& p, const void* base, char* buf, size_t cap);

    /** @brief Validate a numeric candidate @p v against [min, max]. */
    bool validate_numeric(const ConfigParam& p, int32_t v);

    /** @brief Parse edit text @p s for parameter @p p into @p out (numeric
     *  types only) and bounds-check it. @return false on a malformed number or
     *  an out-of-range value. */
    bool parse_scalar(const ConfigParam& p, const char* s, int32_t* out);

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

        /** @} */

    protected:
        /** @name Shared write path
         *  @{ */

        /** @brief Parse + validate edit text @p text for parameter @p p and
         *  write its bytes into @p base at p.offset (numeric by width, string
         *  copied and NUL-padded to capacity). @return false (nothing written)
         *  on invalid input. Backends call this against their writable target
         *  (the local image, or the PLC input mirror). */
        static bool write_value(const ConfigParam& p, const char* text, void* base);

        /** @} */
    };

}}

#endif // LIBBOTANIK_CORE_CONFIG_H
