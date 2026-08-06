#ifndef MODBUS_TCP_PARAMS_H
#define MODBUS_TCP_PARAMS_H

/** @file modbus/tcp/params.h
 *  @brief The slave's own configuration rows, for a site's variable table.
 *
 *  `TcpSlave` reads these BY NAME (the `mb` group), so the library declares them
 *  rather than leaving every site to spell them correctly - the same rule the
 *  net_ / ntp_ / mqtt_ rows follow, and for the same reason: a site that misnames
 *  one gets a slave silently running on its defaults, which looks exactly like a
 *  slave that is working.
 *
 *  Registers 950-953, inside the library's 900-999 reserve - clear of the
 *  net_/ntp_/mqtt_/geo_ rows below them and of the alarms at its top.
 */

#include "libbotanik/config/modbus.h"   // BOTANIK_MODBUS_TCP_PORT - this row's default
#include "libbotanik/vars/declare.h"    // the P() factory these rows are written with

#define BOTANIK_MODBUS_TCP_PARAM_VARS                                          \
    /* mb_: TcpSlave reads these by name, so the library declares them */      \
    ::Botanik::Vars::P("mb_serial", ::Botanik::Vars::T_U32,                    \
        1, 0, 9999, 0, "", 950,                                                \
        "bump to apply staged mb_ edits", ""),                                 \
    ::Botanik::Vars::P("mb_enable", ::Botanik::Vars::T_BOOL,                   \
        1, 0, 1, 0, "", 951,                                                   \
        "serve the Modbus TCP slave", ""),                                     \
    ::Botanik::Vars::P("mb_port", ::Botanik::Vars::T_U16,                      \
        BOTANIK_MODBUS_TCP_PORT, 1, 65535, 0, "", 952,                         \
        "Modbus TCP slave port", "standard is 502"),                           \
    ::Botanik::Vars::P("mb_write_enable", ::Botanik::Vars::T_BOOL,             \
        1, 0, 1, 0, "", 953,                                                   \
        "let a master write parameters", "off makes the slave read-only"),

#endif // MODBUS_TCP_PARAMS_H
