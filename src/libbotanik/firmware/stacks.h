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

        /** @brief The monitor's own rate, as a Component so it registers like
         *  everything else.
         *
         *  Beside the writer rather than on it, and the reason is layering:
         *  `vars/` depends on `logs/` (the Bank reports a refused write through
         *  a Logger), so giving a log writer a Bank would close that into a
         *  cycle and `logs/` would stop building without `vars/`. This class is
         *  in `firmware/`, which already depends on both. */
        class Rate : public Component {
            Utils::BufferedSerial& _port;
            const Vars::Bank*      _cfg;
            uint32_t               _baud = Config::MONITOR_BAUD;

        public:
            Rate(const SetupContext& ctx, Utils::BufferedSerial& port,
                 const Vars::Bank* cfg)
                : Component(ctx, "monitor"), _port(port), _cfg(cfg) {}

            void register_tasks() override {
                _tasks->add(*this, &Rate::_task, "monitor_cfg",
                      Tasks::GROUP_SLOW, Tasks::PHASE_CONFIG);
            }

            /** @brief Edge-triggered, so the steady case is one integer compare
             *  and no device call. Applies the moment it moves, where the
             *  CONSOLE's waits for the `term_` commit: the monitor is a one-way
             *  log stream, so getting its rate wrong costs the log and nothing
             *  else. */
            void apply() {
                if (_cfg == nullptr) return;
                const uint32_t want = (uint32_t)Vars::get_int(
                    _cfg, "serial_monitor_baud", (int32_t)_baud);
                if (want == _baud || want == 0) return;
                _baud = want;
                _port.begin(want);
            }

        private:
            static void _task(Component* c, const Tasks::Tick&) {
                static_cast<Rate*>(c)->apply();
            }
        };
        Rate _rate;

    public:
        MonitorStack(const SetupContext& ctx, const Vars::Bank* cfg = nullptr)
            : _serial(HAL::Serial::get(HAL::Serial::MONITOR)),
              _writer(ctx, _serial, /*drop_if_tty=*/true, "monitor_log", "MONITOR"),
              _rate(ctx, _serial, cfg) {}

        Utils::BufferedSerial& serial() { return _serial; }
        Logs::SerialLogWriter& writer() { return _writer; }

        /** @brief The writer's drain, then the port's own rate. The port itself
         *  is not a task: the writer owns the wire and pumps it in its own. */
        void register_tasks() {
            _writer.register_tasks();
            _rate.register_tasks();
        }

        /** @brief Apply the stored rate before the loop, so the boot lines go
         *  out at it. */
        void apply_stored_config() { _rate.apply(); }
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
                     const SetupContext& ctx,
                     const Scheduler::SchedulerContext* sched = nullptr,
                     const SystemContext* sys = nullptr,
                     const Vars::Bank* cfg = nullptr)
            : _serial(HAL::Serial::get(HAL::Serial::CONSOLE)),
              // Leading commas from here down: an optional page can drop out
              // without stranding a separator.
              // The console reads the `term_` group and its own baud out of
              // the bank: those rows describe nothing but it.
              _terminal(ctx, _serial, cols, rows, timestamp_ms, baud,
                        sched, alarms, Terminal::Ansi::CONFIGURED_TERMINAL, cfg)
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
        NetworkStack(const SetupContext& ctx, const Vars::Bank* cfg,
                     const SystemContext* sys)
            : _resolver(ctx.logger),
              _network(ctx, cfg, &_resolver)
#if BOTANIK_FEATURE_NTP
            , _ntp(ctx, cfg, &_resolver, &_network)
#endif
#if BOTANIK_FEATURE_MQTT
            , _mqtt(ctx, cfg, &_resolver, &_network)
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

        /** @brief Hand each enabled service its own registration, in the order
         *  this used to step them.
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
