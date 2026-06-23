#ifndef SITES_GH_MLB41_PAGES_H
#define SITES_GH_MLB41_PAGES_H

/**
 * @file pages.h
 * @brief gh_mlb41 site pages (Home + Status). Used verbatim by sim and OPTA.
 *
 * The floats here are display-only (PLC integer units -> human strings) and
 * never touch shared memory. Included only via site.h (after the payload), so
 * PlcOutputData_t and the status structs are always in scope (no circular
 * include).
 */

#include "libbotanik.h"
#include "libbotanik/utils/text.h"   // format_float - snprintf has no %f on Mbed

#include <algorithm>   // std::max
#include <cstdio>      // snprintf

/** @brief Greenhouse status page: renders the full sensor/actuator set. */
class GreenhouseStatusPage : public Botanik::Pages::StatusPage<PlcOutputData_t> {
public:
    GreenhouseStatusPage(Botanik::Terminal::AnsiTerminal& terminal, const PlcOutputData_t* data)
        : Botanik::Pages::StatusPage<PlcOutputData_t>(terminal, data) {}

protected:
    // Two-column body geometry on an 80-col display: left panel cols 2..36,
    // right 41..76. R_VAL=54 clears the 9-wide labels; left labels padded to
    // 10 + colon (widest "Sun times :") so L_VAL=15 ends the field at col 36
    // (L_W=22). Values kept fixed-width so columns never shift as they update.
    static constexpr uint16_t L_HDR = 2,  L_LBL = 3,  L_VAL = 15, L_W = 22;
    static constexpr uint16_t R_HDR = 41, R_LBL = 42, R_VAL = 54, R_W = 23;
    static constexpr uint16_t HDR_WL = 34, HDR_WR = 36;
    // System-band extras (rows 7, 8) align with the base system fields,
    // rjust()'d to match. Labels use _label_at (no clear-to-eol) so the two
    // columns don't clobber.
    static constexpr uint16_t SYS_VAL = 16, SYS_W = 24;

    void draw_payload_static() override {
        // System band (rows 7, 8): network + time/MQTT sync.
        _label_at(7, 2, "Network");
        _label_at(8, 2, "Network IP");
        _label_at(7, R_LBL, "NTP sync");
        _label_at(8, R_LBL, "MQTT");

        // Left column: POWER + CLIMATE.
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
        using Botanik::Utils::format_age;
        using Botanik::Utils::format_float;
        const PlcOutputData_t* d = data();
        if (d == nullptr) return;
        char buf[48], fld[40];
        char fa[12], fb[12];

        // System band (rows 7, 8). Network status (slot 6); IP not yet wired
        // (slot 9).
        snprintf(buf, sizeof(buf), "%s (lat N/A)", NetStatus::str(d->net_status));
        _cell(6, 7, SYS_VAL, rjust(fld, sizeof(fld), SYS_W, buf), SYS_W);
        _cell(9, 8, SYS_VAL, rjust(fld, sizeof(fld), SYS_W, "N/A"), SYS_W);
        // NTP sync + offset (slot 7); MQTT not yet wired (slot 31).
        if (d->ntp_synced) snprintf(buf, sizeof(buf), "OK (%s s)",
                                    format_float(fa, sizeof(fa), d->ntp_offset_ms / 1000.0, 3));
        else               snprintf(buf, sizeof(buf), "no sync");
        _cell(7,  7, R_VAL, rjust(fld, sizeof(fld), R_W, buf), R_W);
        _cell(31, 8, R_VAL, rjust(fld, sizeof(fld), R_W, "N/A"), R_W);

        // Power.
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
        _cell(13, 14, L_VAL, UpsStatus::str(d->ups_status), L_W);

        // Climate.
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
        // Last rain: fixed-width age + peak wind (capped at 999). format_age
        // stays <= 3 digits ("136 y"), so worst "136 y (wind 999km/h)" < L_W.
        char rb[10];
        unsigned wind = d->wind_peak_kmh;
        if (wind > 999) wind = 999;
        snprintf(buf, sizeof(buf), "%s (wind %ukm/h)",
                 format_age(rb, sizeof(rb), d->rain_age_s, false, true), wind);
        _cell(17, 20, L_VAL, buf, L_W);
        // Sun times.
        snprintf(buf, sizeof(buf), "%02u:%02u / %02u:%02u",
                 d->sunrise_min / 60, d->sunrise_min % 60,
                 d->sunset_min  / 60, d->sunset_min  % 60);
        _cell(0, 21, L_VAL, buf, L_W);

        // Irrigation.
        unsigned wi = d->water_in_l;
        snprintf(buf, sizeof(buf), "%03u %03u L / %02u Lpm",
                 wi / 1000, wi % 1000, (unsigned)d->water_out_lpm);
        _cell(19, 11, R_VAL, buf, R_W);
        if (d->current_tray)
            snprintf(buf, sizeof(buf), "Tray %u", (unsigned)d->current_tray);
        else
            snprintf(buf, sizeof(buf), "N/A");
        _cell(20, 12, R_VAL, buf, R_W);
        _cell(21, 13, R_VAL, d->current_tray ? TrayStatus::str(d->tray_status) : "-", R_W);

        // Actuators.
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
        // Identity block (site/version/commit) is the base HomePage's; add
        // readings.
        _label(11, "Temp");
        _label(12, "Humidity");
        _label(13, "Ventilation");
        _label(14, "Irrigation");
        _label(15, "Alarms");
    }

    void draw_payload_dynamic() override {
        using Botanik::Utils::format_float;
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
        // Aggregate the four zones (most-open window, highest blower) so the
        // summary can't read "0%" while B-D run.
        uint8_t win_max  = std::max(std::max(d->window_a, d->window_b),
                                    std::max(d->window_c, d->window_d));
        uint8_t blow_max = std::max(std::max(d->blower_a, d->blower_b),
                                    std::max(d->blower_c, d->blower_d));
        snprintf(buf, sizeof(buf), "Win %s%%  Blow %s%%",
                 format_float(fa, sizeof(fa), (double)win_max, 1, 4),
                 format_float(fb, sizeof(fb), (double)blow_max, 1, 4));
        _value(13, buf);
        // Tray status padded to its widest value ("DRAINING") so the paren
        // never moves.
        if (d->current_tray)
            snprintf(buf, sizeof(buf), "Tray %u (%-8s)",
                     d->current_tray, TrayStatus::str(d->tray_status));
        else
            snprintf(buf, sizeof(buf), "idle");
        _value(14, buf, true, true);   // invert the whole tray value on change

        // Short list of active alarm conditions. Clamp the running offset so a
        // (theoretical) truncation can't hand the next snprintf a wrapped size.
        buf[0] = '\0';
        size_t n = 0;
        const char* sep = "";
        auto add_alarm = [&](bool on, const char* tok) {
            if (!on || n >= sizeof(buf)) return;
            int w = snprintf(buf + n, sizeof(buf) - n, "%s%s", sep, tok);
            if (w > 0) n = (n + (size_t)w > sizeof(buf)) ? sizeof(buf) : n + (size_t)w;
            sep = " ";
        };
        add_alarm(d->rain_detected, "RAIN");
        add_alarm(d->ups_status != UpsStatus::ONLINE, "UPS");
        add_alarm(d->generator_running, "GEN");
        add_alarm(d->heat_blower_on || d->heatpump_on, "HEAT");
        _value(15, (buf[0] == '\0') ? "none" : buf);
    }
};

#endif // SITES_GH_MLB41_PAGES_H
