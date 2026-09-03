// REVIEW_35: TODO
#ifndef PAGES_HOME_H
#define PAGES_HOME_H

/**
 * @file home.h
 * @brief HomePage - landing screen: ASCII logo, a compact system summary, and
 *        a small live log tail along the bottom.
 *
 * Site-independent: logo, firmware identity block, uptime, log tail, and the
 * site's home-field readings in three columns of 8 (the site supplies the
 * table, the page owns the geometry).
 *
 * Also a LogWriter: the bottom HOME_SCROLLBACK_LINES rows are a live tail (no
 * scroll-up) replayed from a small HomeScrollback. begin_redraw() over the
 * whole content area bottom-aligns the lines, so the band needs no bespoke
 * render.
 */

#include <cstdint>

#include "libbotanik/config/identity.h"
#include "libbotanik/config/terminal.h"
#include "libbotanik/config/vars.h"

#include "libbotanik/hal/platform.h"
#include "libbotanik/logs/format.h"
#include "libbotanik/logs/writer.h"
#include "libbotanik/pages/metric_page.h"
#include "libbotanik/pages/scrollback.h"
#include "libbotanik/vars/access.h"

// Build-stamped git short hash (generated header); "unknown" when absent
// (Arduino .ino deploy doesn't run CMake).
#if defined(__has_include) && __has_include("botanik_gitversion.h")
#  include "botanik_gitversion.h"           // BOTANIK_GIT_HASH
#endif
#ifndef BOTANIK_GIT_HASH
#  define BOTANIK_GIT_HASH "unknown"
#endif

namespace Botanik { namespace Pages {

    /** @brief Landing page (see file header). */
    class HomePage : public MetricPage, public Logs::BufferedLogWriter {
    protected:
        /** @name State
         *  @{ */

        /** @brief Bottom log tail: most recent lines, bottom-aligned on the
         *         screen. */
        HomeScrollback _scrollback;
        /** @brief Mirrors tick(active); gates band repaints in write(). */
        bool _log_active = false;

        /** @brief Site home-field table (label + status variable) + the payload
         *  they read. Up to COLS * COL_MAX shown; extras ignored. */
        const Vars::HomeVar* _fields      = nullptr;
        /** @brief The rows' resolved indices, filled at bring-up (the table
         *  above names its variables rather than indexing them). */
        const Vars::VarRef*  _refs        = nullptr;
        uint8_t                   _field_count = 0;
        const Vars::Bank*           _io          = nullptr;

        /** @} */

        /** @name Home-field three-column layout
         *  COLS columns of COL_MAX rows each, starting at COL_TOP (one blank row
         *  below the logo, clear of the identity block). Column 1 gives its top
         *  UPTIME_CELLS cells to the uptime readout and a spacer, so it holds
         *  COL_MAX - UPTIME_CELLS fields. Labels left-justified, values in a fixed
         *  field so columns stay put. Per-column geometry lives in _field_place().
         *  @{ */
        static constexpr uint8_t  COLS = 3, COL_MAX = 8, UPTIME_CELLS = 2;
        /** @brief Site fields the three columns hold. Mirrored as
         *  Config::HOME_FIELDS_MAX so a generated home-field table can
         *  static_assert against it: _field_place() silently ignores extras, so
         *  a site has to catch that at build time. */
        static constexpr uint8_t  FIELD_CAP = COLS * COL_MAX - UPTIME_CELLS;
        static_assert(FIELD_CAP == Config::HOME_FIELDS_MAX,
                      "Config::HOME_FIELDS_MAX must track the three columns' capacity");
        static constexpr uint16_t COL_TOP = 7;
        /** @brief Value width shared by the three columns (80 cols / 3). */
        static constexpr uint16_t COL_W = 13;
        /** @} */

        /** @name Uptime readout
         *  Column 1's top cell: same label / value columns as the fields below it
         *  ("DDDd HH:MM:SS" is exactly one value field wide).
         *  @{ */
        static constexpr uint16_t UPTIME_ROW = COL_TOP, UPTIME_VAL = 13,
                                  UPTIME_W = COL_W;
        /** @} */

        /** @brief First content row below the fixed layout (uptime + columns). */
        static constexpr uint16_t PAYLOAD_ROW = COL_TOP + COL_MAX;

        /** @brief First screen row of the bottom log band. Clamped so it never
         *  starts above the fixed content (logo + summary); on a too-short
         *  screen the band collapses to empty (band_top == rows). */
        uint16_t log_band_top() const;

        /** @name Draw hooks - override to customize the page.
         *  @{ */

        /** @brief Draw the ASCII logo / banner block. */
        virtual void draw_logo();

        /** @brief Firmware identity block (site name, version, commit),
         *  right-aligned top-right (rows 3-5), clear of the logo. Dot leaders
         *  align the colons. Static (compile-time values). Drawn after the
         *  logo, whose clear-to-eol would wipe it. */
        virtual void draw_id_block();

        /** @brief Draw the divider above the bottom log band: a reverse-video
         *  bar with a normal-video " LOGS " tab near the left. Static (drawn
         *  once). */
        void draw_log_header(uint16_t row);

        /** @brief Place site home-field @p i: its label row + column, value
         *  column + width, and diff-cache slot. Fills column-major (the first
         *  column's fields, then the second, then the third). @return false past
         *  FIELD_CAP. */
        bool _field_place(uint8_t i, uint16_t& row, uint16_t& lbl, uint16_t& val,
                          uint16_t& w) const {
            // Per-column geometry (a static constexpr member would be odr-used
            // here and need an out-of-class definition). Column 1 starts below
            // the uptime readout and its spacer, so it is that much shorter.
            static const uint16_t LBL[COLS] = { 2, 28, 54 };
            static const uint16_t VAL[COLS] = { 13, 39, 65 };
            static const uint16_t TOP[COLS] = { COL_TOP + UPTIME_CELLS,
                                                COL_TOP, COL_TOP };
            static const uint8_t  MAX[COLS] = { COL_MAX - UPTIME_CELLS,
                                                COL_MAX, COL_MAX };
            for (uint8_t c = 0; c < COLS; c++) {
                if (i < MAX[c]) {
                    row = (uint16_t)(TOP[c] + i);
                    lbl = LBL[c]; val = VAL[c]; w = COL_W;
                    return true;
                }
                i = (uint8_t)(i - MAX[c]);
            }
            return false;
        }

        void draw_static() override;

        void draw_dynamic() override;

        /** @brief Uptime counter - hot field, refreshed every second,
         *  un-highlighted (it changes every tick). */
        void draw_hot() override;

        /** @} */

    public:
        /** @brief @p fields (+ @p count) is the site home-field table, @p io
         *  what resolves them and @p refs where their names were resolved at
         *  bring-up (all borrowed; may be null/0 for a bare home screen). */
        HomePage(Console& console, const Vars::HomeVar* fields = nullptr,
                 uint8_t count = 0, const Vars::Bank* io = nullptr,
                 const Vars::VarRef* refs = nullptr)
            : MetricPage(console),
              Logs::BufferedLogWriter(nullptr, "home_log_level"),
              _fields(fields), _refs(refs), _field_count(count), _io(io) {
            // cold-field redraw cadence is MetricPage's default (PAGE_COLD_MS)
            // uptime ticks every second
            _hot_ms     = Config::PAGE_HOT_MS;
            // The band is a glance, not a record: NOTICE and up, so boot
            // chatter cannot push the one line worth seeing off the bottom.
            // The full stream is the LOGS page.
            set_min_level(Logs::LOG_NOTICE);
        }

        const char* name() const override { return "HOME"; }

        /** @name Focus & update (also drive the log band)
         *  @{ */

        void focus() override;

        void deactivate() override {
            MetricPage::deactivate();
            _log_active = false;
        }

        void tick(bool active) override;

        /** @} */

    private:
        /** @name BufferedLogWriter interface
         *  @{ */

        /** @brief The page's own name, so a loss report says which sink. */
        const char* sink_name() const override { return name(); }
        size_t write(const char* buf, size_t len) override;

        /** @} */
    };

}}

#endif // PAGES_HOME_H
