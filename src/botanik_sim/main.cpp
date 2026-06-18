/**
 * @file main.cpp
 * @brief Botanik simulator entry point.
 *
 * The equivalent of an Arduino sketch: it wires the library components together
 * around a site-specific PLC payload (which lives in simulator.h/.cpp). On OPTA
 * this plumbing lives in the .ino sketch and the PLC shared memory comes from
 * HAL::get_plc_input()/get_plc_output(); here the simulator owns it as local
 * statics.
 */

#include "libbotanik.h"
#include "libbotanik_asserts.h"      // BOTANIK_ASSERT + set_assert_logger
#include "libbotanik/utils/text.h"   // format_float - snprintf has no %f on our Mbed
#include "libbotanik/alerts/alerts.h"
#include "libbotanik/alerts/notifier.h"
#include "libbotanik/alerts/reader.h"
#include "botanik_sim/simulator.h"

#include <csignal>   // std::signal
#include <cstdio>    // snprintf - page rendering

// Pulled into scope to keep the page render lines readable. See utils/text.h for
// why %f is unavailable on the Mbed build.
using Botanik::Utils::format_float;

// Display formatting helpers (PLC integer units -> human strings; the floats
// here are display-only and never touch shared memory) -----------------------

/** @brief Map a UpsStatus value to a short display string. */
static const char* ups_str(uint8_t s) {
    switch (s) {
        case UPS_ONLINE:     return "ONLINE";
        case UPS_ON_BATTERY: return "BATTERY";
        case UPS_FAULT:      return "FAULT";
        default:             return "OFFLINE";
    }
}

/** @brief Map a TrayStatus value to a short display string. */
static const char* tray_status_str(uint8_t s) {
    switch (s) {
        case TRAY_FILLING:  return "FILLING";
        case TRAY_SOAKING:  return "SOAKING";
        case TRAY_DRAINING: return "DRAINING";
        default:            return "IDLE";
    }
}

/** @brief Map a NetStatus value to a short display string. */
static const char* net_str(uint8_t s) {
    switch (s) {
        case NET_ONLINE: return "ONLINE";
        case NET_LINK:   return "LINK";
        default:         return "DOWN";
    }
}

// Concrete site pages --------------------------------------------------------

/** @brief Greenhouse status page: renders the full sensor/actuator set. */
class GreenhouseStatusPage : public Botanik::Pages::StatusPage<PlcOutputData_t> {
public:
    GreenhouseStatusPage(Botanik::Terminal::AnsiTerminal& terminal, const PlcOutputData_t* data)
        : Botanik::Pages::StatusPage<PlcOutputData_t>(terminal, data) {}

protected:
    // Two-column body geometry on an 80-col display: left panel cols 2..36, right
    // panel 41..76. R_VAL starts at col 54 so the 9-wide labels ("Humidif.:") clear
    // it. Values below are kept fixed-width so columns never shift as they update.
    // Left labels are padded to 10 chars + colon (the widest is "Sun times :"), so
    // L_VAL=15 keeps a single space after every colon while the field still ends at
    // col 36 (L_W=22).
    static constexpr uint16_t L_HDR = 2,  L_LBL = 3,  L_VAL = 15, L_W = 22;
    static constexpr uint16_t R_HDR = 41, R_LBL = 42, R_VAL = 54, R_W = 23;
    static constexpr uint16_t HDR_WL = 34, HDR_WR = 36;
    // System-band extras (rows 7, 8) align with the base system fields: left value
    // col 16 (base L_VAL/L_W), right value col 54 (base R_VAL/R_W), both right-
    // justified via rjust() to match the base. Labels use _label_at (no clear-to-
    // eol) so the two columns on a row never clobber each other.
    static constexpr uint16_t SYS_VAL = 16, SYS_W = 24;

    void draw_payload_static() override {
        // System band (rows 7, 8): network + time/MQTT sync, two columns.
        _label_at(7, 2, "Network");
        _label_at(8, 2, "Network IP");
        _label_at(7, R_LBL, "NTP sync");
        _label_at(8, R_LBL, "MQTT");

        // Left column: POWER + CLIMATE (blank rows 9 and 15 above the headers).
        _header  (10, L_HDR, "POWER",   HDR_WL);
        _label_at(11, L_LBL, "Grid AC   :");
        _label_at(12, L_LBL, "DC bus    :");
        _label_at(13, L_LBL, "Battery   :");
        _label_at(14, L_LBL, "UPS       :");
        _header  (16, L_HDR, "CLIMATE", HDR_WL);
        _label_at(17, L_LBL, "Exterior  :");
        _label_at(18, L_LBL, "Floor     :");
        _label_at(19, L_LBL, "Ceiling   :");
        _label_at(20, L_LBL, "Last rain :");
        _label_at(21, L_LBL, "Sun times :");

        // Right column: IRRIGATION + ACTUATORS.
        _header  (10, R_HDR, "IRRIGATION", HDR_WR);
        _label_at(11, R_LBL, "In/Out  :");
        _label_at(12, R_LBL, "Tray    :");
        _label_at(13, R_LBL, "Status  :");
        _header  (16, R_HDR, "ACTUATORS",  HDR_WR);
        _label_at(17, R_LBL, "Windows :");
        _label_at(18, R_LBL, "Blowers :");
        _label_at(19, R_LBL, "Extract.:");
        _label_at(20, R_LBL, "Humidif.:");
        _label_at(21, R_LBL, "Screen  :");
    }

    void draw_payload_dynamic() override {
        const PlcOutputData_t* d = data();
        if (d == nullptr) return;
        char buf[48], fld[40];
        char fa[12], fb[12];

        // System band (rows 7, 8), right-justified to match the base system fields.
        // Network status + average latency (slot 6); IP not yet wired (slot 9).
        snprintf(buf, sizeof(buf), "%s (lat N/A)", net_str(d->net_status));
        _cell(6, 7, SYS_VAL, rjust(fld, sizeof(fld), SYS_W, buf), SYS_W);
        _cell(9, 8, SYS_VAL, rjust(fld, sizeof(fld), SYS_W, "N/A"), SYS_W);
        // NTP sync + last offset in seconds (slot 7); MQTT not yet wired (slot 31).
        if (d->ntp_synced) snprintf(buf, sizeof(buf), "OK (%s s)",
                                    format_float(fa, sizeof(fa), d->ntp_offset_ms / 1000.0, 3));
        else               snprintf(buf, sizeof(buf), "no sync");
        _cell(7,  7, R_VAL, rjust(fld, sizeof(fld), R_W, buf), R_W);
        _cell(31, 8, R_VAL, rjust(fld, sizeof(fld), R_W, "N/A"), R_W);

        // Power (slots 10..13).
        snprintf(buf, sizeof(buf), "%s V (%s Hz)",
                 format_float(fa, sizeof(fa), d->grid_ac_mv / 1000.0, 1, 5),
                 format_float(fb, sizeof(fb), d->grid_freq_centi_hz / 100.0, 1, 4));
        _cell(10, 11, L_VAL, buf, L_W);
        snprintf(buf, sizeof(buf), "%s V (A %s B %s)",
                 format_float(fa, sizeof(fa), d->dc_bus_mv / 1000.0, 1, 4),
                 d->dc_bus_a_ok ? "OK" : "!!", d->dc_bus_b_ok ? "OK" : "!!");
        _cell(11, 12, L_VAL, buf, L_W);
        snprintf(buf, sizeof(buf), "%s V (gen %sV)",
                 format_float(fa, sizeof(fa), d->battery_mv / 1000.0, 1, 4),
                 format_float(fb, sizeof(fb), d->generator_mv / 1000.0, 1, 4));
        _cell(12, 13, L_VAL, buf, L_W);
        _cell(13, 14, L_VAL, ups_str(d->ups_status), L_W);

        // Climate (slots 14..18).
        snprintf(buf, sizeof(buf), "%s C  RH %s%%",
                 format_float(fa, sizeof(fa), d->ext_temp_c_centi / 100.0, 1, 4),
                 format_float(fb, sizeof(fb), d->ext_rh_centi_pct / 100.0, 1, 4));
        _cell(14, 17, L_VAL, buf, L_W);
        snprintf(buf, sizeof(buf), "%s C  RH %s%%",
                 format_float(fa, sizeof(fa), d->floor_temp_c_centi / 100.0, 1, 4),
                 format_float(fb, sizeof(fb), d->floor_rh_centi_pct / 100.0, 1, 4));
        _cell(15, 18, L_VAL, buf, L_W);
        snprintf(buf, sizeof(buf), "%s C  RH %s%%",
                 format_float(fa, sizeof(fa), d->ceil_temp_c_centi / 100.0, 1, 4),
                 format_float(fb, sizeof(fb), d->ceil_rh_centi_pct / 100.0, 1, 4));
        _cell(16, 19, L_VAL, buf, L_W);
        // Last rain: minutes since rain merged with the peak wind. Both capped at
        // 999 so the worst case ("999 min (wind 999km/h)") stays within L_W (22).
        unsigned rain_min = d->rain_age_s / 60;
        if (rain_min > 999) rain_min = 999;
        unsigned wind = d->wind_peak_kmh;
        if (wind > 999) wind = 999;
        snprintf(buf, sizeof(buf), "%u min (wind %ukm/h)", rain_min, wind);
        _cell(17, 20, L_VAL, buf, L_W);
        // Sun times (sunrise / sunset, slot 0).
        snprintf(buf, sizeof(buf), "%02u:%02u / %02u:%02u",
                 d->sunrise_min / 60, d->sunrise_min % 60,
                 d->sunset_min  / 60, d->sunset_min  % 60);
        _cell(0, 21, L_VAL, buf, L_W);

        // Irrigation (slots 19..21).
        unsigned wi = d->water_in_l;
        snprintf(buf, sizeof(buf), "%03u %03u L / %02u Lpm",
                 wi / 1000, wi % 1000, (unsigned)d->water_out_lpm);
        _cell(19, 11, R_VAL, buf, R_W);
        if (d->current_tray) snprintf(buf, sizeof(buf), "Tray %u", (unsigned)d->current_tray);
        else                 snprintf(buf, sizeof(buf), "N/A");
        _cell(20, 12, R_VAL, buf, R_W);
        _cell(21, 13, R_VAL, d->current_tray ? tray_status_str(d->tray_status) : "-", R_W);

        // Actuators (slots 22..26).
        snprintf(buf, sizeof(buf), "A%3u%% B%3u%% C%3u%% D%3u%%",
                 d->window_a, d->window_b, d->window_c, d->window_d);
        _cell(22, 17, R_VAL, buf, R_W);
        snprintf(buf, sizeof(buf), "A%3u%% B%3u%% C%3u%% D%3u%%",
                 d->blower_a, d->blower_b, d->blower_c, d->blower_d);
        _cell(23, 18, R_VAL, buf, R_W);
        snprintf(buf, sizeof(buf), "A%3u%% B%3u%%",
                 d->extractor_a, d->extractor_b);
        _cell(26, 19, R_VAL, buf, R_W);
        _cell(24, 20, R_VAL, d->cooling_on ? "OPEN" : "CLOSE", R_W);
        snprintf(buf, sizeof(buf), "%s%%",
                 format_float(fa, sizeof(fa), (double)d->thermal_screen, 1, 4));
        _cell(25, 21, R_VAL, buf, R_W);
    }
};

/** @brief Greenhouse home page: logo + a compact key-readings summary. */
class GreenhouseHomePage : public Botanik::Pages::HomePage<PlcOutputData_t> {
public:
    GreenhouseHomePage(Botanik::Terminal::AnsiTerminal& terminal, const PlcOutputData_t* data)
        : Botanik::Pages::HomePage<PlcOutputData_t>(terminal, data) {}

protected:
    void draw_payload_static() override {
        _label(11, "Temp");
        _label(12, "Humidity");
        _label(13, "Ventilation");
        _label(14, "Irrigation");
        _label(15, "Alerts");
    }

    void draw_payload_dynamic() override {
        const PlcOutputData_t* d = data();
        if (d == nullptr) return;
        char buf[64];
        char fa[12], fb[12];

        snprintf(buf, sizeof(buf), "%s C",
                 format_float(fa, sizeof(fa), d->ext_temp_c_centi / 100.0, 1, 4));
        _value(11, buf);
        snprintf(buf, sizeof(buf), "%s%%",
                 format_float(fa, sizeof(fa), d->ext_rh_centi_pct / 100.0, 1, 4));
        _value(12, buf);
        snprintf(buf, sizeof(buf), "Win %s%%  Blow %s%%",
                 format_float(fa, sizeof(fa), (double)d->window_a, 1, 4),
                 format_float(fb, sizeof(fb), (double)d->blower_a, 1, 4));
        _value(13, buf);
        // Tray status padded to its widest value ("DRAINING") so the closing
        // paren never moves.
        if (d->current_tray)
            snprintf(buf, sizeof(buf), "Tray %u (%-8s)", d->current_tray, tray_status_str(d->tray_status));
        else
            snprintf(buf, sizeof(buf), "idle");
        _value(14, buf, true, true);   // invert the whole tray value on change

        // Compose a short list of active alert conditions. Clamp the running offset
        // so a (theoretical) truncation can't hand the next snprintf a wrapped size.
        buf[0] = '\0';
        size_t n = 0;
        const char* sep = "";
        auto add_alert = [&](bool on, const char* tok) {
            if (!on || n >= sizeof(buf)) return;
            int w = snprintf(buf + n, sizeof(buf) - n, "%s%s", sep, tok);
            if (w > 0) n = (n + (size_t)w > sizeof(buf)) ? sizeof(buf) : n + (size_t)w;
            sep = " ";
        };
        add_alert(d->rain_detected, "RAIN");
        add_alert(d->ups_status != UPS_ONLINE, "UPS");
        add_alert(d->generator_running, "GEN");
        add_alert(d->heat_blower_on || d->heatpump_on, "HEAT");
        _value(15, (buf[0] == '\0') ? "none" : buf);
    }
};

// Site alert table: one entry per Alerts::AlertId, in bit-index order. The static
// prefix is filled here (code, description, masked_by, ON/OFF dwell thresholds in
// ms; -1 disables an action); the dynamic suffix is left zeroed for the model.
// masked_by is 0 for now (no masking relationships defined yet); the mechanism is
// in place for the site to fill. Thresholds are placeholders to be tuned per site.
// The dynamic suffix of each entry is intentionally left zeroed (model-owned).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static Botanik::Alerts::AlertCondition g_alerts[Botanik::Alerts::ALERT_BITS] = {
    // code        description                                      mask  logOn  logOff mqttOn mqttOff
    {"AC_GRID",  "Grid AC out of spec or offline",                  0,    2000,  2000,  15000, 15000},
    {"DC_PS1",   "DC PS1 volts too low",                            0,    2000,  2000,  15000, 15000},
    {"GEN_FLT",  "Generator failed to start",                       0,    1000,  2000,  10000, 15000},
    {"AC_LOCAL", "Local AC out of spec or offline",                 0,    2000,  2000,  15000, 15000},
    {"DC_PS2",   "DC PS2 volts too low",                            0,    2000,  2000,  15000, 15000},
    {"DC_UPS",   "DC UPS on battery",                               0,    1000,  2000,  10000, 15000},
    {"DC_LOCAL", "Local DC volts too low",                          0,    2000,  2000,  15000, 15000},
    {"GEN_BATT", "Generator start battery too low or offline",      0,    5000,  5000,  30000, 30000},
    {"PLC_FLT",  "Failed to synchronize with PLC",                  0,    2000,  2000,  10000, 15000},
    {"NET_FLT",  "Network should be set up but no TCP/IP I/O",      0,    5000,  5000,  30000, 30000},
    {"NTP_TIM",  "Timeout while attempting NTP sync",               0,    5000,  5000,  -1,    -1},
    {"ACT_FLT",  "An actioner faulted in the last hour",            0,    2000,  2000,  15000, 15000},
    {"SNS_FLT",  "A sensor faulted in the last hour",               0,    2000,  2000,  15000, 15000},
    {"TOO_HOT",  "Inside temperature too high",                     0,    3000,  3000,  20000, 20000},
    {"TOO_COLD", "Inside temperature too low",                      0,    3000,  3000,  20000, 20000},
    {"TOO_HUM",  "Relative humidity too high",                      0,    3000,  3000,  20000, 20000},
    {"TMP_STR",  "Temperature stratification too high",             0,    5000,  5000,  -1,    -1},
    {"SYS_RST",  "Reinitialized in the last five minutes",          0,    0,     -1,    -1,    -1},
    {"WTR_LK",   "Leak in irrigation circuit",                      0,    1000,  2000,  10000, 15000},
    {"WTR_BAR",  "Water pressure too low",                          0,    2000,  2000,  15000, 15000},
    {"WTR_CLOG", "Failing to drain a tray",                         0,    3000,  3000,  20000, 20000},
    {"IO_DROP",  "Logical I/O drops",                               0,    2000,  2000,  -1,    -1},
    {"LOG_FLD",  "Logs flooding",                                   0,    2000,  2000,  -1,    -1},
    {"WH_STORM", "Current storm conditions",                        0,    2000,  5000,  15000, 30000},
};
#pragma GCC diagnostic pop

// Site shell commands (data-driven, registered on the shell page below). Each is a
// plain callback taking the output channel, the argument tail, and the opaque ctx
// passed to set_commands() - here the alert model.
static void cmd_version(Botanik::Shell::ShellWriter& out, const char*, uint16_t, void*) {
    char buf[80];
    int n = snprintf(buf, sizeof(buf), "Botanik %s on %s",
                     Botanik::Config::FIRMWARE_VERSION, Botanik::HAL::hardware_description());
    if (n > 0) out.write(buf, (size_t)n);
}

static void cmd_alerts(Botanik::Shell::ShellWriter& out, const char*, uint16_t, void* ctx) {
    const Botanik::Alerts::AlertModel* m =
        static_cast<const Botanik::Alerts::AlertModel*>(ctx);
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "alerts: %u active, %u unmasked",
                     m ? m->active_count() : 0, m ? m->unmasked_count() : 0);
    if (n > 0) out.write(buf, (size_t)n);
}

static const Botanik::Shell::ShellCommand SITE_COMMANDS[] = {
    { "version", "show firmware version and platform", cmd_version },
    { "alerts",  "show active / unmasked alert counts", cmd_alerts  },
};

int main(int argc, char** argv) {
    using namespace Botanik;

    // Setup code
    // -----------------------------------------------------------------------

    // Stash argv so HAL::reset() can re-exec the process with the same arguments.
    HAL::set_reboot_args(argc, argv);

    // Raw terminal + console serial. One baud, used for the UART and to size the
    // terminal's cooperative-TX depth gates (see AnsiTerminal).
    static const uint32_t CONSOLE_BAUD = 4800;
    HAL::setup_raw_console();
    HAL::get_serial(HAL::CONSOLE)->begin(CONSOLE_BAUD);

    // Graceful shutdown on Ctrl+C / SIGTERM: flip the run flag so the main loop
    // breaks into HAL::shutdown() (same exit path as the "exit" command). SIGSEGV
    // stays with the HAL handler to restore the terminal on a crash.
    // NOLINT: the handler is async-signal-safe (assigns a volatile sig_atomic_t)
    // but lives in simulator.cpp, so clang-tidy can't confirm it across TUs.
    std::signal(SIGINT,  botanik_sim_shutdown);   // NOLINT(bugprone-signal-handler)
    std::signal(SIGTERM, botanik_sim_shutdown);   // NOLINT(bugprone-signal-handler)

    // Shared PLC memory - both handler and emulator point at the same region.
    // On OPTA: PlcIO::PlcInput<PlcInputData_t>* plcin =
    //              static_cast<PlcIO::PlcInput<PlcInputData_t>*>(HAL::get_plc_input());
    static PlcIO::PlcInput<PlcInputData_t>   shared_in  {};
    static PlcIO::PlcOutput<PlcOutputData_t> shared_out {};

    // Serial I/O
    Utils::BufferedSerial console_serial(HAL::get_serial(HAL::CONSOLE));
    Utils::BufferedSerial monitor_serial(HAL::get_serial(HAL::MONITOR));

    // Logging. The monitor sink drops output when stderr is a terminal, so the
    // diagnostic log can't garble the console UI; redirect stderr to capture it.
    Logs::Logger           logger;
    Logs::MonitorLogWriter monitor(monitor_serial);
    set_assert_logger(&logger);   // route BOTANIK_ASSERT failures to the log

    // Scheduler (constructed before the PLC layer and terminal so they can share
    // its published loop timestamp via the pointer below). It also measures and
    // publishes loop performance (CPU load, jitter, stack usage) on its context.
    Scheduler::LoopScheduler scheduler(Config::TASK_PERIOD_MS, &logger);
    uint32_t* timestamp_ms = &scheduler.context()->timestamp_ms;

    // PLC sync layer (handler + log reader on main thread)
    using SyncHandler = PlcIO::PlcSyncHandler<PlcInputData_t, PlcOutputData_t>;
    SyncHandler         plc_handler(&shared_in, &shared_out, timestamp_ms, &logger);
    // Dilate the link timeouts, mirrored by the emulator side in its thread.
    plc_handler.set_heartbeat_ms(plc_dilated_timeout(Config::PLC_HEARTBEAT_MS));
    plc_handler.set_timeout_ms  (plc_dilated_timeout(Config::PLC_TIMEOUT_MS));
    plc_handler.set_watchdog_ms (plc_dilated_timeout(Config::PLC_WATCHDOG_MS));
    PlcIO::PlcLogReader plc_log_reader(plc_handler.context(), &logger);

    // Alert model: merges the PLC alert bitset with local runtime alerts, raises
    // ALERT-topic logs + (draft) MQTT notifications, and feeds the terminal alert
    // bar / alerts page. The notifier is draft plumbing (no transport wired yet).
    Alerts::MqttAlertNotifier alert_mqtt("botanik/greenhouse", &logger);
    Alerts::AlertModel        alert_model(g_alerts, Alerts::ALERT_BITS, &logger, &alert_mqtt);
    const PlcOutputData_t*    plc_out = plc_handler.output_data();
    Alerts::AlertReader<PlcOutputData_t> alert_reader(&alert_model, plc_out);

    // Terminal UI - bundle the subsystem status contexts the terminal reads.
    SystemContext sys_ctx(scheduler.context(), plc_handler.context());

    // Initialize terminal and pages. The terminal takes the alert model so it can
    // render the always-visible bottom alert bar (and reserve its row) on every page.
    Terminal::AnsiTerminal terminal(console_serial, 80, 24, timestamp_ms, CONSOLE_BAUD,
                                    sys_ctx, &alert_model);
    Pages::ShellPage shell_page(terminal);
    Pages::LogsPage  logs_page(terminal);
    GreenhouseHomePage   home_page(terminal, plc_handler.output_data());
    GreenhouseStatusPage status_page(terminal, plc_handler.output_data());
    Pages::AlertsPage    alerts_page(terminal, &alert_model);

    // Register the site shell commands (ctx = alert model, for the "alerts" cmd).
    shell_page.set_commands(SITE_COMMANDS,
                            sizeof(SITE_COMMANDS) / sizeof(SITE_COMMANDS[0]), &alert_model);

    // Register logging sinks
    logger.add_writer(&monitor);
    logger.add_writer(&shell_page);
    logger.add_writer(&logs_page);
    logger.add_writer(&home_page);
    // Report the console's TX-full drops (shell output that didn't fit the ring).
    logger.watch_tx(&console_serial);

    // Emulator thread (drives the mock greenhouse payload). Also locks process
    // memory and makes the main loop + emulator realtime on separate cores; see
    // start_greenhouse_emulator for the start-order constraint.
    start_greenhouse_emulator(&shared_in, &shared_out, &logger);

    // Register terminal pages
    terminal.add_page(&home_page);
    terminal.add_page(&shell_page);
    terminal.add_page(&logs_page);
    terminal.add_page(&status_page);
    terminal.add_page(&alerts_page);

    // Select the terminal type before the first render. Swap to setup_minitel()
    // when driving a real Minitel 1B+/2 over the DIN serial port.
    terminal.setup_vt102();
    // Both targets boot on the home page (no page-index persistence: a reset comes
    // back default, matching hardware where RAM is cleared).
    terminal.switch_to(0);

    // Show boot banner
    logger.info(PlcIO::BOOT, "Botanik v%s on %s",
                Config::FIRMWARE_VERSION, HAL::hardware_description());

    // Arm the hardware watchdog. The main loop kicks it every healthy tick; once
    // the PLC link faults we stop kicking so the board reboots HW_WATCHDOG_MS later.
    HAL::watchdog_start(plc_dilated_timeout(Config::HW_WATCHDOG_MS));

    // Main loop code
    // -----------------------------------------------------------------------

    // Per-tick cycle contract (the OPTA firmware loop mirrors this order; see
    // docs/CONCURRENCY.md). The order is load-bearing - inputs are ingested before
    // processing, and the TX rings are flushed only after everything has queued its
    // output for the beat:
    //   tick -> receive -> plc_handler -> plc_log_reader -> logger -> terminal
    //        -> monitor.drain -> transmit -> watchdog_kick
    while (true) {
        // tick() republishes the loop "now" when it releases a beat (so the steps
        // below share one timestamp) and paints the free stack for the usage scan.
        if (!scheduler.tick()) continue;

        console_serial.receive();
        monitor_serial.receive();

        plc_handler.step();
        plc_log_reader.step();

        // Read + analyze alerts after the PLC payload is fresh and before the
        // logger drains, so any alarm START/END lands this beat. SYS_RST is a
        // local alert, asserted for the first five minutes after boot.
        uint64_t now_ms = HAL::uptime_ms();
        alert_model.set_local(Alerts::SYS_RST, now_ms < 300000);
        alert_reader.step(now_ms);

        logger.step();

        terminal.step();
        monitor.drain();

        console_serial.transmit();
        monitor_serial.transmit();

        // only kick the watchdog if the PLC is not faulted
        if (!plc_handler.context()->faulted()) HAL::watchdog_kick();

        if (!botanik_running) break;
    }

    // Reached only via the run flag (SIGINT/SIGTERM). HAL::shutdown() runs the
    // reset hook (joins the emulator thread) and exits through atexit, restoring
    // the console. Report 128 + signo so a supervisor sees the signal-driven exit.
    HAL::shutdown(128 + (int)botanik_shutdown_signo);
    return 0;
}
