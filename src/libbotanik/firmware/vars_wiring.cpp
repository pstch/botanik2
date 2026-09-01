/**
 * @file firmware/vars_wiring.cpp
 * @brief The library's own variable rows: the includes their defaults read,
 *        and the registration. The rows themselves are generated.
 *
 * Under `firmware/` rather than in `vars/`: a row's factory default is its
 * component's struct default, so this file has to see console/, network/ and
 * modbus/ - which is the composition root's privilege, not the variable
 * model's. @see firmware/bindings.cpp
 *
 * A row belongs here when the LIBRARY reads or raises it by name. It is
 * declared in the CSV tables under `vars/tables/` and generated into
 * `vars/vars_table.h`,
 * which this file includes AFTER the feature-gated includes below - a row's
 * factory default is its component's own struct default, so those types have to
 * be complete first.
 *
 * ORDER IS LOAD-BEARING at one point only: the parameters go last, so a site's
 * parameter segment abuts them. @see lib_order
 */
#include "libbotanik/vars/vars.h"

#include "libbotanik/vars/model.h"

// The factory default of a row that configures a component is that
// component's own struct default: one number, next to what runs at it.
#if BOTANIK_FEATURE_CONSOLE
#include "libbotanik/console/terminal.h"
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR
#include "libbotanik/firmware/stacks.h"
#endif
#if BOTANIK_FEATURE_NETWORK
#include "libbotanik/network/network.h"
#endif
#if BOTANIK_FEATURE_NTP
#include "libbotanik/network/ntp.h"
#endif
#if BOTANIK_FEATURE_MQTT
#include "libbotanik/network/mqtt.h"
#include "libbotanik/network/telemetry.h"
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
#include "libbotanik/modbus/tcp/slave.h"
#endif

#include "libbotanik/config/alarms.h"
#include "libbotanik/config/logging.h"
#include "libbotanik/config/modbus.h"
#include "libbotanik/config/scheduler.h"
#include "libbotanik/config/serial.h"
#include "libbotanik/config/sun.h"
#include "libbotanik/config/terminal.h"
#include "libbotanik/config/vars.h"
#include "libbotanik/logs/topics.h"
#include "libbotanik/vars/declare.h"
#include "libbotanik/vars/vars_table.h"

namespace Botanik { namespace Vars {

    void register_library_vars(Bank& bank) {
        (void)bank.register_vars(library_vars, LIBRARY_VARS_COUNT);
    }

}}  // namespace Botanik::Vars
