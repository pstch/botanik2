/**
 * @file firmware/bindings.cpp
 * @brief The binder's walk, and one reader per configurable component.
 *
 * The only place that includes both a component's header and the bank. A reader
 * is the whole coupling: delete this file and every component here still builds,
 * configured by whoever owns it.
 *
 * Under `firmware/` rather than beside its header: knowing every optional
 * subsystem is the composition root's job, and reaching up into console/,
 * network/ and modbus/ from `vars/` inverted the layer map. The interface
 * (`vars/bindings.h`) stays where its callers expect it.
 */
#include "libbotanik/vars/bindings.h"

#include "libbotanik/vars/model.h"
#include "libbotanik/vars/params.h"
#include "libbotanik/tasks/tasks.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/logs/topics.h"
#include "libbotanik/utils/assert.h"

#include "libbotanik_features.h"

#if BOTANIK_FEATURE_CONSOLE
#include "libbotanik/console/console.h"
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR
#include "libbotanik/firmware/stacks.h"
#endif
#if BOTANIK_FEATURE_NETWORK
#include "libbotanik/network/network.h"
#endif

#if BOTANIK_FEATURE_NTP
#include "libbotanik/network/ntp.h"
#endif
#if BOTANIK_FEATURE_MQTT
#include "libbotanik/network/mqtt.h"
#include "libbotanik/network/telemetry.h"
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
#include "libbotanik/modbus/tcp/slave.h"
#endif
#if BOTANIK_FEATURE_SUN_TRACKER
#include "libbotanik/sun/sun_tracker.h"
#endif

namespace Botanik { namespace Vars {

    const char* host_or_gateway(const Bank& io, const char* row) {
        const char* t = io.text(io.var(row));
        if (t != nullptr && t[0] != '\0') return t;
        const char* gw = io.text(io.var("net_gw"));
        return (gw != nullptr) ? gw : "";
    }


    // The walk

    void ConfigBinder::add(const char* serial, void* target,
                           void (*apply)(void*, const Bank&, bool)) {
        if (target == nullptr || apply == nullptr) return;
        if (_len >= Config::CONFIG_BINDINGS_MAX) {
            // A dropped binding leaves its component on its CONSTRUCTED
            // defaults while the rows it should have read sit in the table
            // looking live - the parameter edits, and nothing moves.
            BOTANIK_ASSERT(!"config bindings full: raise CONFIG_BINDINGS_MAX");
            if (_logger != nullptr)
                _logger->critical(Logs::BOOT,
                                  "binding '%s' refused: raise CONFIG_BINDINGS_MAX (%u)",
                                  (serial != nullptr) ? serial : "?",
                                  (unsigned)Config::CONFIG_BINDINGS_MAX);
            return;
        }
        ConfigBinding& b = _tab[_len++];
        b.serial = serial;
        b.target = target;
        b.apply = apply;
        b.applied = 0;
        b.ever = false;
    }

    void ConfigBinder::register_tasks() {
        _tasks->add(*this, &ConfigBinder::_task, "sys_config", Config::CONFIG_COMMIT_MS,
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

#if BOTANIK_FEATURE_NETWORK
    /** @brief A row's text into a fixed field, always NUL-terminated. A config
     *  struct OWNS its strings: the pool a row's text lives in belongs to the
     *  bank, and a build with no bank has neither. */
    static void copy_str(char* dst, size_t cap, const char* src) {
        if (src == nullptr) { dst[0] = '\0'; return; }
        size_t i = 0;
        for (; i + 1 < cap && src[i] != '\0'; i++) dst[i] = src[i];
        dst[i] = '\0';
    }
#endif

#if BOTANIK_FEATURE_CONSOLE
    /** @brief The `console_` group. The band guards the way IN; a corrupt store
     *  reaches set_config() by another road, so the make is re-checked here
     *  rather than trusted - an out-of-range one would index a handshake switch
     *  with no case for it. */
    static void apply_console(void* target, const Bank& io, bool announce) {
        Terminal::Console& console = *static_cast<Terminal::Console*>(target);
        Terminal::ConsoleConfig cfg;
        cfg.baud = io.var("console_baud").value().as<uint32_t>();
        cfg.cols = io.var("console_cols").value().as<uint16_t>();
        cfg.rows = io.var("console_rows").value().as<uint16_t>();
        const uint8_t type = io.var("console_type").value().as<uint8_t>();
        if (type <= (uint8_t)Terminal::Ansi::TerminalType::MINITEL_2)
            cfg.type = (Terminal::Ansi::TerminalType)type;
        console.set_config(cfg, announce);
    }

    void bind_console(ConfigBinder& binder, Terminal::Console& console) {
        binder.add("console", &console, &apply_console);
    }
#endif

#if BOTANIK_FEATURE_NETWORK
    /** @brief The `net_` group. Staged: an address, a mask and a gateway from
     *  three different networks is not a link anyone can bring up. */
    static void apply_network(void* target, const Bank& io, bool) {
        Network::NetworkConfig cfg;
        cfg.enabled = io.var("net_enable").as_bool();
        cfg.dhcp    = io.var("net_dhcp").as_bool();
        copy_str(cfg.ip,   sizeof(cfg.ip),   io.text(io.var("net_ip")));
        copy_str(cfg.mask, sizeof(cfg.mask), io.text(io.var("net_mask")));
        copy_str(cfg.gw,   sizeof(cfg.gw),   io.text(io.var("net_gw")));
        copy_str(cfg.dns1, sizeof(cfg.dns1), io.text(io.var("net_dns1")));
        copy_str(cfg.dns2, sizeof(cfg.dns2), io.text(io.var("net_dns2")));
        static_cast<Network::NetworkHandler*>(target)->set_config(cfg);
    }

    void bind_network(ConfigBinder& binder, Network::NetworkHandler& net) {
        binder.add("net", &net, &apply_network);
    }
#endif

#if BOTANIK_FEATURE_NTP
    /** @brief The `ntp_` group. */
    static void apply_ntp(void* target, const Bank& io, bool) {
        Network::NtpConfig cfg;
        cfg.enabled  = io.var("ntp_enable").as_bool();
        cfg.port     = io.var("ntp_port").value().as<uint16_t>();
        cfg.poll_sec = io.var("ntp_poll_sec").value().as<uint32_t>();
        copy_str(cfg.srv1, sizeof(cfg.srv1), host_or_gateway(io, "ntp_srv1"));
        // srv2 is the FALLBACK and stays empty when it is empty: falling back
        // to the same gateway srv1 already is would be no fallback at all.
        copy_str(cfg.srv2, sizeof(cfg.srv2), io.text(io.var("ntp_srv2")));
        static_cast<Network::NtpClient*>(target)->set_config(cfg);
    }

    void bind_ntp(ConfigBinder& binder, Network::NtpClient& ntp) {
        binder.add("ntp", &ntp, &apply_ntp);
    }
#endif

#if BOTANIK_FEATURE_MQTT
    /** @brief The `mqtt_` group. Shared with the telemetry sender, which binds
     *  the same serial to its own struct. */
    static void apply_mqtt(void* target, const Bank& io, bool) {
        Network::MqttConfig cfg;
        cfg.enabled = io.var("mqtt_enable").as_bool();
        cfg.port    = io.var("mqtt_port").value().as<uint16_t>();
        cfg.ka_sec  = io.var("mqtt_ka_sec").value().as<uint16_t>();
        copy_str(cfg.host, sizeof(cfg.host), host_or_gateway(io, "mqtt_host"));
        static_cast<Network::MqttClient*>(target)->set_config(cfg);
    }

    void bind_mqtt(ConfigBinder& binder, Network::MqttClient& mqtt) {
        binder.add("mqtt", &mqtt, &apply_mqtt);
    }
#endif

#if BOTANIK_FEATURE_MQTT
    /** @brief The telemetry half of the `mqtt_` group. Same serial as the
     *  client's: one commit, two structs. */
    static void apply_telemetry(void* target, const Bank& io, bool) {
        Network::TelemetryConfig cfg;
        const int32_t sec = (int32_t)io.var("mqtt_tlm_sec").as_i64();
        cfg.period_ms = sec > 0 ? (uint32_t)sec * 1000u : 0u;
        cfg.retain    = io.var("mqtt_retain").as_bool();
        static_cast<Network::MqttSender*>(target)->set_config(cfg);
    }

    void bind_telemetry(ConfigBinder& binder, Network::MqttSender& tlm) {
        binder.add("mqtt", &tlm, &apply_telemetry);
    }
#endif

#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
    /** @brief The `mb` group. */
    static void apply_modbus_tcp(void* target, const Bank& io, bool) {
        Modbus::TcpSlaveConfig cfg;
        cfg.enabled  = io.var("mb_enable").as_bool();
        cfg.writable = io.var("mb_write_enable").as_bool();
        cfg.port     = io.var("mb_port").value().as<uint16_t>();
        static_cast<Modbus::TcpSlave*>(target)->set_config(cfg);
    }

    void bind_modbus_tcp(ConfigBinder& binder, Modbus::TcpSlave& slave) {
        binder.add("mb", &slave, &apply_modbus_tcp);
    }
#endif

#if BOTANIK_FEATURE_LOGS_MONITOR
    /** @brief `monitor_baud`, unstaged: one row, and a wrong rate costs the log
     *  and nothing else. */
    static void apply_monitor(void* target, const Bank& io, bool) {
        MonitorConfig cfg;
        cfg.baud = io.var("monitor_baud").value().as<uint32_t>();
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
