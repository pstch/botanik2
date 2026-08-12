// REVIEW_35: TODO
#ifndef LIBBOTANIK_FIRMWARE_STACKS_H
#define LIBBOTANIK_FIRMWARE_STACKS_H

/**
 * @file stacks.h
 * @brief Feature-gated composition sub-objects for BotanikFirmware.
 *
 * Each optional subsystem group is bundled into one small owner so the firmware
 * base carries a single `#if BOTANIK_FEATURE_*` member per group. The firmware
 * still owns the shared singletons (logger, scheduler, health, sys context) and
 * injects them by pointer.
 *
 *  - MonitorStack  - the secondary MONITOR UART + its SerialLogWriter sink.
 *  - ConsoleStack  - the console UART + AnsiTerminal + the built-in pages.
 *  - NetworkStack  - resolver + network handler + (NTP) + (MQTT + telemetry).
 */

#include "libbotanik/tasks/tasks.h"
#include <cstdint>                          // uint8_t/uint32_t

#include "libbotanik/config/identity.h"   // Config::SITE_NAME / FIRMWARE_VERSION / MQTT_TOPIC_PREFIX

#include "libbotanik/alarms/alarms.h"       // Alarms::AlarmController
#include "libbotanik/logs/logger.h"         // Logs::Logger
#include "libbotanik/logs/writer.h"         // Logs::SerialLogWriter
#include "libbotanik/utils/serial.h"        // Utils::BufferedSerial
#include "libbotanik/vars/model.h"          // Vars::Bank

#if BOTANIK_FEATURE_CONSOLE
#include "libbotanik/console/console.h"     // Terminal::Console
#endif
#if BOTANIK_FEATURE_SHELL_PAGE
#include "libbotanik/pages/shell.h"         // Pages::ShellPage
#endif
#if BOTANIK_FEATURE_LOGS_PAGE
#include "libbotanik/pages/logs.h"          // Pages::LogsPage
#endif
#if BOTANIK_FEATURE_ALARMS_PAGE
#include "libbotanik/pages/alarms.h"        // Pages::AlarmsPage
#endif
#if BOTANIK_FEATURE_NOTES_PAGE
#include "libbotanik/pages/notes.h"         // Pages::NotesPage
#endif

#if BOTANIK_FEATURE_NETWORK
#include "libbotanik/network/network.h"     // Network::NetworkHandler
#include "libbotanik/network/resolver.h"    // Network::NameResolver
#endif
#if BOTANIK_FEATURE_NTP
#include "libbotanik/network/ntp.h"         // Network::NtpClient
#endif
#if BOTANIK_FEATURE_LOGS_PUBLISH
#include "libbotanik/network/mqtt_log.h"    // Network::MqttLogWriter
#endif
#if BOTANIK_FEATURE_MQTT
#include "libbotanik/network/mqtt.h"        // Network::MqttClient
#include "libbotanik/network/telemetry.h"   // Network::MqttSender
#endif

namespace Botanik {

#if BOTANIK_FEATURE_LOGS_MONITOR
    /** @brief The MONITOR diagnostic path: a dedicated UART and the
     *  SerialLogWriter that drains the logger onto it. Independent of the
     *  console (a headless build can still keep it). */
    class MonitorStack {

        Utils::BufferedSerial _serial;
        Logs::SerialLogWriter _writer;

    public:
        MonitorStack(Logs::Logger* logger = nullptr)
            : _serial(HAL::Serial::get(HAL::Serial::MONITOR)),
              _writer(_serial, /*drop_if_tty=*/true, logger, "monitor_log") {}

        Utils::BufferedSerial& serial() { return _serial; }
        Logs::SerialLogWriter& writer() { return _writer; }
        // The port is not a task: the writer owns the wire and pumps it in its
        // own. serial() is still handed out for the logger's TX-drop watch.
    };
#endif

#if BOTANIK_FEATURE_CONSOLE
    /** @brief The interactive console: the console UART, the AnsiTerminal, and
     *  the built-in pages that need nothing but it (shell / logs / alarms, and
     *  the notes editor when that feature is on). @p sched and @p sys are
     *  borrowed by the status bar and the shell, by pointer, so the firmware may
     *  pass its members before their fields are filled. */
    class ConsoleStack {

        Utils::BufferedSerial   _serial;
        Terminal::Console       _terminal;
#if BOTANIK_FEATURE_SHELL_PAGE
        Pages::ShellPage        _shell;
#endif
#if BOTANIK_FEATURE_LOGS_PAGE
        Pages::LogsPage         _logs;
#endif
#if BOTANIK_FEATURE_ALARMS_PAGE
        Pages::AlarmsPage       _alarms;
#endif
#if BOTANIK_FEATURE_NOTES_PAGE
        Pages::NotesPage        _notes;
#endif

    public:
        /** @param cols/rows terminal geometry; @param timestamp_ms the shared
         *  display clock; @param baud sizes the TX depth floor;
         *  @param alarms feeds the bottom alarm bar (also the AlarmsPage source);
         *  @param logger the sink the pages announce through. */
        ConsoleStack(uint16_t cols, uint16_t rows, const uint64_t* timestamp_ms,
                     uint32_t baud, Alarms::AlarmController* alarms,
                     Logs::Logger* logger,
                     const Scheduler::SchedulerContext* sched = nullptr,
                     const SystemContext* sys = nullptr)
            : _serial(HAL::Serial::get(HAL::Serial::CONSOLE)),
              // Leading commas from here down: an optional page can drop out
              // without stranding a separator.
              _terminal(_serial, cols, rows, timestamp_ms, baud,
                        sched, alarms, logger)
#if BOTANIK_FEATURE_SHELL_PAGE
            , _shell(_terminal, sys)
#endif
#if BOTANIK_FEATURE_LOGS_PAGE
            , _logs(_terminal)
#endif
#if BOTANIK_FEATURE_ALARMS_PAGE
            , _alarms(_terminal, alarms)
#endif
#if BOTANIK_FEATURE_NOTES_PAGE
            , _notes(_terminal, logger)
#endif
        {}

        Utils::BufferedSerial&  serial()   { return _serial; }
        Terminal::Console&      terminal() { return _terminal; }
#if BOTANIK_FEATURE_SHELL_PAGE
        Pages::ShellPage&       shell()    { return _shell; }
#endif
#if BOTANIK_FEATURE_LOGS_PAGE
        Pages::LogsPage&        logs()     { return _logs; }
#endif
#if BOTANIK_FEATURE_ALARMS_PAGE
        Pages::AlarmsPage&      alarms()   { return _alarms; }
#endif
#if BOTANIK_FEATURE_NOTES_PAGE
        Pages::NotesPage&       notes()    { return _notes; }
#endif

    };
#endif

#if BOTANIK_FEATURE_NETWORK
    /** @brief The config-driven network services, all sourcing their config from
     *  the parameter model. NTP and MQTT are further gated; telemetry rides on
     *  MQTT and publishes the live SystemContext. */
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
#if BOTANIK_FEATURE_LOGS_PUBLISH
        Network::MqttLogWriter  _mqtt_log;
#endif

    public:
        /** @param cfg the parameter model the handlers read their config from;
         *  @param logger the shared logger; @param sys the live status the MQTT
         *  telemetry base fields read (stored, read per-beat). */
        NetworkStack(const Vars::Bank* cfg, Logs::Logger* logger,
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
#if BOTANIK_FEATURE_LOGS_PUBLISH
        Network::MqttLogWriter&  mqtt_log()  { return _mqtt_log; }
#endif

        /** @brief Hand each enabled service its own registration, in the order
         *  this used to step them.
         *
         *  Not a Component - it owns several - so it has no tasks of its own.
         *  Nothing steps anything now: each child is a registry entry with its
         *  own rate and its own row on the TASKS page. */
        void register_tasks(Tasks::Registry& r) {
            _network.register_tasks(r);
#if BOTANIK_FEATURE_NTP
            _ntp.register_tasks(r);
#endif
#if BOTANIK_FEATURE_MQTT
            _mqtt.register_tasks(r);
            // MqttSender is not a Component: it has no lifecycle to register, so
            // it keeps the plain-function form until it grows one.
            r.add("telemetry", Tasks::GROUP_FAST, Tasks::PHASE_SERVICE,
                  [](void* s, uint64_t now) {
                      static_cast<Network::MqttSender*>(s)->step(now);
                  },
                  &_telemetry);
#endif
        }

    };
#endif

}  // namespace Botanik

#endif // LIBBOTANIK_FIRMWARE_STACKS_H
