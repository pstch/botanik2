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
#include <cstdint>

#include "libbotanik/config/identity.gen.h"

#include "libbotanik/alarms/alarms.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/logs/writer.h"
#include "libbotanik/utils/serial.h"
#include "libbotanik/vars/model.h"

#if BOTANIK_FEATURE_CONSOLE
#include "libbotanik/console/console.h"
#endif
#if BOTANIK_FEATURE_SHELL_PAGE
#include "libbotanik/pages/shell.h"
#endif
#if BOTANIK_FEATURE_LOGS_PAGE
#include "libbotanik/pages/logs.h"
#endif
#if BOTANIK_FEATURE_ALARMS_PAGE
#include "libbotanik/pages/alarms.h"
#endif
#if BOTANIK_FEATURE_NOTES_PAGE
#include "libbotanik/pages/notes.h"
#endif

#if BOTANIK_FEATURE_NETWORK
#include "libbotanik/network/network.h"
#include "libbotanik/network/resolver.h"
#endif
#if BOTANIK_FEATURE_NTP
#include "libbotanik/network/ntp.h"
#endif
#if BOTANIK_FEATURE_LOGS_PUBLISH
#include "libbotanik/network/mqtt_log.h"
#endif
#if BOTANIK_FEATURE_MQTT
#include "libbotanik/network/mqtt.h"
#include "libbotanik/network/telemetry.h"
#endif

namespace Botanik {

#if BOTANIK_FEATURE_LOGS_MONITOR
    /** @brief The MONITOR port's line rate. Unstaged, where the console's waits
     *  for the `console_` commit: this is a one-way log stream, so getting the
     *  rate wrong costs the log and nothing else. */
    struct MonitorConfig {
        uint32_t baud = 115200;
    };

    /** @brief The MONITOR diagnostic path: a dedicated UART and the
     *  SerialLogWriter that drains the logger onto it. Independent of the
     *  console (a headless build can still keep it). */
    class MonitorStack {

        Utils::BufferedSerial _serial;
        Logs::SerialLogWriter _writer;

        /** @brief The rate in force. 0 until something sets one, so the first
         *  push always reaches the device. */
        uint32_t              _baud = 0;

    public:
        explicit MonitorStack(const SetupContext& ctx)
            : _serial(HAL::Serial::get(HAL::Serial::MONITOR)),
              _writer(ctx, _serial, /*drop_if_tty=*/true, "sys_monitor_log", "MONITOR",
                      "monitor_log_level") {}

        Utils::BufferedSerial& serial() { return _serial; }
        Logs::SerialLogWriter& writer() { return _writer; }
        /** @brief The rate the port is running at, as last applied. */
        uint32_t baud() const { return _baud; }

        /** @brief Edge-triggered, so the steady case is one integer compare and
         *  no device call. */
        void set_config(const MonitorConfig& cfg) {
            if (cfg.baud == 0 || cfg.baud == _baud) return;
            _baud = cfg.baud;
            _serial.begin(cfg.baud);
        }

        /** @brief The writer's drain. The port itself is not a task: the writer
         *  owns the wire and pumps it in its own. */
        void register_tasks() { _writer.register_tasks(); }
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
        /** @param timestamp_ms the shared display clock;
         *  @param alarms feeds the bottom alarm bar (also the AlarmsPage source). */
        ConsoleStack(const uint64_t* timestamp_ms, Alarms::AlarmController* alarms,
                     const SetupContext& ctx,
                     const Scheduler::SchedulerContext* sched = nullptr,
                     const SystemContext* sys = nullptr)
            : _serial(HAL::Serial::get(HAL::Serial::CONSOLE)),
              // Leading commas from here down: an optional page can drop out
              // without stranding a separator.
              // Geometry, rate and make arrive through set_config(); with a
              // bank present that is Vars::bind_console()'s job.
              _terminal(ctx, _serial, timestamp_ms, sched, alarms)
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
            , _notes(_terminal, ctx.logger)
#endif
        { (void)sys; }   // only ShellPage reads it

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
        /** @param sys the live status the MQTT telemetry base fields read
         *  (stored, read per-beat). Every handler's own config arrives through
         *  ConfigBinder. */
        NetworkStack(const SetupContext& ctx, const SystemContext* sys)
            : _resolver(ctx.logger),
              _network(ctx, &_resolver)
#if BOTANIK_FEATURE_NTP
            , _ntp(ctx, &_resolver, &_network)
#endif
#if BOTANIK_FEATURE_MQTT
            , _mqtt(ctx, &_resolver, &_network)
            , _telemetry(ctx, &_mqtt, sys, Config::MQTT_TOPIC_PREFIX)
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

        /** @brief Hand each enabled service its own registration, in beat order.
         *
         *  Not a Component - it owns several - so it has no tasks of its own.
         *  Nothing steps anything now: each child is a registry entry with its
         *  own rate and its own row on the TASKS page. */
        void register_tasks() {
            _network.register_tasks();
#if BOTANIK_FEATURE_NTP
            _ntp.register_tasks();
#endif
#if BOTANIK_FEATURE_MQTT
            _mqtt.register_tasks();
            _telemetry.register_tasks();
#endif
        }

    };
#endif

}  // namespace Botanik

#endif // LIBBOTANIK_FIRMWARE_STACKS_H
