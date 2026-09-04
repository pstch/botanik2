#ifndef LIBBOTANIK_VARS_BINDINGS_H
#define LIBBOTANIK_VARS_BINDINGS_H

/**
 * @file bindings.h
 * @brief ConfigBinder - the one bridge from the variable table to a component's
 *        own configuration struct.
 *
 * A component takes its settings through set_config(), and never learns where
 * they came from: with no variable model its owner fills the struct in, with one
 * this walks the table at PHASE_CONFIG and fills it from the rows. Every
 * `Vars::` mention behind a configurable component lives in bindings.cpp.
 */

#include <cstdint>

#include "libbotanik/config/vars.h"
#include "libbotanik/core/component.h"
#include "libbotanik/vars/ref.h"

namespace Botanik { namespace Logs { class Logger; } }
namespace Botanik { namespace Tasks { struct Tick; } }
namespace Botanik { namespace Terminal { class Console; } }
namespace Botanik { namespace Sun { class SunPositionTracker; } }
namespace Botanik { class MonitorStack; }
namespace Botanik { namespace Network { class NetworkHandler; class NtpClient; class MqttClient; class MqttSender; } }
namespace Botanik { namespace Modbus { class TcpSlave; } }

namespace Botanik { namespace Vars {

    class Bank;

    /** @brief One component's settings, and the rows behind them.
     *
     *  @p serial_row is the group's commit serial, which STAGES this binding:
     *  the settings land together on the bump rather than one per edit, so a
     *  half-typed geometry never reaches the screen. VAR_NONE means unstaged -
     *  re-read every beat, for settings where a partial apply is invisible. */
    struct ConfigBinding {
        uint16_t serial_row;
        void*    target;
        void   (*apply)(void*, const Bank&, bool announce);
        uint32_t applied;   ///< last serial pushed
        bool     ever;      ///< pushed at least once (the first push is unconditional)
    };

    /** @brief Pushes each registered component its configuration, out of the
     *  bank, at PHASE_CONFIG. */
    class ConfigBinder : public Botanik::Component {
        /** @name Table
         *  @{ */

        const Bank*   _io;
        ConfigBinding _tab[Config::CONFIG_BINDINGS_MAX];
        uint8_t       _len = 0;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        ConfigBinder(const SetupContext& ctx, const Bank* io)
            : Component(ctx, "Config"), _io(io) {}

        /** @brief Bind @p target's configuration to @p serial_row's group.
         *  @p apply reads the rows and calls the component's set_config(); it is
         *  the only code that sees both sides. Silently drops past
         *  Config::CONFIG_BINDINGS_MAX. */
        void add(uint16_t serial_row, void* target,
                 void (*apply)(void*, const Bank&, bool announce));

        void register_tasks() override;

        /** @brief Push every binding whose group has moved, and every binding
         *  that has never been pushed. A group with no rows is never pushed at
         *  all: that component is one this site does not configure.
         *  @warning AFTER the parameter load, or the defaults land and the
         *  stored image never does. */
        void apply_all();

        /** @} */

    private:
        static void _task(Component* c, const Tasks::Tick& t);
    };

    /** @name Bindings
     *  One per configurable component; each is the whole coupling between
     *  that component and the table. @see bindings.cpp
     *  @{ */

    void bind_console(ConfigBinder& binder, Terminal::Console& console);
    /** @brief Each log sink's floor, from the row that sink names. A page
     *  cannot reach the bank and should not learn how; the LOGGER cannot read it
     *  either, or `logs/` would depend back on `vars/` and the lowest service in
     *  the tree would stop building without the variable model. */
    void bind_logger(ConfigBinder& binder, Logs::Logger& logger);
    void bind_sun(ConfigBinder& binder, Sun::SunPositionTracker& sun);
    void bind_monitor(ConfigBinder& binder, MonitorStack& monitor);
    void bind_network(ConfigBinder& binder, Network::NetworkHandler& net);
    void bind_ntp(ConfigBinder& binder, Network::NtpClient& ntp);
    void bind_mqtt(ConfigBinder& binder, Network::MqttClient& mqtt);
    void bind_telemetry(ConfigBinder& binder, Network::MqttSender& tlm);
    void bind_modbus_tcp(ConfigBinder& binder, Modbus::TcpSlave& slave);

    /** @} */


    /** @brief A host row's text, or the GATEWAY when it is empty.
     *
     *  The hostname rows ship blank - a hostname is the one thing a register
     *  pair cannot carry, so they are console-only, and a default in the table
     *  would be a guess at somebody else's network. The gateway is not a guess:
     *  it is the one address on this segment the unit has been told about, and
     *  on a small plant the router is the NTP relay and the broker often enough
     *  to be worth trying before nothing at all.
     *
     *  Public because the CONFIG and the console must agree: a `net` report
     *  showing an empty broker while the client talks to the gateway would be
     *  the report lying. */
    const char* host_or_gateway(const Bank& io, uint16_t row);

}}  // namespace Botanik::Vars

#endif // LIBBOTANIK_VARS_BINDINGS_H
