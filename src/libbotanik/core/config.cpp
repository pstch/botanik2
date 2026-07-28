// REVIEW_35: TODO
/**
 * @file config.cpp
 * @brief The library config-parameter table (ScalarField[]) + the
 *        generic read/render/validate helpers declared in config.h.
 */
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "libbotanik/core/config.h"
#include "libbotanik/utils/text.h"   // format_float / format_int

namespace Botanik { namespace Cfg {

    // Library built-in parameters, as writable ScalarFields. name == the
    // PlcOutputConfig_t field name; the value is read at out_offset and a commit
    // written at in_offset (== out_offset, since the input mirror shares the
    // layout). Fields: name, out_offset, in_offset, type, digits, str, unit,
    // desc, notes, default, min, max, capacity, str_kind, group.
    static const uint16_t HS = Config::PLC_HOST_STR_LEN;
#define CO(f) (int16_t)offsetof(PlcIO::PlcOutputConfig_t, f)
    const ConfigParam CONFIG_PARAMS[] = {
        // -- network --
        {"net_dhcp",        CO(net_dhcp),     CO(net_dhcp),     SCALAR_U8,     0, nullptr, "",    "DHCP (1) or static IP (0)", "",                 "1",            0, 1,      0,  STR_NONE,     CFG_NET},
        {"net_ip",          CO(net_ip),       CO(net_ip),       SCALAR_STRING, 0, nullptr, "",    "Static IPv4 address",       "used when DHCP off","",             0, 0,      HS, STR_IPV4,     CFG_NET},
        {"net_netmask",     CO(net_netmask),  CO(net_netmask),  SCALAR_STRING, 0, nullptr, "",    "Static subnet mask",        "used when DHCP off","",             0, 0,      HS, STR_IPV4,     CFG_NET},
        {"net_gateway",     CO(net_gateway),  CO(net_gateway),  SCALAR_STRING, 0, nullptr, "",    "Default gateway",           "used when DHCP off","",             0, 0,      HS, STR_IPV4,     CFG_NET},
        {"net_dns1",        CO(net_dns1),     CO(net_dns1),     SCALAR_STRING, 0, nullptr, "",    "Primary DNS server",        "",                 "",             0, 0,      HS, STR_IPV4,     CFG_NET},
        {"net_dns2",        CO(net_dns2),     CO(net_dns2),     SCALAR_STRING, 0, nullptr, "",    "Secondary DNS server",      "empty = none",     "",             0, 0,      HS, STR_IPV4_OPT, CFG_NET},
        // -- NTP --
        {"ntp_enabled",     CO(ntp_enabled),  CO(ntp_enabled),  SCALAR_U8,     0, nullptr, "",    "Enable NTP time sync",      "",                 "1",            0, 1,      0,  STR_NONE,     CFG_NTP},
        {"ntp_poll_s",      CO(ntp_poll_s),   CO(ntp_poll_s),   SCALAR_U16,    0, nullptr, "s",   "NTP poll interval",         "0 = firmware default","64",         0, 65535,  0,  STR_NONE,     CFG_NTP},
        {"ntp_server1",     CO(ntp_server1),  CO(ntp_server1),  SCALAR_STRING, 0, nullptr, "",    "Primary NTP server",        "",                 "pool.ntp.org", 0, 0,      HS, STR_HOST,     CFG_NTP},
        {"ntp_server2",     CO(ntp_server2),  CO(ntp_server2),  SCALAR_STRING, 0, nullptr, "",    "Secondary NTP server",      "empty = none",     "",             0, 0,      HS, STR_HOST_OPT, CFG_NTP},
        // -- MQTT --
        {"mqtt_enabled",    CO(mqtt_enabled), CO(mqtt_enabled), SCALAR_U8,     0, nullptr, "",    "Enable MQTT publishing",    "",                 "1",            0, 1,      0,  STR_NONE,     CFG_MQTT},
        {"mqtt_port",       CO(mqtt_port),    CO(mqtt_port),    SCALAR_U16,    0, nullptr, "",    "Broker TCP port",           "0 = firmware default","1883",       0, 65535,  0,  STR_NONE,     CFG_MQTT},
        {"mqtt_keepalive_s",CO(mqtt_keepalive_s),CO(mqtt_keepalive_s),SCALAR_U16, 0, nullptr,"s", "MQTT keepalive",            "0 = firmware default","60",         0, 65535,  0,  STR_NONE,     CFG_MQTT},
        {"mqtt_broker",     CO(mqtt_broker),  CO(mqtt_broker),  SCALAR_STRING, 0, nullptr, "",    "MQTT broker host",          "",                 "",             0, 0,      HS, STR_HOST,     CFG_MQTT},
        // -- geo (decimal degrees) --
        {"geo_latitude",    CO(geo_latitude), CO(geo_latitude), SCALAR_F32,    4, nullptr, "deg", "Site latitude",            "north +, south -", "48.8566",      -90,  90,   0,  STR_NONE,     CFG_GEO},
        {"geo_longitude",   CO(geo_longitude),CO(geo_longitude),SCALAR_F32,    4, nullptr, "deg", "Site longitude",           "east +, west -",   "2.3522",       -180, 180,  0,  STR_NONE,     CFG_GEO},
    };
#undef CO
    const uint8_t CONFIG_PARAM_COUNT = (uint8_t)(sizeof(CONFIG_PARAMS) / sizeof(CONFIG_PARAMS[0]));

    // Read / render

    const char* type_name(const ConfigParam& p) {
        if (p.type == SCALAR_U8 && p.min == 0 && p.max == 1) return "b";   // boolean (0/1)
        switch (p.type) {
            case SCALAR_U8:     return "u8";
            case SCALAR_U16:    return "u16";
            case SCALAR_U32:    return "u32";
            case SCALAR_I16:    return "i16";
            case SCALAR_I32:    return "i32";
            case SCALAR_F32:    return "f32";
            case SCALAR_ENUM:   return "enum";
            case SCALAR_STRING: return "str";
        }
        return "?";
    }

    size_t param_width(const ConfigParam& p) {
        switch (p.type) {
            case SCALAR_U8: case SCALAR_ENUM: return 1;
            case SCALAR_U16: case SCALAR_I16: return 2;
            case SCALAR_U32: case SCALAR_I32: case SCALAR_F32: return 4;
            case SCALAR_STRING:               return p.capacity;
        }
        return 0;
    }

    uint16_t group_serial_offset(uint8_t group) {
        switch (group) {
            case CFG_NET:  return (uint16_t)offsetof(PlcIO::PlcOutputConfig_t, net_serial);
            case CFG_NTP:  return (uint16_t)offsetof(PlcIO::PlcOutputConfig_t, ntp_serial);
            case CFG_MQTT: return (uint16_t)offsetof(PlcIO::PlcOutputConfig_t, mqtt_serial);
            case CFG_GEO:  return (uint16_t)offsetof(PlcIO::PlcOutputConfig_t, geo_serial);
            default:       return 0;
        }
    }

    // The serial words sit in the struct in ConfigGroup order, so a group's byte
    // range runs from its serial to the next group's (or the struct end for the
    // last). Pinned here so group_byte_range can derive the end instead of a
    // second switch that could drift from group_serial_offset.
    static_assert(offsetof(PlcIO::PlcOutputConfig_t, net_serial)
                < offsetof(PlcIO::PlcOutputConfig_t, ntp_serial), "config serial order");
    static_assert(offsetof(PlcIO::PlcOutputConfig_t, ntp_serial)
                < offsetof(PlcIO::PlcOutputConfig_t, mqtt_serial), "config serial order");
    static_assert(offsetof(PlcIO::PlcOutputConfig_t, mqtt_serial)
                < offsetof(PlcIO::PlcOutputConfig_t, geo_serial), "config serial order");

    void group_byte_range(uint8_t group, uint16_t* start, uint16_t* len) {
        if (group >= CFG_GROUP_COUNT) { *start = 0; *len = 0; return; }
        uint16_t s   = group_serial_offset(group);
        uint16_t end = (group + 1 < CFG_GROUP_COUNT)
            ? group_serial_offset((uint8_t)(group + 1))
            : (uint16_t)sizeof(PlcIO::PlcOutputConfig_t);
        *start = s;
        *len   = (uint16_t)(end - s);
    }

    int32_t read_scalar(const ConfigParam& p, const void* base) {
        const uint8_t* q = (const uint8_t*)base + p.out_offset;
        switch (p.type) {
            case SCALAR_U8: case SCALAR_ENUM: { uint8_t  v; memcpy(&v, q, 1); return v; }
            case SCALAR_U16:                  { uint16_t v; memcpy(&v, q, 2); return v; }
            case SCALAR_U32:                  { uint32_t v; memcpy(&v, q, 4); return (int32_t)v; }
            case SCALAR_I16:                  { int16_t  v; memcpy(&v, q, 2); return v; }
            case SCALAR_I32:                  { int32_t  v; memcpy(&v, q, 4); return v; }
            case SCALAR_F32:                  { float    v; memcpy(&v, q, 4); return (int32_t)v; }
            case SCALAR_STRING:               return 0;
        }
        return 0;
    }

    // Copy at most cap-1 bytes of s into buf, NUL-terminate, return length.
    static int copy_str(char* buf, size_t cap, const char* s) {
        if (cap == 0) return 0;
        size_t n = 0;
        while (s[n] != 0 && n < cap - 1) { buf[n] = s[n]; n++; }
        buf[n] = 0;
        return (int)n;
    }

    int format_value(const ConfigParam& p, const void* base, char* buf, size_t cap) {
        if (cap == 0) return 0;
        if (p.type == SCALAR_STRING) return copy_str(buf, cap, scalar_read_string(p, base));
        if (p.type == SCALAR_F32) {
            Utils::format_float(buf, cap, (double)scalar_read_f32(p, base), p.digits);
            return (int)strlen(buf);
        }
        int32_t v = read_scalar(p, base);
        if (p.type == SCALAR_ENUM && p.str != nullptr) {
            const char* s = p.str((uint8_t)v);
            if (s != nullptr) return copy_str(buf, cap, s);
        }
        int n = snprintf(buf, cap, "%ld", (long)v);
        return (n < 0) ? 0 : (n < (int)cap ? n : (int)cap - 1);
    }

    // Validate / parse

    bool parse_scalar(const ConfigParam& p, const char* s, int32_t* out) {
        if (s == nullptr || *s == 0) return false;
        char* end = nullptr;
        long v = strtol(s, &end, 10);
        if (end == s) return false;
        while (*end == ' ') end++;
        if (*end != 0) return false;                 // trailing garbage
        if (v < INT32_MIN || v > INT32_MAX) return false;
        if (v < p.min || v > p.max) return false;
        *out = (int32_t)v;
        return true;
    }

    bool parse_f32(const ConfigParam& p, const char* s, float* out) {
        if (s == nullptr || *s == 0) return false;
        char* end = nullptr;
        double d = strtod(s, &end);
        if (end == s) return false;
        while (*end == ' ') end++;
        if (*end != 0) return false;                 // trailing garbage
        if (d < (double)p.min || d > (double)p.max) return false;
        *out = (float)d;
        return true;
    }

    bool validate_ipv4(const char* s, bool allow_empty) {
        if (s == nullptr || *s == 0) return allow_empty;
        int octets = 0;
        for (;;) {
            if (*s < '0' || *s > '9') return false;
            int val = 0, digits = 0;
            while (*s >= '0' && *s <= '9') {
                val = val * 10 + (*s - '0');
                if (val > 255) return false;
                digits++; s++;
            }
            if (digits == 0 || digits > 3) return false;
            octets++;
            if (*s == '.') { s++; if (octets == 4) return false; continue; }
            if (*s == 0) break;
            return false;
        }
        return octets == 4;
    }

    bool validate_hostname(const char* s, uint16_t cap, bool allow_empty) {
        if (s == nullptr || *s == 0) return allow_empty;
        uint16_t n = 0;
        for (const char* q = s; *q != 0; ++q) {
            char c = *q;
            bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '.';
            if (!ok) return false;
            if (++n >= cap) return false;   // must fit including the NUL
        }
        return true;
    }

    bool validate_string(const ConfigParam& p, const char* s) {
        switch (p.str_kind) {
            case STR_IPV4:     return validate_ipv4(s, false);
            case STR_IPV4_OPT: return validate_ipv4(s, true);
            case STR_HOST:     return validate_hostname(s, p.capacity, false);
            case STR_HOST_OPT: return validate_hostname(s, p.capacity, true);
            default:           return false;
        }
    }

    // Shared write path (base helper): writes into @p base at the parameter's
    // in_offset (the input mirror).

    bool ConfigHandler::write_value(const ConfigParam& p, const char* text, void* base) {
        if (p.in_offset < 0) return false;            // read-only: not writable
        uint8_t* dst = (uint8_t*)base + p.in_offset;
        if (is_string(p)) {
            if (!validate_string(p, text)) return false;
            size_t n = 0;
            while (text[n] != 0 && n + 1 < p.capacity) { dst[n] = (uint8_t)text[n]; n++; }
            memset(dst + n, 0, p.capacity - n);   // NUL-pad the fixed-size field
            return true;
        }
        if (p.type == SCALAR_F32) {
            float f = 0.0f;
            if (!parse_f32(p, text, &f)) return false;
            memcpy(dst, &f, 4);
            return true;
        }
        int32_t v = 0;
        if (!parse_scalar(p, text, &v)) return false;
        switch (p.type) {
            case SCALAR_U8: case SCALAR_ENUM: { uint8_t  b = (uint8_t)v;  memcpy(dst, &b, 1); break; }
            case SCALAR_U16:                  { uint16_t b = (uint16_t)v; memcpy(dst, &b, 2); break; }
            case SCALAR_U32:                  { uint32_t b = (uint32_t)v; memcpy(dst, &b, 4); break; }
            case SCALAR_I16:                  { int16_t  b = (int16_t)v;  memcpy(dst, &b, 2); break; }
            case SCALAR_I32:                  { int32_t  b = v;           memcpy(dst, &b, 4); break; }
            default:                          return false;
        }
        return true;
    }

}}
