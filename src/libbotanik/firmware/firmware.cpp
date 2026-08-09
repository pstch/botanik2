// REVIEW_35: TODO
/**
 * @file firmware.cpp
 * @brief BotanikFirmware: construction, bring-up, and the task table.
 *
 * Everything cold. The header keeps the class, the accessors and loop() - the
 * hot path, and the frame the scheduler's stack painting is measured against -
 * and nothing else: these bodies are ~350 lines that every site and every test
 * used to compile for themselves out of the tree's highest-fan-out header.
 */
#include "libbotanik/firmware/firmware.h"   // BotanikFirmware - implemented here

namespace Botanik {

    // Construction
    // ------------------------------------------------------------------------

    // The initialiser list follows declaration order exactly (-Wreorder): _io
    // must be alive before _params and _scan take its address, and _health
    // before _modbus_tcp and _console take its alarm model.
    BotanikFirmware::BotanikFirmware(const SiteDescriptor& site)
        : _site(site),
          _logger(),
          _scheduler(Config::TASK_PERIOD_MS, &_logger),
          _io(site.vars),
          // Params come from local storage.
          _param_store(),
          _params(&_io, &_param_store, &_logger),
#if BOTANIK_FEATURE_PLC_SCAN
          _scan(site.programs, site.program_count, &_io, &_logger),
#endif
          // The alarm model scans the variable table for SCOPE_A rows;
          // platform-health alarms are appended and driven locally.
          _health(&_logger, _scheduler.context(), &_io)
          , _sys_ctx()   // fields wired in the body, once addresses are known
#if BOTANIK_FEATURE_LOGS_MONITOR
          , _monitor(&_logger)
#endif
#if BOTANIK_FEATURE_NETWORK
          // Network reads its config from the variable bank by name;
          // telemetry publishes the live _sys_ctx (read later).
          , _net(&_io, &_logger, &_sys_ctx)
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
          , _modbus_rtu(&_io, &_logger, site.devices)
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
          // The alarm model is the ack block's target; it is HealthMonitor's,
          // and this member is declared after it so the reference is live.
          , _modbus_tcp(&_io, &_logger, &_health.alarm_model())
#endif
#if BOTANIK_FEATURE_CONSOLE
          // _sys_ctx is declared above, so it exists (empty) by now; the console
          // borrows it by pointer and reads it later, once the body below has
          // filled in the subsystem addresses.
          , _console(_console_cols, _console_rows,
                     &_scheduler.context()->timestamp_ms, _console_baud,
                     &_health.alarm_model(), &_logger,
                     _scheduler.context(), &_sys_ctx)
          // Base-owned pages, built from the descriptor + the live bank.
#if BOTANIK_FEATURE_HOME_PAGE
          , _home_page(_console.terminal(), _site.home_fields,
                       _site.home_field_count, &_io, _site.home_refs)
#endif
#if BOTANIK_FEATURE_PARAMS_PAGE
          , _params_page(_console.terminal(), &_params)
#endif
#if BOTANIK_FEATURE_MONITOR_PAGE
          , _monitor_page(_console.terminal(), &_io)
#endif
#if BOTANIK_FEATURE_TASKS_PAGE
          , _tasks_page(_console.terminal(), _tasks)
#endif
#endif
    {
        // Wire the status aggregate now every subsystem address is fixed.
        _sys_ctx.scheduler = _scheduler.context();
        _sys_ctx.tasks     = &_tasks;
        _sys_ctx.vars      = &_io;
#if BOTANIK_FEATURE_NETWORK
        _sys_ctx.network = &_net.network();
#endif
#if BOTANIK_FEATURE_NTP
        _sys_ctx.ntp = &_net.ntp();
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
        _sys_ctx.modbus_tcp = &_modbus_tcp;
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
        _sys_ctx.modbus_rtu = &_modbus_rtu;
#endif
#if BOTANIK_FEATURE_MQTT
        _sys_ctx.mqtt = &_net.mqtt();
#endif
    }

    // Bring-up
    // ------------------------------------------------------------------------

    void BotanikFirmware::setup() {
        // route BOTANIK_ASSERT failures to the log
        set_assert_logger(&_logger);
        // Seed every variable with its declared default, so a value read before
        // any driver has spoken is the default rather than zero.
        _io.init();
        // Bind the site's declarative tables to the variable table. They name
        // their rows rather than indexing them, so this is where a name becomes
        // an index - once, here, because no renderer should pay for the lookup
        // per frame. A name with no row lands on the sink and is counted (and
        // logged) by the bank, which is what the site test asserts is zero.
        _resolve_site_refs();

        // What runs on the beat, in which phase, and how often. A component
        // whose work resolves something measured in seconds need not pay 100 Hz
        // for it; the scan driver and the logger keep the beat deliberately,
        // since the scan's not-due path is two comparisons per task and the
        // logger drains a queue that evicts under pressure.
        _register_tasks();

#if BOTANIK_FEATURE_CONSOLE
        HAL::Serial::get(HAL::Serial::CONSOLE)->begin(_console_baud);
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR
        HAL::Serial::get(HAL::Serial::MONITOR)->begin(_monitor_baud);
#endif

        // Core sinks: monitor + (with the UI) shell/logs pages, plus a TX-drop
        // watch on each live UART. A site adds its own in on_setup().
#if BOTANIK_FEATURE_LOGS_MONITOR
        _logger.add_writer(&_monitor.writer());
        _logger.watch_tx(&_monitor.serial());
#endif
#if BOTANIK_FEATURE_CONSOLE
#if BOTANIK_FEATURE_SHELL_PAGE
        _logger.add_writer(&_console.shell());
#endif
#if BOTANIK_FEATURE_LOGS_PAGE
        _logger.add_writer(&_console.logs());
#endif
        // The home page sinks the log stream into its bottom log-tail band.
#if BOTANIK_FEATURE_HOME_PAGE
        _logger.add_writer(&_home_page);
#endif
        _logger.watch_tx(&_console.serial());
#endif

#if BOTANIK_FEATURE_CONSOLE
#if BOTANIK_FEATURE_SHELL_PAGE
        // Let the shell's loglevel / alarm-ack commands reach the live logger +
        // model.
        _console.shell().set_diagnostics(&_logger, &_health.alarm_model());
#if BOTANIK_FEATURE_NTP
        // Let the `ntpsync` command force a resync on the live client.
        _console.shell().set_ntp(&_net.ntp());
#endif

        register_commands();   // site commands, tried after the built-ins
#endif
        // tab order (page 0 = boot page); switch_to(0) records it
        register_pages();
        _console.terminal().setup_terminal();
        _console.terminal().switch_to(0);
#endif

        // Details on a second line (the renderer splits on '\n') so it wraps
        // cleanly.
        _logger.info(Logs::BOOT, "Botanik v%s\nRunning on : %s",
                     Config::FIRMWARE_VERSION, HAL::hardware_description());

#if BOTANIK_FEATURE_ALARMS_PUBLISH
        // Route alarm transitions out through the MQTT client (it is an
        // Alarms::Publisher). Until now the notifier was a no-op.
        _health.set_alarm_publisher(&_net.mqtt());
#endif
#if BOTANIK_FEATURE_LOGS_PUBLISH
        // Stream log entries out to the broker (best-effort, own min level).
        _net.mqtt_log().set_publisher(&_net.mqtt());
        _logger.add_writer(&_net.mqtt_log());
#endif
#if BOTANIK_FEATURE_MQTT
        register_telemetry();   // site telemetry fields (base set always on)
#endif

        on_setup();   // site post-wiring (extra sinks / components)

        // Every registered task's one-time init, before the loop exists.
        // Deliberately HERE and not beside _register_tasks(): this is what runs
        // each component's announce() and HealthMonitor's POST, and a line
        // emitted before the sinks above were registered would go nowhere.
        // After on_setup() too, so a site's own sinks catch them.
        //
        // It also takes the parameter load - hundreds of storage reads, tens of
        // ms of flash - out of the first beat, where it read as a boot overrun
        // against a 10 ms budget.
        _tasks.setup(HAL::Clock::uptime_ms());

        // The parameter load just happened, so this is the first moment the
        // stored UART rates exist. Applied here rather than left to the first
        // beat so the boot lines below go out at the rate the operator actually
        // configured - everything above them was emitted at the compile-time
        // default, which is unavoidable: something has to open the port before
        // the store can be read.
        _apply_serial_config();

        HAL::Watchdog::start(_hw_watchdog_ms);

        // Bring-up is done: the system's hold on control is released, so the
        // field is ours to drive. It starts held (Bank's default), so nothing
        // between power-on and here - a half-wired component, a site hook that
        // faults - can energise an actuator on the way past.
        _io.enable_control(Vars::Bank::INHIBIT_HEALTH);

        // One line per UART so neither overflows 80 cols and each gates with its
        // feature (console/monitor dropped when their UART is absent).
        _logger.debug(Logs::BOOT, "boot complete\nHW watchdog %lu ms",
                      (unsigned long)_hw_watchdog_ms);
#if BOTANIK_FEATURE_CONSOLE
        _logger.debug(Logs::BOOT, "console %lu baud", (unsigned long)_console_baud);
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR
        _logger.debug(Logs::BOOT, "monitor %lu baud", (unsigned long)_monitor_baud);
#endif
    }

    void BotanikFirmware::_resolve_site_refs() {
        if (_site.home_refs != nullptr && _site.home_fields != nullptr)
            for (uint8_t i = 0; i < _site.home_field_count; i++)
                _site.home_refs[i] = _io.find(_site.home_fields[i].name);
    }

    // The task table
    // ------------------------------------------------------------------------

    void BotanikFirmware::_register_tasks() {
        using namespace Tasks;

        // --- INPUT: fresh bytes and fresh field, before anything reads ---
#if BOTANIK_FEATURE_CONSOLE
        // BufferedSerial is one Component whose halves belong at opposite ends
        // of the beat - RX here, TX after the render. Stepping the whole pump at
        // both ends would cost an extra read() each beat and split the console's
        // baud-paced TX across the render.
        _tasks.add("console_rx", GROUP_FAST, PHASE_INPUT,
            [](void* s, uint64_t) { static_cast<Utils::BufferedSerial*>(s)->receive(); },
            &_console.serial());
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
        // The field bus commits its readings here, ahead of expire_stale: the
        // sweep that decides a reading has gone stale has to judge the freshest
        // image, not the one from before this beat's answers.
        _tasks.add("modbus_rtu_rx", GROUP_FAST, PHASE_INPUT,
            [](void* s, uint64_t now) {
                static_cast<Modbus::RtuClient*>(s)->receive_step(now);
            },
            &_modbus_rtu);
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
        // A master's write lands HERE, before the programs run, so a setpoint
        // written this beat is what they run on this beat. add_component, so the
        // map is built and the `mb` group read in Registry::setup() rather than
        // inside the first beat.
        _tasks.add_component("modbus_tcp_rx", GROUP_FAST, PHASE_INPUT, _modbus_tcp);
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR
        _tasks.add("monitor_rx", GROUP_FAST, PHASE_INPUT,
            [](void* s, uint64_t) { static_cast<Utils::BufferedSerial*>(s)->receive(); },
            &_monitor.serial());
#endif
        // A full linear scan of the variable table, resolving timeouts measured
        // in seconds.
        _tasks.add("expire_stale", GROUP_BACKGROUND, PHASE_INPUT,
            [](void* s, uint64_t now) { static_cast<Vars::Bank*>(s)->expire_stale(now); },
            &_io);
        // Beside it, and for the same reason: a reading can be fresh and still
        // be nonsense. A dead 4-20 mA loop reads full scale, which no timeout
        // catches - the declared plausibility band does.
        _tasks.add("reject_implausible", GROUP_BACKGROUND, PHASE_INPUT,
            [](void* s, uint64_t) { (void)static_cast<Vars::Bank*>(s)->reject_implausible(); },
            &_io);

        // --- CONFIG: the image every config-driven component reads below ---
        _tasks.add_component("params", GROUP_SLOW, PHASE_CONFIG, _params);
        // ...and the two rates that are config for the firmware ITSELF. After
        // the params component, so an edit committed this beat is applied this
        // beat rather than the next.
        _tasks.add("serial_cfg", GROUP_SLOW, PHASE_CONFIG,
            [](void* s, uint64_t) {
                static_cast<BotanikFirmware*>(s)->_apply_serial_config();
            }, this);

        // --- SERVICE: supervision and the background state machines ---
        // Health before the logger drains, so an alarm START/END lands this
        // beat, and before EXECUTE, so a fault inhibits this beat's programs.
        _tasks.add_component("health", GROUP_SLOW, PHASE_SERVICE, _health);
        _tasks.add("control_latch", GROUP_FAST, PHASE_SERVICE,
            [](void* s, uint64_t) { static_cast<BotanikFirmware*>(s)->_latch_control(); },
            this);
#if BOTANIK_FEATURE_NETWORK
        _tasks.add_component("net", GROUP_FAST, PHASE_SERVICE, _net);
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
        // The flash half of a master's write, paced. Storage is synchronous and
        // one request can cover 61 parameters, so the commits are queued where
        // they are decoded and made durable a couple per beat here. SERVICE, not
        // PRESENT: this is a side effect of the request, not part of answering
        // it - the answer itself goes out in PRESENT below.
        _tasks.add("modbus_tcp_wr", GROUP_FAST, PHASE_SERVICE,
            [](void* s, uint64_t now) {
                static_cast<Modbus::TcpSlave*>(s)->service_persist(now);
            },
            &_modbus_tcp);
#endif

        // --- EXECUTE: the control programs ---
        // Unconditional: the driver holds the Bank and inhibits the programs
        // itself while control is withheld, so it still announces at boot (when
        // control is withheld by definition) and a future output-collect phase
        // runs DURING a withdrawal rather than being skipped with it.
#if BOTANIK_FEATURE_PLC_SCAN
        _tasks.add_component("scan", GROUP_FAST, PHASE_EXECUTE, _scan);
#endif
        // The site's own control law, under authority - the same gate the scan
        // driver applies to the programs, for the same reason. Ahead of the
        // ungated hook below, so a site's law runs with the programs and its
        // bookkeeping sees the result.
        //
        // The gate is per DISPATCH POINT rather than per write: Var is two
        // pointers passed by value and stays that way, so it cannot check a
        // latch it has no route to. That makes this list exhaustive by
        // construction - the scan driver and this - and a third place that
        // drives the field has to come and gate itself here.
        _tasks.add("site_ctl", GROUP_FAST, PHASE_EXECUTE,
            [](void* s, uint64_t now) {
                BotanikFirmware* f = static_cast<BotanikFirmware*>(s);
                if (f->io().control_enabled()) f->on_control(now);
            },
            this);
        _tasks.add("site", GROUP_FAST, PHASE_EXECUTE,
            [](void* s, uint64_t now) { static_cast<BotanikFirmware*>(s)->on_step(now); },
            this);

        // --- OUTPUT: hand what the programs drove to the field ---
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
        // add_component: on_step() IS the transmit half, and this is also what
        // runs the port bring-up and the bus planning in Registry::setup()
        // rather than inside the first beat.
        _tasks.add_component("modbus_rtu_tx", GROUP_FAST, PHASE_OUTPUT, _modbus_rtu);
#endif

        // --- PRESENT: report what just happened ---
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
        // The answer goes out off the image the scan just produced: one beat of
        // latency on a read, against a poll measured in seconds, and no request
        // ever answered from a half-updated image.
        _tasks.add("modbus_tcp_tx", GROUP_FAST, PHASE_PRESENT,
            [](void* s, uint64_t now) {
                static_cast<Modbus::TcpSlave*>(s)->service_tx(now);
            },
            &_modbus_tcp);
#endif
#if BOTANIK_FEATURE_LOGGING
        _tasks.add("logger", GROUP_FAST, PHASE_PRESENT,
            [](void* s, uint64_t) { static_cast<Logs::Logger*>(s)->refill(); },
            &_logger);
#endif
#if BOTANIK_FEATURE_CONSOLE
        // The Console is the Component, not the stack around it: the stack's
        // serial pump is registered separately, at its own phases.
        _tasks.add_component("console", GROUP_SLOW, PHASE_PRESENT, _console.terminal());
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR
        _tasks.add_component("monitor_log", GROUP_FAST, PHASE_PRESENT, _monitor.writer());
#endif
#if BOTANIK_FEATURE_CONSOLE
        _tasks.add("console_tx", GROUP_FAST, PHASE_PRESENT,
            [](void* s, uint64_t) { static_cast<Utils::BufferedSerial*>(s)->transmit(); },
            &_console.serial());
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR
        _tasks.add("monitor_tx", GROUP_FAST, PHASE_PRESENT,
            [](void* s, uint64_t) { static_cast<Utils::BufferedSerial*>(s)->transmit(); },
            &_monitor.serial());
#endif

        if (_tasks.overflow() != 0)
            _logger.critical(Logs::BOOT, "%u task(s) refused: MAX_TASKS is %u",
                             (unsigned)_tasks.overflow(), (unsigned)Config::MAX_TASKS);
    }

    // Per-beat internals
    // ------------------------------------------------------------------------

    void BotanikFirmware::_apply_serial_config() {
#if BOTANIK_FEATURE_CONSOLE
        const uint32_t con = (uint32_t)Vars::get_int(&_io, "serial_console_baud",
                                                     (int32_t)_console_baud);
        if (con != _console_baud && con != 0) {
            _console_baud = con;
            // Both, together: the wire and the budget the renderer paces to.
            // Either alone is the bug this stayed compile-time to avoid.
            HAL::Serial::get(HAL::Serial::CONSOLE)->begin(con);
            _console.terminal().terminal().set_baud(con);
            _logger.notice(Logs::BOOT, "console now %lu baud", (unsigned long)con);
        }
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR
        const uint32_t mon = (uint32_t)Vars::get_int(&_io, "serial_monitor_baud",
                                                     (int32_t)_monitor_baud);
        if (mon != _monitor_baud && mon != 0) {
            _monitor_baud = mon;
            HAL::Serial::get(HAL::Serial::MONITOR)->begin(mon);
            _logger.notice(Logs::BOOT, "monitor now %lu baud", (unsigned long)mon);
        }
#endif
    }

    void BotanikFirmware::_latch_control() {
        if (!_health.watchdog_ok()) {
            if (!_health_fault) {
                _health_fault = true;
                _io.disable_control(Vars::Bank::INHIBIT_HEALTH);
                _logger.critical(Logs::HEALTH,
                    "control disabled\n"
                    "outputs at their safe state, scan inhibited");
            }
        } else if (_health_fault) {
            _health_fault = false;
            _io.enable_control(Vars::Bank::INHIBIT_HEALTH);
            // The lead is hoisted out of the ternary, so the format string is a
            // literal the printf attribute can still check, and both outcomes
            // grep as "health recovered".
            _logger.warning(Logs::HEALTH, "health recovered\n%s",
                _io.control_enabled()
                    ? "control enabled, scan resuming"
                    : "control still disabled by the operator");
        }
    }

    // Site customization hooks (defaults)
    // ------------------------------------------------------------------------

#if BOTANIK_FEATURE_CONSOLE
    void BotanikFirmware::register_pages() {
        // Whichever page registers FIRST is page 0, the boot page. That is
        // positional, not a property of HOME: with HOME_PAGE off the boot page
        // is simply the next one that exists.
#if BOTANIK_FEATURE_HOME_PAGE
        _console.terminal().add_page(&_home_page);
#endif
#if BOTANIK_FEATURE_SHELL_PAGE
        _console.terminal().add_page(&_console.shell());
#endif
#if BOTANIK_FEATURE_LOGS_PAGE
        _console.terminal().add_page(&_console.logs());
#endif
#if BOTANIK_FEATURE_ALARMS_PAGE
        _console.terminal().add_page(&_console.alarms());
#endif
#if BOTANIK_FEATURE_PARAMS_PAGE
        _console.terminal().add_page(&_params_page);
#endif
#if BOTANIK_FEATURE_MONITOR_PAGE
        _console.terminal().add_page(&_monitor_page);
#if BOTANIK_FEATURE_PLC_SCAN
        // Only this class holds both the page and the scan driver, so the join
        // is made here rather than by widening either of them.
        _monitor_page.set_writer_names(
            [](const void* ctx, uint8_t w) -> const char* {
                const Scan::Driver* d = static_cast<const Scan::Driver*>(ctx);
                const Plc::Program* p = d->program(w);
                return (p != nullptr) ? p->name() : nullptr;
            },
            &_scan);
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
        // ...and the plant's name for a unit id, from the same place the
        // field-bus log lines get it.
        _monitor_page.set_device_names(
            [](const void* ctx, uint8_t unit) -> const char* {
                return Modbus::device_tag(
                    static_cast<const Modbus::DeviceTable*>(ctx), unit);
            },
            _site.devices);
#endif
#endif
#if BOTANIK_FEATURE_TASKS_PAGE
        _console.terminal().add_page(&_tasks_page);
#endif
#if BOTANIK_FEATURE_NOTES_PAGE
        _console.terminal().add_page(&_console.notes());
#endif
    }

#if BOTANIK_FEATURE_SHELL_PAGE
    void BotanikFirmware::register_commands() {
        _console.shell().set_commands(_site.commands, _site.command_count,
                                      (void*)&_io);
    }
#endif
#endif  // BOTANIK_FEATURE_CONSOLE

#if BOTANIK_FEATURE_MQTT
    void BotanikFirmware::register_telemetry() {
        telemetry().set_variables(&_io);
    }
#endif

}  // namespace Botanik
