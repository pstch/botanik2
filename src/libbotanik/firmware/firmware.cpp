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
    // must be alive before _params and _scan take its address, and _health
    // before _modbus_tcp and _console take its alarm model.
    BotanikFirmware::BotanikFirmware(const SiteDescriptor& site)
        : _site(site),
          _logger(),
          _scheduler(Config::TASK_PERIOD_MS, &_logger),
          _tasks(_scheduler.tasks()),
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
          // The console borrows _sys_ctx (declared above, still empty here) by
          // pointer and reads it once the body below has filled it in.
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
        _sys_ctx.params    = &_params;
#if BOTANIK_FEATURE_CONSOLE
        _sys_ctx.console_cfg = &_console_cfg;
#endif
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
        // Let the shell's alarm list/ack command reach the live model.
        _console.shell().set_alarms(&_health.alarm_model());
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

        on_setup();   // site post-wiring (extra sinks / components)

        // Every registered task's one-time init. It must come after every sink
        // above (and after on_setup(), for a site's own): this is what runs each
        // component's announce() and HealthMonitor's POST. It also keeps the
        // parameter load, tens of ms of flash, out of the first beat.


        // First moment the stored UART rates exist, so the boot lines below go
        // out at the configured rate. Everything above them is at the
        // compile-time default: the port must be open before the store is read.
        _apply_serial_config();
#if BOTANIK_FEATURE_CONSOLE
        // Likewise the console, and here rather than on the first beat because
        // nothing has been drawn yet: it establishes its screen on its first
        // step(), so picking the stored settings up now costs one repaint
        // instead of two. No probation - what is in the store was confirmed
        // before it got there, so there is nobody to ask and nothing to undo.
        _apply_console_config(/*allow_probation=*/false);
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
        // One Component registered at both ends of the beat: RX here, TX after
        // the render. Two tasks, so each half keeps its own cost on TASKS.
        _tasks.add_component<PHASE_INPUT>("console_rx", GROUP_FAST, _console.serial());
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
        // The field bus commits its readings ahead of expire_stale, which must
        // judge the freshest image and not the one from before this beat's
        // answers.
        _tasks.add_component<PHASE_INPUT>("modbus_rtu_rx", GROUP_FAST, _modbus_rtu);
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
        // A master's write lands HERE, before the programs run, so a setpoint
        // written this beat is what they run on this beat. add_component, so the
        // map is built in Registry::setup() rather than inside the first beat.
        _tasks.add_component<PHASE_INPUT>("modbus_tcp_rx", GROUP_FAST, _modbus_tcp);
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR
        _tasks.add_component<PHASE_INPUT>("monitor_rx", GROUP_FAST, _monitor.serial());
#endif
        // A full linear scan of the variable table, resolving timeouts measured
        // in seconds.
        _tasks.add("expire_stale", GROUP_BACKGROUND, PHASE_INPUT,
            [](void* s, uint64_t now) { static_cast<Vars::Bank*>(s)->expire_stale(now); },
            &_io);
        // Beside it: a reading can be fresh and still be nonsense. A dead
        // 4-20 mA loop reads full scale, which no timeout catches - the
        // declared plausibility band does.
        _tasks.add("reject_implausible", GROUP_BACKGROUND, PHASE_INPUT,
            [](void* s, uint64_t) { (void)static_cast<Vars::Bank*>(s)->reject_implausible(); },
            &_io);

        _tasks.add("site_input", GROUP_FAST, PHASE_INPUT,
                   &_site_hook<&BotanikFirmware::on_input>, this);

        // --- CONFIG: the image every config-driven component reads below ---
        _tasks.add_component<PHASE_CONFIG>("params", GROUP_SLOW, _params);
        // ...and what is config for the firmware ITSELF. After the params
        // component, so an edit committed this beat is applied this beat rather
        // than the next.
        _tasks.add("serial_cfg", GROUP_SLOW, PHASE_CONFIG,
            [](void* s, uint64_t) {
                static_cast<BotanikFirmware*>(s)->_apply_serial_config();
            }, this);
#if BOTANIK_FEATURE_CONSOLE
        // The terminal the console draws on, after the rates: one commit can
        // move both, and a repaint belongs at the rate it will actually go out
        // at rather than the one it was queued at.
        _tasks.add("console_cfg", GROUP_SLOW, PHASE_CONFIG,
            [](void* s, uint64_t) {
                static_cast<BotanikFirmware*>(s)->_apply_console_config();
            }, this);
#endif
#if BOTANIK_FEATURE_LOGGING
        // The one sys_ row with a consumer that keeps its own copy. The rest are
        // read live where they are used (health.h thresholds, Params' retain
        // cadence, the telemetry sender), which needs no task at all.
        _tasks.add("log_cfg", GROUP_SLOW, PHASE_CONFIG,
            [](void* s, uint64_t) {
                static_cast<BotanikFirmware*>(s)->_apply_log_config();
            }, this);
#endif

        _tasks.add("site_config", GROUP_FAST, PHASE_CONFIG,
                   &_site_hook<&BotanikFirmware::on_config>, this);

        // --- SERVICE: supervision and the background state machines ---
        // Health before the logger drains, so an alarm START/END lands this
        // beat, and before CONTROL, so a fault inhibits this beat's programs.
        _health.register_tasks(_tasks);   // health, then the alarm model
        _tasks.add("control_latch", GROUP_FAST, PHASE_SERVICE,
            [](void* s, uint64_t) { static_cast<BotanikFirmware*>(s)->_latch_control(); },
            this);
#if BOTANIK_FEATURE_NETWORK
        _tasks.add_component<PHASE_SERVICE>("net", GROUP_FAST, _net);
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
        // The flash half of a master's write, paced: storage is synchronous and
        // one request can cover 61 parameters, so commits are queued where they
        // are decoded and made durable a couple per beat here. SERVICE, not
        // PRESENT: a side effect of the request, not part of answering it - the
        // answer itself goes out in PRESENT below.
        _tasks.add_component<PHASE_SERVICE>("modbus_tcp_wr", GROUP_FAST, _modbus_tcp);
#endif

        _tasks.add("site_service", GROUP_FAST, PHASE_SERVICE,
                   &_site_hook<&BotanikFirmware::on_service>, this);

        // --- CONTROL: the control programs ---
        // Unconditional: the driver holds the Bank and inhibits the programs
        // itself while control is withheld, so it announces at boot (when
        // control is withheld by definition) and its non-driving work runs
        // DURING a withdrawal rather than being skipped with it.
#if BOTANIK_FEATURE_PLC_SCAN
        _tasks.add_component<PHASE_CONTROL>("scan", GROUP_FAST, _scan);
#endif
        // The site's own control law, under the same gate the scan driver
        // applies to the programs. The gate is per DISPATCH POINT, not per write
        // (see on_control()), so the scan driver and this are the whole list: a
        // third place that drives the field must gate itself here.
        _tasks.add("site_control", GROUP_FAST, PHASE_CONTROL,
            [](void* s, uint64_t now) {
                BotanikFirmware* f = static_cast<BotanikFirmware*>(s);
                if (f->io().control_enabled()) f->on_control(now);
            },
            this);

        // --- OUTPUT: hand what the programs drove to the field ---
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
        // The same component as modbus_rtu_rx above, registered again for its
        // other half: on_output() issues the next request. setup() runs once
        // whichever registration reaches it first, so the port bring-up and the
        // bus planning stay off the first beat.
        _tasks.add_component<PHASE_OUTPUT>("modbus_rtu_tx", GROUP_FAST, _modbus_rtu);
#endif

        _tasks.add("site_output", GROUP_FAST, PHASE_OUTPUT,
                   &_site_hook<&BotanikFirmware::on_output>, this);

        // --- PRESENT: report what just happened ---
        // First in the phase, so anything it logs is drained by the logger below
        // in the SAME beat rather than waiting for the next one.
        _tasks.add("site_present", GROUP_FAST, PHASE_PRESENT,
                   &_site_hook<&BotanikFirmware::on_present>, this);
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
        // The answer goes out off the image the scan just produced: one beat of
        // latency on a read, against a poll measured in seconds, and no request
        // ever answered from a half-updated image.
        _tasks.add_component<PHASE_PRESENT>("modbus_tcp_tx", GROUP_FAST, _modbus_tcp);
#endif
#if BOTANIK_FEATURE_LOGGING
        _tasks.add("logger", GROUP_FAST, PHASE_PRESENT,
            [](void* s, uint64_t) { static_cast<Logs::Logger*>(s)->refill(); },
            &_logger);
#endif
#if BOTANIK_FEATURE_CONSOLE
        // The Console is the Component, not the stack around it: the stack's
        // serial pump is registered separately, at its own phases.
        _tasks.add_component<PHASE_PRESENT>("console", GROUP_SLOW, _console.terminal());
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR
        _tasks.add_component<PHASE_PRESENT>("monitor_log", GROUP_FAST, _monitor.writer());
#endif
#if BOTANIK_FEATURE_CONSOLE
        _tasks.add_component<PHASE_PRESENT>("console_tx", GROUP_FAST, _console.serial());
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR
        _tasks.add_component<PHASE_PRESENT>("monitor_tx", GROUP_FAST, _monitor.serial());
#endif

        if (_tasks.overflow() != 0)
            _logger.critical(Logs::BOOT, "%u task(s) refused: MAX_TASKS is %u",
                             (unsigned)_tasks.overflow(), (unsigned)Config::MAX_TASKS);
    }

    // Per-beat internals
    // ------------------------------------------------------------------------

    void BotanikFirmware::_apply_serial_config() {
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

#if BOTANIK_FEATURE_CONSOLE
    namespace {
        /** @brief The console rows, in one place: the persist walks them and the
         *  restore writes them, and the two must never disagree about which
         *  rows the console's configuration IS. */
        const char* const CONSOLE_ROWS[] = {
            "serial_console_baud", "term_type", "term_cols", "term_rows",
            "term_serial",
        };
    }  // namespace

    void BotanikFirmware::_console_restore_rows(const ConsoleSettings& s) {
        // set_deferred, not set: these rows are F_STAGED, and putting a withdrawn
        // setting back must not be the thing that makes one durable.
        //
        // Only the revert calls this. A COMMIT must not: the rows already hold
        // what it is applying, and writing them back from the applied values
        // would erase a staged edit the operator has not committed yet - a
        // pending `term_cols` would vanish the moment a baud change committed.
        (void)_io.set_deferred(_io.var(_io.find("serial_console_baud")),
                               Vars::Scalar::U(s.baud));
        (void)_io.set_deferred(_io.var(_io.find("term_type")), Vars::Scalar::U(s.type));
        (void)_io.set_deferred(_io.var(_io.find("term_cols")), Vars::Scalar::U(s.cols));
        (void)_io.set_deferred(_io.var(_io.find("term_rows")), Vars::Scalar::U(s.rows));
        (void)_io.set_deferred(_io.var(_io.find("term_serial")), Vars::Scalar::U(s.serial));
    }

    void BotanikFirmware::_console_apply(const ConsoleSettings& s) {
        if (s.baud != _console_baud && s.baud != 0) {
            // Both together: the wire and the budget the renderer paces to.
            // Either alone leaves the renderer pacing for a rate it is not on.
            HAL::Serial::get(HAL::Serial::CONSOLE)->begin(s.baud);
            _console.terminal().terminal().set_baud(s.baud);
        }
        // The row's band is 0..3, but a band is only the guard on the way IN and
        // a corrupt store reaches this by another road; an out-of-range make
        // would index a handshake switch with no case for it.
        const Terminal::Ansi::TerminalType type =
            s.type <= (uint8_t)Terminal::Ansi::TerminalType::MINITEL_2
                ? (Terminal::Ansi::TerminalType)s.type
                : Terminal::Ansi::CONFIGURED_TERMINAL;
        (void)_console.terminal().reconfigure(type, s.cols, s.rows);

        _console_baud = s.baud;
        _console_type = s.type;
        _console_cols = s.cols;
        _console_rows = s.rows;
        _term_serial  = s.serial;
    }

    void BotanikFirmware::_console_persist() {
        for (const char* name : CONSOLE_ROWS) {
            const Vars::VarRef r = _io.find(name);
            if (r.index != Vars::VAR_NONE) (void)_io.persist(_io.var(r));
        }
    }

    void BotanikFirmware::_console_resolve_probation() {
        if (_console_cfg.confirm) {
            _console_cfg = ConsoleConfig();          // off probation, flags cleared
            _console_persist();
            _logger.notice(Logs::BOOT, "console settings confirmed and saved");
            return;
        }
        const bool withdrawn = _console_cfg.revert;
        if (!withdrawn && HAL::Clock::uptime_ms() < _console_cfg.probation_until_ms) return;

        const ConsoleSettings back = _console_prev;
        _console_cfg = ConsoleConfig();
        // The rows as well as the device: a row left holding the withdrawn value
        // would commit it again the next time anything bumped the serial.
        _console_restore_rows(back);
        _console_apply(back);
        // CRITICAL, not a notice: the operator is either watching a screen that
        // just changed under them, or not watching at all because the settings
        // were wrong - and in the second case this line is the record of what
        // happened, read later over the monitor port.
        _logger.critical(Logs::BOOT,
            "console settings %s\nback to %lu baud, %ux%u, terminal type %u",
            withdrawn ? "withdrawn" : "NOT confirmed in time",
            (unsigned long)back.baud, (unsigned)back.cols, (unsigned)back.rows,
            (unsigned)back.type);
    }

    void BotanikFirmware::_apply_console_config(bool allow_probation) {
        // A probation in flight is resolved before anything new is looked at:
        // one console change at a time, or a second commit would overwrite the
        // stash that is the only way back from the first.
        if (_console_cfg.on_probation()) { _console_resolve_probation(); return; }

        const uint32_t baud = (uint32_t)Vars::get_int(&_io, "serial_console_baud",
                                                      (int32_t)_console_baud);
        // A group with no rows answers 0, which is also the "never applied"
        // seed - so a build whose site left the splice out simply never
        // reconfigures, and keeps the geometry it was constructed at.
        const uint32_t serial = Vars::group_serial(&_io, "term");
        const bool want_term = (serial != 0 && serial != _term_serial);
        const bool want_baud = (baud != 0 && baud != _console_baud);
        if (!want_term && !want_baud) return;

        // What to put back. The APPLIED values, not the rows': the rows already
        // hold the candidate, which is the thing that may have to be withdrawn.
        _console_prev = { _console_baud, _term_serial, _console_cols,
                          _console_rows, _console_type };

        ConsoleSettings want = _console_prev;
        want.baud = baud;
        if (want_term) {
            want.serial = serial;
            want.type = (uint8_t)Vars::get_int(&_io, "term_type", (int32_t)_console_type);
            want.cols = (uint16_t)Vars::get_int(&_io, "term_cols", (int32_t)_console_cols);
            want.rows = (uint16_t)Vars::get_int(&_io, "term_rows", (int32_t)_console_rows);
        }
        _console_apply(want);

        if (!allow_probation) return;   // bring-up: the store already agrees
        if (Config::CONSOLE_PROBATION_MS == 0) { _console_persist(); return; }

        _console_cfg.probation_until_ms =
            HAL::Clock::uptime_ms() + Config::CONSOLE_PROBATION_MS;
        // The prompt IS the mechanism: an operator who can read this and type
        // the answer has proved the link works in both directions, which is the
        // one thing the firmware cannot check for itself.
        _logger.critical(Logs::BOOT,
            "console now %lu baud, %ux%u, terminal type %u\n"
            "type `console confirm` within %lu s or it reverts",
            (unsigned long)want.baud, (unsigned)want.cols, (unsigned)want.rows,
            (unsigned)want.type, (unsigned long)(Config::CONSOLE_PROBATION_MS / 1000u));
    }
#endif

#if BOTANIK_FEATURE_LOGGING
    void BotanikFirmware::_apply_log_config() {
        const int32_t want = Vars::get_int(&_io, "sys_log_level",
                                           (int32_t)Config::LOG_MIN_LEVEL);
        // No coercion here, and none needed: the row is a T_ENUM over
        // log_level_label(), so the model refused anything that is not a level
        // long before it could reach the slot. The one value that arrives
        // unvetted is the Config fallback above, from a build whose site left
        // the row out - hence a guard rather than a bare cast.
        if (Logs::log_level_label((uint64_t)want) == nullptr) return;
        const Logs::LogLevel lvl = (Logs::LogLevel)want;
        if (lvl != _logger.min_level()) _logger.set_min_level(lvl);
    }
#endif

    void BotanikFirmware::_latch_control() {
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
    }

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
