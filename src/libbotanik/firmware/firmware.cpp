/**
 * @file firmware.cpp
 * @brief BotanikFirmware: construction, bring-up, and the task table.
 *
 * Everything cold. The header keeps only the class, the accessors and loop(),
 * which is the hot path.
 */
#include "libbotanik/firmware/firmware.h"

namespace Botanik {

    // Construction

    // The initialiser list follows declaration order exactly (-Wreorder):
    // _health before _modbus_tcp and _console take its alarm model.
    BotanikFirmware::BotanikFirmware(const SiteDescriptor& site)
        : _site(site),
          _logger(),
          _scheduler(_tasks, Config::TASK_PERIOD_MS, &_logger),
          _setup(_tasks, &_logger),
          _io(_setup),
          // A row's durable copy comes from local storage; Persist owns it.
          _param_store(),
          _persist(_setup, _io),
          _binder(_setup, &_io),
          // The alarm model scans the variable table for K_ALARM rows;
          // platform-health alarms are appended and driven locally.
#if BOTANIK_FEATURE_SUN_TRACKER
          _sun(_setup),
#endif
#if BOTANIK_FEATURE_PLC
          _st(_setup, _io),
#endif
          _health(_setup, _scheduler.context(), &_io)
          , _sys_ctx()   // fields wired in the body, once addresses are known
#if BOTANIK_FEATURE_LOGS_MONITOR
          , _monitor(_setup)
#endif
#if BOTANIK_FEATURE_NETWORK
          // Network reads its config from the variable bank by name;
          // telemetry publishes the live _sys_ctx (read later).
          , _net(_setup, &_sys_ctx)
#endif
          , _local_in(_setup, _io)
          , _local_out(_setup, _io)
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
          , _modbus_rtu(_setup, &_io, site.devices)
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
          // The alarm model is the ack block's target; it is HealthMonitor's,
          // and this member is declared after it so the reference is live.
          , _modbus_tcp(_setup, &_io, &_health.alarm_model())
#endif
#if BOTANIK_FEATURE_CONSOLE
          // The console borrows _sys_ctx (declared above, still empty here) by
          // pointer and reads it once the body below has filled it in.
          , _console(&_scheduler.context()->timestamp_ms,
                     &_health.alarm_model(), _setup,
                     _scheduler.context(), &_sys_ctx)
          // Base-owned pages, built from the descriptor + the live bank.
#if BOTANIK_FEATURE_HOME_PAGE
          , _home_page(_console.terminal(), _site.home_fields,
                       _site.home_field_count, &_io)
#endif
#if BOTANIK_FEATURE_PARAMS_PAGE
          , _params_page(_console.terminal(), &_io, &_persist)
#endif
#if BOTANIK_FEATURE_MONITOR_PAGE
          , _monitor_page(_console.terminal(), &_io, &_persist)
#endif
#if BOTANIK_FEATURE_TASKS_PAGE
          , _tasks_page(_console.terminal(), _tasks)
#endif
#if BOTANIK_FEATURE_SYSTEM_PAGE
          // _sys_ctx is filled in this constructor's BODY, below; the page only
          // reads through the pointer at draw time, so taking its address here
          // is fine.
          , _system_page(_console.terminal(), &_sys_ctx,
                         site.system_fields, site.system_field_count, &_io)
#endif
#endif
    {
        // The tracker, to whatever is built with this context after us. A site
        // program is a derived member, so it is constructed after this body
        // runs and sees the pointer already set.
#if BOTANIK_FEATURE_SUN_TRACKER
        _setup.sun = &_sun;
#endif

        // Bind each component's config to the rows behind it, now every one of
        // them exists. Pushed at PHASE_CONFIG, and once at bring-up. With no
        // table there is nothing to bind: each component keeps what it was
        // constructed with, which is the contract ConfigBinder states.
#if defined(BOTANIK_SITE_ROWS)
#if BOTANIK_FEATURE_LOGGING
        // Every sink's own level row. Without this the five *_log_level rows
        // commit and persist and each writer keeps what it was constructed with.
        Vars::bind_logger(_binder, _logger);
#endif
#if BOTANIK_FEATURE_SUN_TRACKER
        Vars::bind_sun(_binder, _sun);
#endif
#if BOTANIK_FEATURE_CONSOLE
        Vars::bind_console(_binder, _console.terminal());
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR
        Vars::bind_monitor(_binder, _monitor);
#endif
#if BOTANIK_FEATURE_NETWORK
        Vars::bind_network(_binder, _net.network());
#endif
#if BOTANIK_FEATURE_NTP
        Vars::bind_ntp(_binder, _net.ntp());
#endif
#if BOTANIK_FEATURE_MQTT
        Vars::bind_mqtt(_binder, _net.mqtt());
        Vars::bind_telemetry(_binder, _net.telemetry());
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
        Vars::bind_modbus_tcp(_binder, _modbus_tcp);
#endif
#endif // BOTANIK_SITE_ROWS

        // Wire the status aggregate now every subsystem address is fixed.
        _sys_ctx.scheduler = _scheduler.context();
        _sys_ctx.tasks     = &_tasks;
#if BOTANIK_FEATURE_PLC
        _sys_ctx.plant     = &_st;
#endif
        _sys_ctx.vars      = &_io;
        _sys_ctx.persist   = &_persist;
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

    void BotanikFirmware::setup() {
        // route BOTANIK_ASSERT failures to the log
        Logs::route_asserts(&_logger);
        // The rows, before anything else in bring-up. The table is generated
        // whole - the library's rows merged into the site's, ordered, indexed
        // and checked - so this lays out the string pool and seeds every slot
        // from its declared default, and there is nothing here that can fail.
        _io.init(_site.rows, _site.order, _site.row_count,
                 _site.param_first, _site.param_count);
        register_vars();
#if BOTANIK_FEATURE_PLC
        // Points the ST runtime at the bank and runs every generated bind.
        _st.wire();
        _st.set_run_state(&_scheduler.context()->run_state);
        // So the quality log can tell a roster change from an instrument dying.
        _health.set_plant(&_st);
#endif
#if BOTANIK_FEATURE_SUN_TRACKER
        // The tracker publishes its own rows, so a site gets the sun by turning
        // the feature on rather than by writing a program to copy the
        // instrument onto them.
        _sun.wire(_io);
#endif
        // Persist holds the store: the bank is RAM-only, and Persist::on_setup()
        // loads the image from it in the setup pass below.
        _persist.set_store(&_param_store);
        // ...and it is what health writes the record through on the way down.
        _health.set_persist(&_persist);

        // What runs on the beat, in which phase, and how often.
        _register_tasks();

        // The factory rate, so the boot lines have a wire before the store is
        // read. The binder retunes both ports once the parameters land.
#if BOTANIK_FEATURE_CONSOLE
        HAL::Serial::get(HAL::Serial::CONSOLE)->begin(Terminal::ConsoleConfig().baud);
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR
        HAL::Serial::get(HAL::Serial::MONITOR)->begin(MonitorConfig().baud);
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
        // Let the shell's logs / alarms / reboot commands reach the live
        // logger, alarm model and health monitor.
        _console.shell().set_diagnostics(&_logger, &_health.alarm_model(), &_health);
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

#if BOTANIK_FEATURE_NETWORK
        // The monitor raises the row; each component knows its own state.
        _health.set_network(&_net.network());
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
        // ...and the slave gives its port back across a link teardown rather
        // than reporting a bound socket on a link that is gone.
        _modbus_tcp.set_network(&_net.network());
#endif
#endif
#if BOTANIK_FEATURE_NTP
        _health.set_ntp(&_net.ntp());
#endif

#if BOTANIK_FEATURE_ALARMS_PUBLISH
        // Route alarm transitions out through the MQTT client (it is an
        // Alarms::Publisher).
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

        // The site's own components and programs register here, on top of the
        // library's - still ahead of the setup pass below, so they are set up
        // in the same sweep as everything else.
        on_setup();

        // Every registered task's one-time init. It must come after every sink
        // above (and after on_setup(), for a site's own): this is what runs each
        // component's announce() and HealthMonitor's POST. It also keeps the
        // parameter load, tens of ms of flash, out of the first beat.

        _scheduler.setup(HAL::Clock::uptime_ms(), _hw_watchdog_ms);

        // AFTER the line above, which is what loads the parameters: what each
        // component is handed here is the store, not the defaults.
        _binder.apply_all();

        // SETUP is over. The beat runs from here; the laws wait out
        // sys_startup_sec in START, which loop() counts.
        _scheduler.context()->run_state = Scheduler::RunState::START;
        _logger.debug(Logs::BOOT, "setup done, outputs are safe, starting...");

        // One line per UART so neither overflows 80 cols and each gates with its
        // feature (console/monitor dropped when their UART is absent).
        _logger.debug(Logs::BOOT, "boot complete\nHW watchdog %lu ms",
                      (unsigned long)_hw_watchdog_ms);
#if BOTANIK_FEATURE_CONSOLE
        _logger.debug(Logs::BOOT, "console %lu baud",
                      (unsigned long)_console.terminal().baud());
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR
        _logger.debug(Logs::BOOT, "monitor %lu baud", (unsigned long)_monitor.baud());
#endif
    }

#if BOTANIK_FEATURE_MONITOR_PAGE
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
    const char* BotanikFirmware::_device_name_of(const void* ctx, uint8_t unit) {
        return Modbus::device_tag(
            static_cast<const Modbus::DeviceTable*>(ctx), unit);
    }
#endif
#endif

    // The task table

    // The task table
    //
    // A component registers EVERY phase it acts in with one call, so a few
    // appear under the section where their first entry lands and say where
    // else they go. Within a phase, registration order is dispatch order;
    // across phases it is the phase that decides, so nothing here can reorder
    // the beat.
    void BotanikFirmware::_register_tasks() {
        using namespace Tasks;

        // INPUT: fresh bytes and fresh field, before anything reads
        // The controller's own terminals, before the bus: both fill rows the
        // programs read this beat, and this one cannot fail or block.
        _local_in.register_tasks();
        _local_out.register_tasks();
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
        // Spans the beat: takes the field in here, issues at OUTPUT.
        _modbus_rtu.register_tasks();
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
        // Spans three: decodes here, persists at SERVICE, answers at PRESENT.
        _modbus_tcp.register_tasks();
#endif
        // The bank's own two sweeps, after the drivers have committed: age a
        // row out, and reject one that is fresh and still nonsense. Both before
        // anything reads, so one beat is coherent.
        _io.register_tasks();
        // ...and the durable half, on its own cadence.
        _persist.register_tasks();
        // AFTER the expiry sweep: a row that just aged out is invalid only once
        // that has run, and a companion copied before it would be a beat stale
        // on exactly the transition that matters.

        // CONFIG: a commit reaches its component
        _binder.register_tasks();

        // SERVICE: supervision and the background state machines
#if BOTANIK_FEATURE_SUN_TRACKER
        // A slow recompute of its own, not something a control phase does: the
        // programs that read it just read rows.
        _sun.register_tasks();
#endif
        // Health, then the alarm model. The PHASE is what puts both ahead of
        // the laws, so a fault inhibits this beat's control rather than the
        // next one; registering here only orders them against each other.
        _health.register_tasks();
#if BOTANIK_FEATURE_NETWORK
        _net.register_tasks();   // network, ntp, mqtt, telemetry
#endif

        // CONTROL: the site's laws
#if BOTANIK_FEATURE_PLC
        // Every one of them, in the order tables/programs.csv states.
        _st.register_tasks();
#endif

        // OUTPUT: what the programs drove goes to the field
        // The RTU master's other half, registered with its INPUT one above.

        // PRESENT: report what just happened
#if BOTANIK_FEATURE_LOGGING
        // No task for the logger: its buckets refill from elapsed time at the
        // moment they are read (Logger::_emit).
#endif
#if BOTANIK_FEATURE_CONSOLE
        // Three tasks: console_rx at the top of the beat, then the draw and the
        // flush here.
        _console.terminal().register_tasks();
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR
        _monitor.register_tasks();
#endif

        if (_tasks.overflow() != 0)
            _logger.critical(Logs::BOOT, "%u task(s) refused: MAX_TASKS is %u",
                             (unsigned)_tasks.overflow(), (unsigned)Config::MAX_TASKS);
    }

    // Per-beat internals


    // Site customization hooks (defaults)

#if BOTANIK_FEATURE_CONSOLE
    void BotanikFirmware::register_pages() {
        // Whichever page registers FIRST is page 0, the boot page: positional,
        // not a property of HOME. With HOME_PAGE off it is the next one that
        // exists.
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
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
        // ...and the plant's name for a unit id, from the same place the
        // field-bus log lines get it.
        _monitor_page.set_device_names(&_device_name_of, _site.devices);
#endif
#endif
#if BOTANIK_FEATURE_TASKS_PAGE
        _console.terminal().add_page(&_tasks_page);
#endif
#if BOTANIK_FEATURE_SYSTEM_PAGE
        _console.terminal().add_page(&system_page());
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
        telemetry().set_run_state(&_scheduler.context()->run_state);
    }
#endif

}  // namespace Botanik
