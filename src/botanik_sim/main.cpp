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
#include "libbotanik/utils/text.h"   // format_float - snprintf has no %f on our Mbed
#include "botanik_sim/simulator.h"

#include <csignal>   // std::signal
#include <cstdio>    // snprintf - page rendering

// Pulled into scope to keep the page render lines readable. See utils/text.h for
// why %f is unavailable on the Mbed build.
using Botanik::Utils::format_float;
using Botanik::Utils::format_age;

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
    static constexpr uint16_t L_HDR = 2,  L_LBL = 3,  L_VAL = 13, L_W = 24;
    static constexpr uint16_t R_HDR = 41, R_LBL = 42, R_VAL = 54, R_W = 23;
    static constexpr uint16_t HDR_WL = 34, HDR_WR = 36;
    // System-band extras align with the base system fields: label at col 2, value
    // at col 16 (the base StatusPage L_VAL/L_W). Drawn with _label_at (no
    // clear-to-eol) so the right column on those rows (Stack / Serial) is intact.
    static constexpr uint16_t SYS_VAL = 16, SYS_W = 24;

    void draw_payload_static() override {
        // System band: two extra system fields in the left column (rows 6, 7).
        _label_at(6, 2, "Network");
        _label_at(7, 2, "NTP sync");

        // Left column: POWER + CLIMATE (blank rows 8 and 14 above the headers).
        _header  ( 9, L_HDR, "POWER",   HDR_WL);
        _label_at(10, L_LBL, "Grid AC :");
        _label_at(11, L_LBL, "DC bus  :");
        _label_at(12, L_LBL, "Battery :");
        _label_at(13, L_LBL, "UPS     :");
        _header  (15, L_HDR, "CLIMATE", HDR_WL);
        _label_at(16, L_LBL, "Exterior:");
        _label_at(17, L_LBL, "Floor   :");
        _label_at(18, L_LBL, "Ceiling :");
        _label_at(19, L_LBL, "Rain    :");
        _label_at(20, L_LBL, "Wind pk :");
        _label_at(21, L_LBL, "Sun times:");

        // Right column: IRRIGATION + ACTUATORS.
        _header  ( 9, R_HDR, "IRRIGATION", HDR_WR);
        _label_at(10, R_LBL, "In/Out  :");
        _label_at(11, R_LBL, "Tray    :");
        _label_at(12, R_LBL, "Status  :");
        _header  (15, R_HDR, "ACTUATORS",  HDR_WR);
        _label_at(16, R_LBL, "Windows :");
        _label_at(17, R_LBL, "Blowers :");
        _label_at(18, R_LBL, "Extract.:");
        _label_at(19, R_LBL, "Humidif.:");
        _label_at(20, R_LBL, "Screen  :");
    }

    void draw_payload_dynamic() override {
        const PlcOutputData_t* d = data();
        if (d == nullptr) return;
        char buf[48];
        char fa[12], fb[12];

        // System band: Network (slot 6) + NTP sync (slot 7), value col 16.
        _cell(6, 6, SYS_VAL, net_str(d->net_status), SYS_W);
        char ntpbuf[32];
        if (d->ntp_synced) snprintf(ntpbuf, sizeof(ntpbuf), "OK (%+d ms)", (int)d->ntp_offset_ms);
        else               snprintf(ntpbuf, sizeof(ntpbuf), "no sync");
        _cell(7, 7, SYS_VAL, ntpbuf, SYS_W);

        // Power (slots 10..13).
        snprintf(buf, sizeof(buf), "%s V (%s Hz)",
                 format_float(fa, sizeof(fa), d->grid_ac_mv / 1000.0, 1, 5),
                 format_float(fb, sizeof(fb), d->grid_freq_centi_hz / 100.0, 1, 4));
        _cell(10, 10, L_VAL, buf, L_W);
        snprintf(buf, sizeof(buf), "%s V (A %s B %s)",
                 format_float(fa, sizeof(fa), d->dc_bus_mv / 1000.0, 1, 4),
                 d->dc_bus_a_ok ? "OK" : "!!", d->dc_bus_b_ok ? "OK" : "!!");
        _cell(11, 11, L_VAL, buf, L_W);
        snprintf(buf, sizeof(buf), "%s V (gen %sV)",
                 format_float(fa, sizeof(fa), d->battery_mv / 1000.0, 1, 4),
                 format_float(fb, sizeof(fb), d->generator_mv / 1000.0, 1, 4));
        _cell(12, 12, L_VAL, buf, L_W);
        _cell(13, 13, L_VAL, ups_str(d->ups_status), L_W);

        // Climate (slots 14..18).
        snprintf(buf, sizeof(buf), "%s C  RH %s%%",
                 format_float(fa, sizeof(fa), d->ext_temp_c_centi / 100.0, 1, 4),
                 format_float(fb, sizeof(fb), d->ext_rh_centi_pct / 100.0, 1, 4));
        _cell(14, 16, L_VAL, buf, L_W);
        snprintf(buf, sizeof(buf), "%s C  RH %s%%",
                 format_float(fa, sizeof(fa), d->floor_temp_c_centi / 100.0, 1, 4),
                 format_float(fb, sizeof(fb), d->floor_rh_centi_pct / 100.0, 1, 4));
        _cell(15, 17, L_VAL, buf, L_W);
        snprintf(buf, sizeof(buf), "%s C  RH %s%%",
                 format_float(fa, sizeof(fa), d->ceil_temp_c_centi / 100.0, 1, 4),
                 format_float(fb, sizeof(fb), d->ceil_rh_centi_pct / 100.0, 1, 4));
        _cell(16, 18, L_VAL, buf, L_W);
        // Time since rain. Clamped to a minute so this cold field settles instead
        // of churning the diff once it's past a minute.
        uint32_t rain_age = (d->rain_age_s < 60) ? d->rain_age_s : 60;
        _cell(17, 19, L_VAL, format_age(buf, sizeof(buf), rain_age), L_W);
        snprintf(buf, sizeof(buf), "%03u km/h", (unsigned)d->wind_peak_kmh);
        _cell(18, 20, L_VAL, buf, L_W);
        // Sun times (sunrise / sunset, slot 0).
        snprintf(buf, sizeof(buf), "%02u:%02u / %02u:%02u",
                 d->sunrise_min / 60, d->sunrise_min % 60,
                 d->sunset_min  / 60, d->sunset_min  % 60);
        _cell(0, 21, L_VAL, buf, L_W);

        // Irrigation (slots 19..21).
        unsigned wi = d->water_in_l;
        snprintf(buf, sizeof(buf), "%03u %03u L / %02u Lpm",
                 wi / 1000, wi % 1000, (unsigned)d->water_out_lpm);
        _cell(19, 10, R_VAL, buf, R_W);
        if (d->current_tray) snprintf(buf, sizeof(buf), "Tray %u", (unsigned)d->current_tray);
        else                 snprintf(buf, sizeof(buf), "N/A");
        _cell(20, 11, R_VAL, buf, R_W);
        _cell(21, 12, R_VAL, d->current_tray ? tray_status_str(d->tray_status) : "-", R_W);

        // Actuators (slots 22..26).
        snprintf(buf, sizeof(buf), "A%3u%% B%3u%% C%3u%% D%3u%%",
                 d->window_a, d->window_b, d->window_c, d->window_d);
        _cell(22, 16, R_VAL, buf, R_W);
        snprintf(buf, sizeof(buf), "A%3u%% B%3u%% C%3u%% D%3u%%",
                 d->blower_a, d->blower_b, d->blower_c, d->blower_d);
        _cell(23, 17, R_VAL, buf, R_W);
        snprintf(buf, sizeof(buf), "A%3u%% B%3u%%",
                 d->extractor_a, d->extractor_b);
        _cell(26, 18, R_VAL, buf, R_W);
        _cell(24, 19, R_VAL, d->cooling_on ? "OPEN" : "CLOSE", R_W);
        snprintf(buf, sizeof(buf), "%s%%",
                 format_float(fa, sizeof(fa), (double)d->thermal_screen, 1, 4));
        _cell(25, 20, R_VAL, buf, R_W);
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

        // Compose a short list of active alert conditions.
        buf[0] = '\0';
        size_t n = 0;
        const char* sep = "";
        if (d->rain_detected)            { n += snprintf(buf+n, sizeof(buf)-n, "%sRAIN", sep); sep = " "; }
        if (d->ups_status != UPS_ONLINE) { n += snprintf(buf+n, sizeof(buf)-n, "%sUPS",  sep); sep = " "; }
        if (d->generator_running)        { n += snprintf(buf+n, sizeof(buf)-n, "%sGEN",  sep); sep = " "; }
        if (d->heat_blower_on || d->heatpump_on)
                                         { n += snprintf(buf+n, sizeof(buf)-n, "%sHEAT", sep); sep = " "; }
        _value(15, (buf[0] == '\0') ? "none" : buf);
    }
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

    // Scheduler (constructed before the PLC layer and terminal so they can share
    // its published loop timestamp via the pointer below). It also measures and
    // publishes loop performance (CPU load, jitter, stack usage) on its context.
    Scheduler::LoopScheduler scheduler(Config::TASK_PERIOD_MS, &logger);
    uint32_t* timestamp_ms = &scheduler.context()->timestamp_ms;

    // PLC sync layer (handler + log reader on main thread)
    using SyncHandler = PlcIO::PlcSyncHandler<PlcInputData_t, PlcOutputData_t>;
    SyncHandler         plc_handler(&shared_in, &shared_out, timestamp_ms, &logger);
    PlcIO::PlcLogReader plc_log_reader(plc_handler.context(), &logger);

    // Terminal UI - bundle the subsystem status contexts the terminal reads.
    SystemContext sys_ctx(scheduler.context(), plc_handler.context());

    // Initialize terminal and pages
    Terminal::AnsiTerminal terminal(console_serial, 80, 24, timestamp_ms, CONSOLE_BAUD, sys_ctx);
    Pages::ShellPage shell_page(terminal);
    Pages::LogsPage  logs_page(terminal);
    GreenhouseHomePage   home_page(terminal, plc_handler.output_data());
    GreenhouseStatusPage status_page(terminal, plc_handler.output_data());

    // Register logging sinks
    logger.add_writer(&monitor);
    logger.add_writer(&shell_page);
    logger.add_writer(&logs_page);
    logger.add_writer(&home_page);

    // Emulator thread (drives the mock greenhouse payload).
    // NOTE: should be started before make_process_realtime() is called
    start_greenhouse_emulator(&shared_in, &shared_out, &logger);

    // Register terminal pages
    terminal.add_page(&home_page);
    terminal.add_page(&shell_page);
    terminal.add_page(&logs_page);
    terminal.add_page(&status_page);

    // Select the terminal type before the first render. Swap to setup_minitel()
    // when driving a real Minitel 1B+/2 over the DIN serial port.
    terminal.setup_vt102();
    // Restore the page persisted across a HAL::reset() re-exec (BOTANIK_PAGE, set by
    // HAL::save_active_page), defaulting to the home page. switch_to ignores an
    // out-of-range index, so a stale value just lands on the default.
    terminal.switch_to(Utils::env_u8("BOTANIK_PAGE", 0));

    // Show boot banner
    logger.info(PlcIO::BOOT, "Botanik v%s on %s",
                Config::FIRMWARE_VERSION, HAL::hardware_description());

    // Arm the hardware watchdog. The main loop kicks it every healthy tick; once
    // the PLC link faults we stop kicking so the board reboots HW_WATCHDOG_MS later.
    HAL::watchdog_start(Config::HW_WATCHDOG_MS);

    // Main loop code
    // -----------------------------------------------------------------------

    while (true) {
        // tick() republishes the loop "now" when it releases a beat (so the steps
        // below share one timestamp) and paints the free stack for the usage scan.
        if (!scheduler.tick()) continue;

        console_serial.receive();
        monitor_serial.receive();

        plc_handler.step();
        plc_log_reader.step();

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
