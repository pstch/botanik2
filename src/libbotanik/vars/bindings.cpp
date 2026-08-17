/**
 * @file vars/bindings.cpp
 * @brief The binder's walk, and one reader per configurable component.
 *
 * The only place that includes both a component's header and the bank. A reader
 * is the whole coupling: delete this file and every component here still builds,
 * configured by whoever owns it.
 */
#include "libbotanik/vars/bindings.h"

#include "libbotanik/vars/model.h"
#include "libbotanik/vars/params.h"
#include "libbotanik/tasks/tasks.h"

#include "libbotanik_features.h"

#if BOTANIK_FEATURE_CONSOLE
#include "libbotanik/console/console.h"
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR
#include "libbotanik/firmware/stacks.h"
#endif
#if BOTANIK_FEATURE_SUN_TRACKER
#include "libbotanik/sun/sun_tracker.h"
#endif

namespace Botanik { namespace Vars {

    // The walk

    void ConfigBinder::add(const char* serial, void* target,
                           void (*apply)(void*, const Bank&, bool)) {
        if (_len >= Config::CONFIG_BINDINGS_MAX || target == nullptr || apply == nullptr) return;
        ConfigBinding& b = _tab[_len++];
        b.serial = serial;
        b.target = target;
        b.apply = apply;
        b.applied = 0;
        b.ever = false;
    }

    void ConfigBinder::register_tasks() {
        _tasks->add(*this, &ConfigBinder::_task, "config", Tasks::GROUP_SLOW,
                    Tasks::PHASE_CONFIG);
    }

    void ConfigBinder::_task(Component* c, const Tasks::Tick&) {
        static_cast<ConfigBinder*>(c)->apply_all();
    }

    void ConfigBinder::apply_all() {
        if (_io == nullptr) return;
        for (uint8_t i = 0; i < _len; i++) {
            ConfigBinding& b = _tab[i];
            // An unstaged binding re-reads every beat; set_config() is required
            // to be a no-op when nothing moved, so the cost is the read.
            const uint32_t serial = (b.serial != nullptr) ? group_serial(_io, b.serial) : 0;
            // A group with no rows answers 0: this site does not configure that
            // component, so it keeps its struct defaults and is never pushed.
            if (b.serial != nullptr && serial == 0) continue;
            if (b.ever && b.serial != nullptr && serial == b.applied) continue;
            b.applied = serial;
            // Announcing is for a CHANGE, and the first push is not one: it
            // is the stored image arriving at bring-up.
            const bool announce = b.ever;
            b.ever = true;
            b.apply(b.target, *_io, announce);
        }
    }

    // The readers

#if BOTANIK_FEATURE_CONSOLE
    /** @brief The `console_` group. The band guards the way IN; a corrupt store
     *  reaches set_config() by another road, so the make is re-checked here
     *  rather than trusted - an out-of-range one would index a handshake switch
     *  with no case for it. */
    static void apply_console(void* target, const Bank& io, bool announce) {
        Terminal::Console& console = *static_cast<Terminal::Console*>(target);
        Terminal::ConsoleConfig cfg;
        cfg.baud = io.var("console_baud").value().as_u32();
        cfg.cols = io.var("console_cols").value().as_u16();
        cfg.rows = io.var("console_rows").value().as_u16();
        const uint8_t type = io.var("console_type").value().as_u8();
        if (type <= (uint8_t)Terminal::Ansi::TerminalType::MINITEL_2)
            cfg.type = (Terminal::Ansi::TerminalType)type;
        console.set_config(cfg, announce);
    }

    void bind_console(ConfigBinder& binder, Terminal::Console& console) {
        binder.add("console", &console, &apply_console);
    }
#endif

#if BOTANIK_FEATURE_LOGS_MONITOR
    /** @brief `monitor_baud`, unstaged: one row, and a wrong rate costs the log
     *  and nothing else. */
    static void apply_monitor(void* target, const Bank& io, bool) {
        MonitorConfig cfg;
        cfg.baud = io.var("monitor_baud").value().as_u32();
        static_cast<MonitorStack*>(target)->set_config(cfg);
    }

    void bind_monitor(ConfigBinder& binder, MonitorStack& monitor) {
        binder.add(nullptr, &monitor, &apply_monitor);
    }
#endif

#if BOTANIK_FEATURE_SUN_TRACKER
    /** @brief The `geo_` group. The pair is taken TOGETHER, on the commit: that
     *  is what makes it one location rather than two edited numbers. */
    static void apply_geo(void* target, const Bank& io, bool) {
        Sun::GeoConfig geo;
        geo.lat_deg = io.var("geo_lat_deg").as_f32();
        geo.lon_deg = io.var("geo_lon_deg").as_f32();
        static_cast<Sun::SunPositionTracker*>(target)->set_config(geo);
    }

    void bind_sun(ConfigBinder& binder, Sun::SunPositionTracker& sun) {
        binder.add("geo", &sun, &apply_geo);
    }
#endif

}}  // namespace Botanik::Vars
