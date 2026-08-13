// REVIEW_35: TODO
/**
 * @file firmware.cpp
 * @brief BotanikFirmware: construction, bring-up, and the task table.
 *
 * Everything cold. The header keeps only the class, the accessors and loop() -
 * the hot path, and the frame the scheduler's stack painting is measured
 * against.
 */
#include "libbotanik/firmware/firmware.h"   // BotanikFirmware - implemented here

namespace Botanik {

    // Construction
    // ------------------------------------------------------------------------

    // The initialiser list follows declaration order exactly (-Wreorder): _io
    // must be alive before _params takes its address, and _health before
    // _modbus_tcp and _console take its alarm model.
    BotanikFirmware::BotanikFirmware(const SiteDescriptor& site)
        : _site(site),
          _logger(),
          _scheduler(_tasks, Config::TASK_PERIOD_MS, &_logger),
          _io(site.vars),
          // Params come from local storage.
          _param_store(),
          _params(&_io, &_param_store, &_logger),
          _own_tasks(*this),
          // The alarm model scans the variable table for SCOPE_A rows;
          // platform-health alarms are appended and driven locally.
          _health(&_logger, _scheduler.context(), &_io)
          , _sys_ctx()   // fields wired in the body, once addresses are known
#if BOTANIK_FEATURE_LOGS_MONITOR
          , _monitor(&_logger, &_io)
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
          // The console borrows _sys_ctx (declared above, still empty here) by
          // pointer and reads it once the body below has filled it in.
          , _console(Config::CONSOLE_COLS, Config::CONSOLE_ROWS,
                     &_scheduler.context()->timestamp_ms, _console_baud,
                     &_health.alarm_model(), &_logger,
                     _scheduler.context(), &_sys_ctx, &_io)
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
        _sys_ctx.params    = &_params;
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
        // Where a site's names become indices, once, so no renderer pays for
        // the lookup per frame. A name with no row lands on the sink and is
        // counted by the bank, which the site test asserts is zero.
        _resolve_site_refs();

        // What runs on the beat, in which phase, and how often.
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


        // First moment the stored UART rates exist, so the boot lines below go
        // out at the configured rate. Everything above them is at the
        // compile-time default: the port must be open before the store is read.
#if BOTANIK_FEATURE_CONSOLE
        // Likewise the console, and here rather than on the first beat because
        // nothing has been drawn yet: it establishes its screen on its first
        // step(), so picking the stored settings up now costs one repaint
        // instead of two.
        _console.terminal().apply_stored_config();
        _monitor.apply_stored_config();
#endif

        _scheduler.setup(HAL::Clock::uptime_ms(), _hw_watchdog_ms);

        // Bring-up is done: release the system's hold on control. It starts held
        // (Bank's default), so nothing between power-on and here can energise an
        // actuator on the way past.
        _io.set_health_hold(false);

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

#if BOTANIK_FEATURE_MONITOR_PAGE
#if BOTANIK_FEATURE_PLC_SCAN
    const char* BotanikFirmware::_program_name_of(const void* ctx, uint8_t writer) {
        const BotanikFirmware* f = static_cast<const BotanikFirmware*>(ctx);
        const Plc::Program* p = f->_tasks.program(writer);
        return (p != nullptr) ? p->name() : nullptr;
    }
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
    const char* BotanikFirmware::_device_name_of(const void* ctx, uint8_t unit) {
        return Modbus::device_tag(
            static_cast<const Modbus::DeviceTable*>(ctx), unit);
    }
#endif
#endif

    void BotanikFirmware::_resolve_site_refs() {
        if (_site.home_refs != nullptr && _site.home_fields != nullptr)
            for (uint8_t i = 0; i < _site.home_field_count; i++)
                _site.home_refs[i] = _io.find(_site.home_fields[i].name);
#if BOTANIK_FEATURE_PLC_SCAN
        // By name, once, like every other library consumer. A site that omitted
        // the library splice resolves to the sink and the POST names the row.
        _scan_late_alarm = _io.find("alarm_scan_late");
#endif
    }

    // The task table
    // ------------------------------------------------------------------------

    // One named static per method (see OwnTasks). Each reaches the enclosing
    // firmware through the nested class's reference to it.
    void BotanikFirmware::OwnTasks::_site_input(Component* c, const Tasks::Tick& t) {
        static_cast<OwnTasks*>(c)->_fw.on_input(t.now_ms);
    }
    void BotanikFirmware::OwnTasks::_site_config(Component* c, const Tasks::Tick& t) {
        static_cast<OwnTasks*>(c)->_fw.on_config(t.now_ms);
    }
    void BotanikFirmware::OwnTasks::_control_latch(Component* c, const Tasks::Tick& t) {
        static_cast<OwnTasks*>(c)->_fw._latch_control(t.now_ms);
    }
    void BotanikFirmware::OwnTasks::_site_service(Component* c, const Tasks::Tick& t) {
        static_cast<OwnTasks*>(c)->_fw.on_service(t.now_ms);
    }
    void BotanikFirmware::OwnTasks::_site_control(Component* c, const Tasks::Tick& t) {
        static_cast<OwnTasks*>(c)->_fw._run_site_control(t.now_ms);
    }
    void BotanikFirmware::OwnTasks::_site_output(Component* c, const Tasks::Tick& t) {
        static_cast<OwnTasks*>(c)->_fw.on_output(t.now_ms);
    }
    void BotanikFirmware::OwnTasks::_site_present(Component* c, const Tasks::Tick& t) {
        static_cast<OwnTasks*>(c)->_fw.on_present(t.now_ms);
    }

    // The firmware's own eight rows: two bits of housekeeping and the six site
    // hooks. Each names its phase and rate here, beside the others, so the beat
    // still reads top to bottom in _register_tasks() below.
    void BotanikFirmware::OwnTasks::register_tasks(Tasks::Registry& r) {
        using namespace Tasks;
        r.add(*this, &_site_input,
              "site_input",    GROUP_FAST, PHASE_INPUT);
        r.add(*this, &_site_config,
              "site_config",   GROUP_FAST, PHASE_CONFIG);
        r.add(*this, &_control_latch,
              "control_latch", GROUP_FAST, PHASE_SERVICE);
        r.add(*this, &_site_service,
              "site_service",  GROUP_FAST, PHASE_SERVICE);
        // The site's own control law, under the same gate the registry applies
        // to the programs.
        r.add(*this, &_site_control,
              "site_control",  GROUP_FAST, PHASE_CONTROL);
        r.add(*this, &_site_output,
              "site_output",   GROUP_FAST, PHASE_OUTPUT);
        r.add(*this, &_site_present,
              "site_present",  GROUP_FAST, PHASE_PRESENT);
    }

    void BotanikFirmware::_register_tasks() {
        using namespace Tasks;

        // This firmware's own rows, in every phase at once. Registration order
        // within a phase is execution order, so they land ahead of everything
        // registered below - which is where the site hooks belong: a site's
        // per-phase work runs before the library's own for that phase.
        _own_tasks.register_tasks(_tasks);

        // --- INPUT: fresh bytes and fresh field, before anything reads ---
        // Both field-bus components span the beat, and each registers every
        // phase it acts in at once: the RTU master takes the field in at INPUT
        // and issues at OUTPUT, the TCP slave decodes at INPUT, persists at
        // SERVICE and answers at PRESENT. First, so their INPUT halves commit
        // ahead of the bank's expiry sweep below.
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
        _modbus_rtu.register_tasks(_tasks);
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
        _modbus_tcp.register_tasks(_tasks);
#endif
        // The bank's own two sweeps: age a row out, and reject one that is
        // fresh and still nonsense. Both before anything reads, so one beat is
        // coherent.
        _io.register_tasks(_tasks);



        // --- CONFIG: the image every config-driven component reads below ---
        _params.register_tasks(_tasks);




        // --- SERVICE: supervision and the background state machines ---
        // Health before the logger drains, so an alarm START/END lands this
        // beat, and before CONTROL, so a fault inhibits this beat's programs.
        _health.register_tasks(_tasks);   // health, then the alarm model

#if BOTANIK_FEATURE_NETWORK
        _net.register_tasks(_tasks);   // network, ntp, mqtt, telemetry
#endif



        // --- CONTROL: the control programs ---
        // The site's own control law, under the same gate the registry applies
        // to the programs. The gate is per DISPATCH POINT, not per write (see
        // on_control()), so the programs and this are the whole list: a third
        // place that drives the field must gate itself here.


        // --- OUTPUT: hand what the programs drove to the field ---


        // --- PRESENT: report what just happened ---
        // First in the phase, so anything it logs is drained by the logger below
        // in the SAME beat rather than waiting for the next one.

#if BOTANIK_FEATURE_LOGGING
        _logger.register_tasks(_tasks);
#endif
#if BOTANIK_FEATURE_CONSOLE
        // Three tasks, and the port's two are not among them: console_in at the
        // top of the beat, then the draw and the flush here. After the logger,
        // so a line logged this beat is in the pages before they are drawn.
        _console.terminal().register_tasks(_tasks);
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR
        _monitor.register_tasks(_tasks);
#endif

        if (_tasks.overflow() != 0)
            _logger.critical(Logs::BOOT, "%u task(s) refused: MAX_TASKS is %u",
                             (unsigned)_tasks.overflow(), (unsigned)Config::MAX_TASKS);
    }

    // Per-beat internals
    // ------------------------------------------------------------------------


    void BotanikFirmware::_run_site_control(uint64_t now_ms) {
        // The gate is per DISPATCH POINT, not per write (see on_control()), so
        // the programs and this are the whole list: a third place that drives
        // the field must gate itself here.
        if (_io.control_enabled()) on_control(now_ms);
    }

    void BotanikFirmware::_latch_control(uint64_t now_ms) {
        (void)now_ms;
        if (!_health.watchdog_ok()) {
            if (!_health_fault) {
                _health_fault = true;
                _io.set_health_hold(true);
                _logger.critical(Logs::HEALTH,
                    "control disabled\n"
                    "outputs at their safe state, scan inhibited");
            }
        } else if (_health_fault) {
            _health_fault = false;
            _io.set_health_hold(false);
            // Lead hoisted out of the ternary: the format string stays a
            // literal the printf attribute can check, and both outcomes grep
            // as "health recovered".
            _logger.warning(Logs::HEALTH, "health recovered\n%s",
                _io.control_enabled()
                    ? "control enabled, scan resuming"
                    : "control still disabled by the operator");
        }
        _apply_control_authority();
    }

    void BotanikFirmware::_apply_control_authority() {
        // The whole edge, in one call: the gate, and on the way back up the
        // timer reseed and the first-scan flag. Idempotent, so there is no edge
        // to track here.
        _scheduler.set_control_enabled(_io.control_enabled(),
                                       HAL::Clock::uptime_ms());
#if BOTANIK_FEATURE_PLC_SCAN
        _publish_lateness();
        // Edge-triggered and named once: two programs driving one output is a
        // design defect, true on every beat from here.
        (void)Plc::report_writer_conflicts(_io, _tasks, &_logger, _conflicts_said);
#endif
    }

#if BOTANIK_FEATURE_PLC_SCAN
    void BotanikFirmware::_publish_lateness() {
        const Plc::Program* worst = nullptr;
        const uint32_t late = Plc::worst_lateness(_tasks, &worst);
        _scan_worst = worst;
        _scan_late_ms = late;
        // Written every beat, like any other alarm input: the dwell in the row's
        // own policy is what turns a burst into an alarm, not this.
        _io.var(_scan_late_alarm).set(late > Config::SCAN_LATE_MS);
    }

#endif

    // Site customization hooks (defaults)
    // ------------------------------------------------------------------------

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
#if BOTANIK_FEATURE_PLC_SCAN
        // Only this class holds both the page and the site's program table, so
        // the join is made here rather than by widening either of them.
        _monitor_page.set_writer_names(&_program_name_of, this);
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
        // ...and the plant's name for a unit id, from the same place the
        // field-bus log lines get it.
        _monitor_page.set_device_names(&_device_name_of, _site.devices);
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
