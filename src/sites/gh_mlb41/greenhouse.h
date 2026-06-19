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

/** @brief The gh_mlb41 greenhouse firmware. */
class GreenhouseMLB41 : public Botanik::BotanikFirmware<PlcInputData_t, PlcOutputData_t> {
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
                    uint32_t console_baud)
        : Botanik::BotanikFirmware<PlcInputData_t, PlcOutputData_t>(
              shared_in, shared_out, g_alarms, Botanik::Alarms::ALARM_BITS, console_baud),
          _home_page(terminal(), plc_output_data()),
          _status_page(terminal(), plc_output_data())
    {}

    /** @brief Bring up the base, then wire the site on top: the home page sinks the
     *  log stream, leads the tab order (index 0, the boot page), and the status page
     *  slots between logs and alarms. The base registered shell/logs/alarms as
     *  0/1/2; inserting home at 0 and status at 3 yields home/shell/logs/status/alarms. */
    void setup() override {
        Botanik::BotanikFirmware<PlcInputData_t, PlcOutputData_t>::setup();
        logger().add_writer(&_home_page);
        terminal().add_page(&_home_page, 0);
        terminal().add_page(&_status_page, 3);
    }
};

#endif // SITES_GH_MLB41_GREENHOUSE_H
