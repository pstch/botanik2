#ifndef SITES_GH_MLB41_PAGES_H
#define SITES_GH_MLB41_PAGES_H

/**
 * @file pages.h
 * @brief gh_mlb41 site pages (Home + Status) and their display helpers.
 *
 * Site-specific, target-independent: they render the site payload (payload.h) and
 * are used verbatim by the simulator and the OPTA firmware. The floats here are
 * display-only (PLC integer units -> human strings) and never touch shared memory.
 */

#include "libbotanik.h"
#include "libbotanik/utils/text.h"   // format_float - snprintf has no %f on Mbed

#include <cstdio>   // snprintf - page rendering

#include "sites/gh_mlb41/payload.h"

// Display formatting helpers (PLC integer units -> human strings) -------------

/** @brief Map a UpsStatus value to a short display string. */
inline const char* ups_str(uint8_t s) {
    switch (s) {
        case UPS_ONLINE:     return "ONLINE";
        case UPS_ON_BATTERY: return "BATTERY";
        case UPS_FAULT:      return "FAULT";
        default:             return "OFFLINE";
    }
}

/** @brief Map a TrayStatus value to a short display string. */
inline const char* tray_status_str(uint8_t s) {
    switch (s) {
        case TRAY_FILLING:  return "FILLING";
        case TRAY_SOAKING:  return "SOAKING";
        case TRAY_DRAINING: return "DRAINING";
        default:            return "IDLE";
    }
}

/** @brief Map a NetStatus value to a short display string. */
inline const char* net_str(uint8_t s) {
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
        using Botanik::Utils::format_age;
        using Botanik::Utils::format_float;
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
        // Last rain: fixed-width age (format_age, no " ago", floored at minutes)
        // merged with the peak wind (capped at 999). rain_age_s is uint32, so the year
        // rollup is reachable; format_age stays <= 3 digits ("136 y" max), worst case
        // "136 y (wind 999km/h)" < L_W.
        char rb[10];
        unsigned wind = d->wind_peak_kmh;
        if (wind > 999) wind = 999;
        snprintf(buf, sizeof(buf), "%s (wind %ukm/h)",
                 format_age(rb, sizeof(rb), d->rain_age_s, false, true), wind);
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
        // Aggregate the four window / blower zones (the most-open window, the highest
        // blower) rather than only zone A, so the summary can't read "0%" while B-D run.
        uint8_t win_max  = d->window_a;
        if (d->window_b > win_max) win_max = d->window_b;
        if (d->window_c > win_max) win_max = d->window_c;
        if (d->window_d > win_max) win_max = d->window_d;
        uint8_t blow_max = d->blower_a;
        if (d->blower_b > blow_max) blow_max = d->blower_b;
        if (d->blower_c > blow_max) blow_max = d->blower_c;
        if (d->blower_d > blow_max) blow_max = d->blower_d;
        snprintf(buf, sizeof(buf), "Win %s%%  Blow %s%%",
                 format_float(fa, sizeof(fa), (double)win_max, 1, 4),
                 format_float(fb, sizeof(fb), (double)blow_max, 1, 4));
        _value(13, buf);
        // Tray status padded to its widest value ("DRAINING") so the closing
        // paren never moves.
        if (d->current_tray)
            snprintf(buf, sizeof(buf), "Tray %u (%-8s)", d->current_tray, tray_status_str(d->tray_status));
        else
            snprintf(buf, sizeof(buf), "idle");
        _value(14, buf, true, true);   // invert the whole tray value on change

        // Compose a short list of active alarm conditions. Clamp the running offset
        // so a (theoretical) truncation can't hand the next snprintf a wrapped size.
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
        add_alarm(d->ups_status != UPS_ONLINE, "UPS");
        add_alarm(d->generator_running, "GEN");
        add_alarm(d->heat_blower_on || d->heatpump_on, "HEAT");
        _value(15, (buf[0] == '\0') ? "none" : buf);
    }
};

#endif // SITES_GH_MLB41_PAGES_H
