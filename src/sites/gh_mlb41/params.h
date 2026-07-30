#ifndef SITE_PARAMS_H
#define SITE_PARAMS_H

/**
 * @file params.h
 * @brief GENERATED - the gh_mlb41 config parameters: network / NTP / MQTT / geo
 *        plus each group's commit serial.
 *
 * Generated from the Arduino PLC IDE project. Regenerating overwrites this
 * file.
 *
 * A parameter is declared over BOTH site payloads, which are distinct layouts:
 * out_offset reads the live value the PLC reflects (PlcOutputData_t), in_offset
 * stages an edit for the PLC to latch (PlcInputData_t).
 *
 * Commit model: editing a value only stages it. Each group's commit serial is an
 * ordinary parameter NAMED AFTER THE GROUP ("net" commits the net group), so
 * editing that serial ships the staged group; the PLC latches it and reflects
 * the serial back, which the consuming Component watches.
 *
 * Each row pairs a ScalarField (read/display) with a ParamMeta (edit bounds):
 *   field {name, out_offset, type, digits, str, unit, desc, notes}
 *   meta  {def, min, max, len, kind, group}
 */

#include <cstddef>   // offsetof
#include <cstdint>

#include "botanik.h"

#include "payload.h"

namespace Site {

using namespace Botanik;

#define OO(f) (int16_t)offsetof(PlcOutputData_t, config.f)
#define CAP   (uint16_t)GreenhouseConfig_t::STRLEN

const Params::Param g_params[] = {
    // -- network --
    {{"net",          OO(net),           SCALAR_U32,    0, nullptr, "",    "Network config commit", "bump to apply net edits"}, {"1", 0, 1000000000, 0, Params::STR_NONE,     "net"}},
    {{"net_dhcp",     OO(net_dhcp),      SCALAR_U8,     0, nullptr, "",    "DHCP (1) or static IP (0)", ""},              {"1",  0, 1,      0,   Params::STR_NONE,     "net"}},
    {{"net_ip",       OO(net_ip),        SCALAR_STRING, 0, nullptr, "",    "Static IPv4 address", "used when DHCP off"},  {"",   0, 0,      CAP, Params::STR_IPV4,     "net"}},
    {{"net_netmask",  OO(net_netmask),   SCALAR_STRING, 0, nullptr, "",    "Static subnet mask",  "used when DHCP off"},  {"",   0, 0,      CAP, Params::STR_IPV4,     "net"}},
    {{"net_gateway",  OO(net_gateway),   SCALAR_STRING, 0, nullptr, "",    "Default gateway",     "used when DHCP off"},  {"",   0, 0,      CAP, Params::STR_IPV4,     "net"}},
    {{"net_dns1",     OO(net_dns1),      SCALAR_STRING, 0, nullptr, "",    "Primary DNS server",  ""},                    {"",   0, 0,      CAP, Params::STR_IPV4,     "net"}},
    {{"net_dns2",     OO(net_dns2),      SCALAR_STRING, 0, nullptr, "",    "Secondary DNS server","empty = none"},        {"",   0, 0,      CAP, Params::STR_IPV4_OPT, "net"}},
    // -- NTP --
    {{"ntp",          OO(ntp),           SCALAR_U32,    0, nullptr, "",    "NTP config commit", "bump to apply ntp edits"}, {"1", 0, 1000000000, 0, Params::STR_NONE,     "ntp"}},
    {{"ntp_enabled",  OO(ntp_enabled),   SCALAR_U8,     0, nullptr, "",    "Enable NTP time sync",""},                    {"1",  0, 1,      0,   Params::STR_NONE,     "ntp"}},
    {{"ntp_poll_s",   OO(ntp_poll_s),    SCALAR_U16,    0, nullptr, "s",   "NTP poll interval",   "0 = firmware default"},{"64", 0, 65535,  0,   Params::STR_NONE,     "ntp"}},
    {{"ntp_server1",  OO(ntp_server1),   SCALAR_STRING, 0, nullptr, "",    "Primary NTP server",  ""},          {"pool.ntp.org", 0, 0,      CAP, Params::STR_HOST,     "ntp"}},
    {{"ntp_server2",  OO(ntp_server2),   SCALAR_STRING, 0, nullptr, "",    "Secondary NTP server","empty = none"},        {"",   0, 0,      CAP, Params::STR_HOST_OPT, "ntp"}},
    // -- MQTT --
    {{"mqtt",         OO(mqtt),          SCALAR_U32,    0, nullptr, "",    "MQTT config commit", "bump to apply mqtt edits"},{"1",0, 1000000000, 0, Params::STR_NONE,     "mqtt"}},
    {{"mqtt_enabled", OO(mqtt_enabled),  SCALAR_U8,     0, nullptr, "",    "Enable MQTT publishing",""},                  {"1",  0, 1,      0,   Params::STR_NONE,     "mqtt"}},
    {{"mqtt_port",    OO(mqtt_port),     SCALAR_U16,    0, nullptr, "",    "Broker TCP port",     "0 = firmware default"},{"1883",0,65535,  0,   Params::STR_NONE,     "mqtt"}},
    {{"mqtt_keepalive_s",OO(mqtt_keepalive_s),SCALAR_U16,0,nullptr,"s",  "MQTT keepalive",      "0 = firmware default"},{"60", 0, 65535,  0,   Params::STR_NONE,     "mqtt"}},
    {{"mqtt_broker",  OO(mqtt_broker),   SCALAR_STRING, 0, nullptr, "",    "MQTT broker host",    ""},                    {"",   0, 0,      CAP, Params::STR_HOST,     "mqtt"}},
    // -- geo (decimal degrees) --
    {{"geo",          OO(geo),           SCALAR_U32,    0, nullptr, "",    "Geo config commit", "bump to apply geo edits"}, {"1", 0, 1000000000, 0, Params::STR_NONE,     "geo"}},
    {{"geo_latitude", OO(geo_latitude),  SCALAR_F32,    4, nullptr, "deg", "Site latitude",       "north +, south -"},  {"48.8566", -90,  90, 0,  Params::STR_NONE,     "geo"}},
    {{"geo_longitude",OO(geo_longitude), SCALAR_F32,    4, nullptr, "deg", "Site longitude",      "east +, west -"},    {"2.3522", -180, 180, 0,  Params::STR_NONE,     "geo"}},
};

#undef OO
#undef CAP

const uint8_t g_param_count = (uint8_t)(sizeof(g_params) / sizeof(g_params[0]));

// SiteDescriptor counts every table with a uint8_t, so a longer table would wrap
// its count and publish only a prefix.
static_assert(sizeof(g_params) / sizeof(g_params[0]) <= Config::TABLE_ROWS_MAX,
              "too many parameters: the descriptor's uint8_t count would wrap");

}  // namespace Site

#endif // SITE_PARAMS_H
