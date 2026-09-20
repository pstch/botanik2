#ifndef PAGES_SYSTEM_PAGE_H
#define PAGES_SYSTEM_PAGE_H

/**
 * @file status.h
 * @brief SystemPage - the SYSTEM page, and the base a site builds its own on.
 *
 * Answers the questions no other page does and the status bar cannot fit: is
 * the unit in service, is the watchdog being kicked, what is the task table
 * costing, and are the two field buses talking. Everything the row-0 bar
 * already carries - wall clock, CPU load, stack - is deliberately NOT repeated
 * here; a status page that restates the chrome wastes the one screen an
 * operator opens when something is wrong.
 *
 * A site subclasses it and overrides the three payload hooks; there is no type
 * parameter and no data(), a derived page holds whatever it needs itself.
 * Derived content starts at PAYLOAD_ROW, which is DERIVED from what is compiled
 * in, so a build without the network does not leave its rows blank.
 *
 * Drawing is diffed by MetricPage. The alarm set is on the terminal-owned
 * bottom bar, not here.
 */

#include <cstdint>
#include <cstdio>

#include "libbotanik/config/scheduler.gen.h"
#include "libbotanik/config/terminal.gen.h"
#include "libbotanik_features.h"

#include "libbotanik/core/context.h"
#include "libbotanik/pages/metric_page.h"
#include "libbotanik/config/vars.gen.h"
// HomeVar, which the site's half of the band is a table of. The only thing this
// header still needs from vars/; the draw bodies take the rest in the .cpp.
#include "libbotanik/vars/access.h"


namespace Botanik { namespace Pages {

    /** @brief Full-screen status display base (see file header). */
    class SystemPage : public MetricPage {
    protected:
        /** @name State
         *  @{ */

        /** @brief The subsystem handles the system band renders from. Injected,
         *  not read back off the console. */
        const SystemContext* _sys;

        /** @name The site's own half of the band
         *  Borrowed and flash-resident, like the home table: a label and the
         *  row it shows, both generated. @{ */
        const Vars::HomeVar* _site_fields = nullptr;
        const Vars::Bank*    _io          = nullptr;
        uint8_t              _site_count  = 0;
        /** @} */

        /** @} */

        /** @name Band rows
         *  DERIVED, not declared: a build without the network must not leave
         *  its two rows blank and start the payload below them anyway.
         *  @{ */
        static constexpr uint16_t ROW_HEADER = 2;
        static constexpr uint16_t ROW_RUN    = 3;   ///< run state | watchdog
        static constexpr uint16_t ROW_BEAT   = 4;   ///< jitter    | task period
        static constexpr uint16_t ROW_TASKS  = 5;   ///< task table cost
#if BOTANIK_FEATURE_NETWORK
        static constexpr uint16_t ROW_NET    = 6;   ///< link      | NTP
        static constexpr uint16_t ROW_NETIP  = 7;   ///< address   | MQTT
        static constexpr uint16_t _ROW_AFTER_NET = ROW_NETIP;
#else
        static constexpr uint16_t _ROW_AFTER_NET = ROW_TASKS;
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT || BOTANIK_FEATURE_MODBUS_TCP_SERVER
        static constexpr uint16_t ROW_BUS  = _ROW_AFTER_NET + 1;   ///< RTU | TCP
        static constexpr uint16_t _ROW_END = ROW_BUS;
#else
        static constexpr uint16_t _ROW_END = _ROW_AFTER_NET;
#endif
        /** @brief First row a derived page owns; one blank row separates it
         *  from the band above. */
        static constexpr uint16_t PAYLOAD_ROW = _ROW_END + 2;
        /** @} */

        /** @brief The two-column layout at the CURRENT width.
         *
         *  Computed rather than fixed at 80: the console runs 80..132, and a
         *  grid pinned to 80 sits in the left 60% of a wide terminal. Widths
         *  are clamped to what a value cell can hold. */
        struct Band { uint16_t l_lbl, l_val, l_w, r_lbl, r_val, r_w; };
        Band band() const {
            const uint16_t cols = _terminal.cols();
            const uint16_t half = (uint16_t)(cols / 2);
            Band b;
            b.l_lbl = 2;
            b.l_val = 16;
            b.l_w   = (half > b.l_val + 2u) ? (uint16_t)(half - b.l_val - 2u) : 1u;
            b.r_lbl = (uint16_t)(half + 2u);
            b.r_val = (uint16_t)(half + 14u);
            b.r_w   = (cols > b.r_val + 3u) ? (uint16_t)(cols - b.r_val - 3u) : 1u;
            if (b.l_w > FIELD_MAX) b.l_w = FIELD_MAX;
            if (b.r_w > FIELD_MAX) b.r_w = FIELD_MAX;
            return b;
        }

        /** @brief Right-justify @p s into a @p width-wide field. Returns
         *  @p buf.
         *  @note Minimum width, not maximum: an over-long @p s passes through
         *  and _cell() then truncates its TAIL, which for right-justified text
         *  drops the digits that matter. Keep the formats inside the width. */
        static const char* rjust(char* buf, size_t cap, uint16_t width, const char* s) {
            if (width >= cap) width = (uint16_t)(cap - 1);
            snprintf(buf, cap, "%*s", (int)width, s);
            return buf;
        }

        /** @name Draw hooks - override to customize the page.
         *  @{ */

        void draw_static() override;

        void draw_dynamic() override;

        /** @brief What the task table costs, two ways: the SUM of the per-task
         *  means is an upper bound on a beat (every task due at once), the DUTY
         *  is that spread over each task's own rate, which predicts headroom.
         *  @note Both need TaskCost timed off the high-resolution counter: on
         *  the low-power clock most tasks read 0. @see HAL::Clock::hrt_ticks */
        void _draw_tasks(const Band& b);

#if BOTANIK_FEATURE_NETWORK
        /** @brief The network band. Each cell keeps its OWN feature gate: the
         *  pointer is declared whatever the build (@see SystemContext), but
         *  without its header the pointee is incomplete and the accessor below
         *  would not compile. The rows stay either way and read "off", so the
         *  grid does not reflow between builds. */
        void _draw_network(const Band& b);
#endif

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT || BOTANIK_FEATURE_MODBUS_TCP_SERVER
        /** @brief The two field buses. For a greenhouse PLC this is the thing
         *  most worth a permanent readout: a dead RS-485 segment is a plant
         *  running on declared values. */
        void _draw_buses(const Band& b);
#endif

        /** @brief Per-second fields: the two that say whether the unit is
         *  alive and being allowed to stay that way, plus the jitter EMA which
         *  moves every beat. Un-highlighted - a never-settling value would
         *  churn the reverse-video. */
        void draw_hot() override;

        /** @name Payload hooks - one per cadence the base has.
         *
         *  All three, so a subclass never has to override draw_hot() and
         *  remember to chain to it.
         *  @warning The hot and cold sets must be DISJOINT: a field drawn from
         *  both paints twice a cycle and flickers. @{ */

        /** @name The site's half of the band
         *
         *  Two columns of six under the library's rows, laid out by the same
         *  band() the rows above use, so both halves line up at any width. A
         *  field past SYSTEM_FIELDS_MAX is dropped rather than drawn off the
         *  bottom - the static_assert in the site's generated header is what
         *  catches it before that.
         *  @{ */

        /** @brief Rows the site half occupies, given how many fields it has. */
        uint16_t _site_rows() const;

        void _draw_site_labels();

        void _draw_site_values();

        /** @} */

        /** @brief Site labels and headers, drawn once on focus. Base: the
         *  site's own table. */
        virtual void draw_payload_static() { _draw_site_labels(); }
        /** @brief Site values on the cold cadence. Base: the site's own table. */
        virtual void draw_payload_dynamic() { _draw_site_values(); }
        /** @brief Site values on the per-second cadence. Base: nothing. */
        virtual void draw_payload_hot() {}

        /** @} */

        /** @} */

    public:
        /** @brief Bind to @p console, reading the system band from @p sys.
         *
         *  @p sys may be null - every cell then renders blank rather than
         *  faulting, which is what a rig with no aggregate wants. @p fields
         *  (+ @p count) is the site's SYSTEM table and @p io the bank its rows
         *  are in. All borrowed, all optional: a site that declares none gets
         *  the library band alone. */
        SystemPage(Console& console, const SystemContext* sys,
                   const Vars::HomeVar* fields = nullptr, uint8_t count = 0,
                   const Vars::Bank* io = nullptr)
            : MetricPage(console), _sys(sys)
            , _site_fields(fields), _io(io), _site_count(count)
        {
            _hot_ms = Config::PAGE_HOT_MS;   // run state + watchdog + jitter
        }

        const char* name() const override { return "SYSTEM"; }
    };

}}

#endif // PAGES_SYSTEM_PAGE_H
