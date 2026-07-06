// REVIEW_35: TODO
/**
 * @file config.cpp
 * @brief The library ConfigParam table + the generic read/render/validate
 *        helpers declared in config.h.
 */
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "libbotanik/core/config.h"

namespace Botanik { namespace Cfg {

    // Library built-in parameters. name == the PlcOutputConfig_t field name;
    // offset via offsetof; the flat namespace's grouping is only the `group`
    // tag (which <group>_serial to bump) and the shared name prefix.
    // Fields: name, offset, type, divisor, decimals, group, min, max, capacity, str
    const ConfigParam CONFIG_PARAMS[] = {
        // -- network --
        {"net_dhcp",        offsetof(PlcIO::PlcOutputConfig_t, net_dhcp),        CFG_U8,     0, 0, CFG_NET,  0, 1, 0, nullptr},
        {"net_ip",          offsetof(PlcIO::PlcOutputConfig_t, net_ip),          CFG_STRING, 0, 0, CFG_NET,  0, 0, Config::PLC_HOST_STR_LEN, nullptr},
        {"net_netmask",     offsetof(PlcIO::PlcOutputConfig_t, net_netmask),     CFG_STRING, 0, 0, CFG_NET,  0, 0, Config::PLC_HOST_STR_LEN, nullptr},
        {"net_gateway",     offsetof(PlcIO::PlcOutputConfig_t, net_gateway),     CFG_STRING, 0, 0, CFG_NET,  0, 0, Config::PLC_HOST_STR_LEN, nullptr},
        {"net_dns1",        offsetof(PlcIO::PlcOutputConfig_t, net_dns1),        CFG_STRING, 0, 0, CFG_NET,  0, 0, Config::PLC_HOST_STR_LEN, nullptr},
        {"net_dns2",        offsetof(PlcIO::PlcOutputConfig_t, net_dns2),        CFG_STRING, 0, 0, CFG_NET,  0, 0, Config::PLC_HOST_STR_LEN, nullptr},
        // -- NTP --
        {"ntp_enabled",     offsetof(PlcIO::PlcOutputConfig_t, ntp_enabled),     CFG_U8,     0, 0, CFG_NTP,  0, 1, 0, nullptr},
        {"ntp_poll_s",      offsetof(PlcIO::PlcOutputConfig_t, ntp_poll_s),      CFG_U16,    0, 0, CFG_NTP,  0, 65535, 0, nullptr},
        {"ntp_server1",     offsetof(PlcIO::PlcOutputConfig_t, ntp_server1),     CFG_STRING, 0, 0, CFG_NTP,  0, 0, Config::PLC_HOST_STR_LEN, nullptr},
        {"ntp_server2",     offsetof(PlcIO::PlcOutputConfig_t, ntp_server2),     CFG_STRING, 0, 0, CFG_NTP,  0, 0, Config::PLC_HOST_STR_LEN, nullptr},
        // -- MQTT --
        {"mqtt_enabled",    offsetof(PlcIO::PlcOutputConfig_t, mqtt_enabled),    CFG_U8,     0, 0, CFG_MQTT, 0, 1, 0, nullptr},
        {"mqtt_port",       offsetof(PlcIO::PlcOutputConfig_t, mqtt_port),       CFG_U16,    0, 0, CFG_MQTT, 0, 65535, 0, nullptr},
        {"mqtt_keepalive_s",offsetof(PlcIO::PlcOutputConfig_t, mqtt_keepalive_s),CFG_U16,    0, 0, CFG_MQTT, 0, 65535, 0, nullptr},
        {"mqtt_broker",     offsetof(PlcIO::PlcOutputConfig_t, mqtt_broker),     CFG_STRING, 0, 0, CFG_MQTT, 0, 0, Config::PLC_HOST_STR_LEN, nullptr},
        // -- geo (degrees * 1e7; edited/shown as decimal degrees) --
        {"geo_latitude_e7", offsetof(PlcIO::PlcOutputConfig_t, geo_latitude_e7), CFG_I32, 10000000, 7, CFG_GEO,  -900000000,  900000000, 0, nullptr},
        {"geo_longitude_e7",offsetof(PlcIO::PlcOutputConfig_t, geo_longitude_e7),CFG_I32, 10000000, 7, CFG_GEO, -1800000000, 1800000000, 0, nullptr},
    };
    const uint8_t CONFIG_PARAM_COUNT = (uint8_t)(sizeof(CONFIG_PARAMS) / sizeof(CONFIG_PARAMS[0]));

    // Read / render

    uint16_t group_serial_offset(uint8_t group) {
        switch (group) {
            case CFG_NET:  return (uint16_t)offsetof(PlcIO::PlcOutputConfig_t, net_serial);
            case CFG_NTP:  return (uint16_t)offsetof(PlcIO::PlcOutputConfig_t, ntp_serial);
            case CFG_MQTT: return (uint16_t)offsetof(PlcIO::PlcOutputConfig_t, mqtt_serial);
            case CFG_GEO:  return (uint16_t)offsetof(PlcIO::PlcOutputConfig_t, geo_serial);
            default:       return 0;
        }
    }

    int32_t read_scalar(const ConfigParam& p, const void* base) {
        const uint8_t* q = (const uint8_t*)base + p.offset;
        switch (p.type) {
            case CFG_U8: case CFG_ENUM: { uint8_t  v; memcpy(&v, q, 1); return v; }
            case CFG_U16:               { uint16_t v; memcpy(&v, q, 2); return v; }
            case CFG_U32:               { uint32_t v; memcpy(&v, q, 4); return (int32_t)v; }
            case CFG_I16:               { int16_t  v; memcpy(&v, q, 2); return v; }
            case CFG_I32:               { int32_t  v; memcpy(&v, q, 4); return v; }
            case CFG_STRING:            return 0;
        }
        return 0;
    }

    const char* read_string(const ConfigParam& p, const void* base) {
        return (const char*)((const uint8_t*)base + p.offset);
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
        if (p.type == CFG_STRING) return copy_str(buf, cap, read_string(p, base));

        int32_t v = read_scalar(p, base);
        if (p.type == CFG_ENUM && p.str != nullptr) {
            const char* s = p.str((uint8_t)v);
            if (s != nullptr) return copy_str(buf, cap, s);
        }
        if (p.divisor > 1) {
            int n = snprintf(buf, cap, "%.*f", (int)p.decimals,
                             (double)v / (double)p.divisor);
            return (n < 0) ? 0 : (n < (int)cap ? n : (int)cap - 1);
        }
        int n = snprintf(buf, cap, "%ld", (long)v);
        return (n < 0) ? 0 : (n < (int)cap ? n : (int)cap - 1);
    }

    // Validate / parse

    bool validate_numeric(const ConfigParam& p, int32_t v) {
        return v >= p.min && v <= p.max;
    }

    bool parse_scalar(const ConfigParam& p, const char* s, int32_t* out) {
        if (s == nullptr || *s == 0) return false;
        char* end = nullptr;
        long long r;
        if (p.divisor > 1) {
            // Fixed-point: parse decimal, scale by divisor, round to nearest.
            double d = strtod(s, &end);
            if (end == s) return false;
            double scaled = d * (double)p.divisor;
            r = (long long)(scaled < 0 ? scaled - 0.5 : scaled + 0.5);
        } else {
            long v = strtol(s, &end, 10);
            if (end == s) return false;
            r = v;
        }
        while (*end == ' ') end++;
        if (*end != 0) return false;                 // trailing garbage
        if (r < INT32_MIN || r > INT32_MAX) return false;
        if (!validate_numeric(p, (int32_t)r)) return false;
        *out = (int32_t)r;
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

}}
