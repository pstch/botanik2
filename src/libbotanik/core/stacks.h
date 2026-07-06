// REVIEW_35: TODO
#ifndef LIBBOTANIK_CORE_STACKS_H
#define LIBBOTANIK_CORE_STACKS_H

/**
 * @file stacks.h
 * @brief Feature-gated composition sub-objects for BotanikFirmware.
 *
 * Each optional subsystem group is bundled into one small owner so the firmware
 * base carries a single `#if BOTANIK_FEATURE_*` member per group instead of
 * scattering the components (and their ctor-init lines and per-beat steps)
 * across the template. The firmware still owns the shared singletons (logger,
 * scheduler, health, sys context) and injects them by pointer.
 *
 *  - MonitorStack  - the secondary MONITOR UART + its SerialLogWriter sink.
 *  - ConsoleStack  - the console UART + AnsiTerminal + the built-in pages.
 *  - NetworkStack  - resolver + network handler + (NTP) + (MQTT + telemetry).
 */

#include "libbotanik.h"
#if BOTANIK_FEATURE_NETWORK
#include "libbotanik/network/resolver.h" // NameResolver
#include "libbotanik/network/network.h"  // NetworkHandler
#endif
#if BOTANIK_FEATURE_NTP
#include "libbotanik/network/ntp.h"       // NtpClient
#endif
#if BOTANIK_FEATURE_MQTT
#include "libbotanik/network/mqtt.h"      // MqttClient
#include "libbotanik/network/telemetry.h" // MqttSender
#endif

#include <cstdint>

namespace Botanik {

#if BOTANIK_FEATURE_MONITOR
    /** @brief The MONITOR diagnostic path: a dedicated UART and the
     *  SerialLogWriter that drains the logger onto it. Independent of the
     *  console (a headless build can still keep it). */
    class MonitorStack {

        Utils::BufferedSerial _serial;
        Logs::SerialLogWriter _writer;

    public:
        MonitorStack(Logs::Logger* logger = nullptr)
            : _serial(HAL::Serial::get(HAL::Serial::MONITOR), logger, "MonitorSerial"),
              _writer(_serial, /*drop_if_tty=*/true, logger, "MonitorWriter") {}

        Utils::BufferedSerial& serial() { return _serial; }
        Logs::SerialLogWriter& writer() { return _writer; }
        // Both the serial pump (serial().step) and the writer drain
        // (writer().step) are Components stepped directly by the firmware loop.
    };
#endif

#if BOTANIK_FEATURE_CONSOLE
    /** @brief The interactive console: the console UART, the AnsiTerminal, and
     *  the three built-in pages (shell / logs / alarms). The terminal's
     *  SystemContext is wired after construction (set_context) once every
     *  subsystem address is known. */
    class ConsoleStack {

        Utils::BufferedSerial   _serial;
        Terminal::Console       _terminal;
        Pages::ShellPage        _shell;
        Pages::LogsPage         _logs;
        Pages::AlarmsPage       _alarms;

    public:
        /** @param cols/rows terminal geometry; @param timestamp_ms the shared
         *  32-bit display clock; @param baud sizes the TX depth floor;
         *  @param alarms feeds the bottom alarm bar (also the AlarmsPage source);
         *  @param logger the sink the pages announce through. */
        ConsoleStack(uint16_t cols, uint16_t rows, const uint32_t* timestamp_ms,
                     uint32_t baud, Alarms::AlarmModel* alarms,
                     Logs::Logger* logger)
            : _serial(HAL::Serial::get(HAL::Serial::CONSOLE), logger, "ConsoleSerial"),
              _terminal(_serial, cols, rows, timestamp_ms, baud,
                        SystemContext(), alarms, logger),
              _shell(_terminal),
              _logs(_terminal),
              _alarms(_terminal, alarms) {}

        Utils::BufferedSerial&  serial()   { return _serial; }
        Terminal::Console&      terminal() { return _terminal; }
        Pages::ShellPage&       shell()    { return _shell; }
        Pages::LogsPage&        logs()     { return _logs; }
        Pages::AlarmsPage&      alarms()   { return _alarms; }

        /** @brief Terminal logic step (the serial pump is stepped separately by
         *  the firmware via serial()). */
        void step(uint64_t now_ms) { _terminal.step(now_ms); }
    };
#endif

#if BOTANIK_FEATURE_NETWORK
    /** @brief The config-driven network services, all sourcing their config from
     *  the PLC block (NETWORK requires PLC). NTP and MQTT are further gated;
     *  telemetry rides on MQTT and publishes the live SystemContext. */
    class NetworkStack {

        Network::NameResolver   _resolver;
        Network::NetworkHandler _network;
#if BOTANIK_FEATURE_NTP
        Network::NtpClient      _ntp;
#endif
#if BOTANIK_FEATURE_MQTT
        Network::MqttClient     _mqtt;
        Network::MqttSender     _telemetry;
#endif

    public:
        /** @param plc the PLC sync context the handlers read their config from;
         *  @param logger the shared logger; @param sys the live status the MQTT
         *  telemetry base fields read (stored, read per-beat). */
        NetworkStack(const Cfg::ConfigHandler* cfg, Logs::Logger* logger,
                     const SystemContext* sys)
            : _resolver(logger),
              _network(cfg, logger, &_resolver)
#if BOTANIK_FEATURE_NTP
            , _ntp(cfg, logger, &_resolver, &_network)
#endif
#if BOTANIK_FEATURE_MQTT
            , _mqtt(cfg, logger, &_resolver, &_network)
            , _telemetry(&_mqtt, sys, Config::MQTT_TOPIC_PREFIX)
#endif
        { (void)sys; }

        Network::NameResolver&   resolver()  { return _resolver; }
        Network::NetworkHandler& network()   { return _network; }
#if BOTANIK_FEATURE_NTP
        Network::NtpClient&      ntp()       { return _ntp; }
#endif
#if BOTANIK_FEATURE_MQTT
        Network::MqttClient&     mqtt()      { return _mqtt; }
        Network::MqttSender&     telemetry() { return _telemetry; }
#endif

        /** @brief Advance every enabled service for this beat. */
        void step(uint64_t now_ms) {
            _network.step(now_ms);
#if BOTANIK_FEATURE_NTP
            _ntp.step(now_ms);
#endif
#if BOTANIK_FEATURE_MQTT
            _mqtt.step(now_ms);
            _telemetry.step(now_ms);   // periodic field publication (base + site)
#endif
        }
    };
#endif

}  // namespace Botanik

#endif // LIBBOTANIK_CORE_STACKS_H
