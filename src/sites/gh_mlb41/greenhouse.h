#ifndef SITES_GH_MLB41_GREENHOUSE_H
#define SITES_GH_MLB41_GREENHOUSE_H

/**
 * @file greenhouse.h
 * @brief GreenhouseMLB41 - the gh_mlb41 site firmware (derives BotanikFirmware).
 *
 * Adds the site pages as members (built from the base's exposed components) and
 * customizes registration: its home page leads the tab order and also sinks the
 * log stream. The payload layout, pages and alarm table it wires in are the
 * single source of truth for this greenhouse (payload.h / pages.h / alarms.h).
 */

#include "libbotanik/firmware.h"

#include "sites/gh_mlb41/payload.h"
#include "sites/gh_mlb41/pages.h"
#include "sites/gh_mlb41/alarms.h"

/** @brief The gh_mlb41 greenhouse firmware. `final` so the per-beat hook devirtualizes. */
class GreenhouseMLB41 final : public Botanik::BotanikFirmware<PlcInputData_t, PlcOutputData_t> {
    /** @name Site pages
     *  @{ */
    GreenhouseHomePage   _home_page;
    GreenhouseStatusPage _status_page;
    /** @} */

public:
    /** @brief Build the base around this site's alarm table, then the site pages
     *  from the base's terminal + PLC output payload. */
    GreenhouseMLB41(Botanik::PlcIO::PlcInput<PlcInputData_t>*   shared_in,
                    Botanik::PlcIO::PlcOutput<PlcOutputData_t>* shared_out,
                    uint32_t console_baud, uint16_t cols = 80, uint16_t rows = 24)
        : Botanik::BotanikFirmware<PlcInputData_t, PlcOutputData_t>(
              shared_in, shared_out, g_alarms, g_alarm_count,
              console_baud, cols, rows),
          _home_page(terminal(), plc_output_data()),
          _status_page(terminal(), plc_output_data())
    {}

protected:
    /** @brief Tab order (add_page order = tab order): the home page leads as the boot
     *  page, the status page slots between logs and alarms -> home/shell/logs/status/alarms. */
    void register_pages() override {
        terminal().add_page(&_home_page);     // page 0 (the boot page)
        terminal().add_page(&shell_page());
        terminal().add_page(&logs_page());
        terminal().add_page(&_status_page);
        terminal().add_page(&alarms_page());
    }

    /** @brief The home page also sinks the log stream (its bottom log-tail band). */
    void on_setup() override {
        logger().add_writer(&_home_page);
    }
};

#endif // SITES_GH_MLB41_GREENHOUSE_H
