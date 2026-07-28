/**
 * @file test_config_params.cpp
 * @brief Tests for the ConfigParam model (core/config.h): the library table,
 *        the generic read/format/validate/parse helpers, and the ConfigHandler
 *        registry (library + site params). The backends (Plc/Local) and the
 *        write path are exercised elsewhere; here we cover the pure helpers.
 */
#include <cstdint>
#include <cstring>

#include "doctest.h"

#include "libbotanik/core/config.h"

using namespace Botanik;
using namespace Botanik::Cfg;

namespace {
    const ConfigParam* find(const char* name) {
        for (uint8_t i = 0; i < CONFIG_PARAM_COUNT; i++)
            if (strcmp(CONFIG_PARAMS[i].name, name) == 0) return &CONFIG_PARAMS[i];
        return nullptr;
    }

    // A trivial backend: a local image, no write path (set() stubbed). Enough to
    // exercise the registry (count/param/format).
    struct TestHandler : ConfigHandler {
        PlcIO::PlcOutputConfig_t img{};
        TestHandler() : ConfigHandler(nullptr) {}
        const PlcIO::PlcOutputConfig_t* image() const override { return &img; }
        bool set(const ConfigParam&, const char*) override { return false; }
    };
}

TEST_CASE("config table: sane offsets, string capacities, group tags") {
    CHECK(CONFIG_PARAM_COUNT == 16);
    for (uint8_t i = 0; i < CONFIG_PARAM_COUNT; i++) {
        const ConfigParam& p = CONFIG_PARAMS[i];
        CHECK(p.out_offset >= 0);
        CHECK((size_t)p.out_offset < sizeof(PlcIO::PlcOutputConfig_t));
        CHECK(p.in_offset == p.out_offset);   // config: input mirror shares the layout
        CHECK(p.group < CFG_GROUP_COUNT);
        if (is_string(p)) CHECK(p.capacity == Config::PLC_HOST_STR_LEN);
    }
    // Spot-check a few names resolve and carry the expected type/group.
    REQUIRE(find("net_dhcp") != nullptr);
    CHECK(find("net_dhcp")->type == SCALAR_U8);
    CHECK(find("net_ip")->type == SCALAR_STRING);
    CHECK(find("ntp_poll_s")->group == CFG_NTP);
    CHECK(find("geo_latitude")->type == SCALAR_F32);
}

TEST_CASE("group_serial_offset matches the image serial fields") {
    CHECK(group_serial_offset(CFG_NET)  == offsetof(PlcIO::PlcOutputConfig_t, net_serial));
    CHECK(group_serial_offset(CFG_NTP)  == offsetof(PlcIO::PlcOutputConfig_t, ntp_serial));
    CHECK(group_serial_offset(CFG_MQTT) == offsetof(PlcIO::PlcOutputConfig_t, mqtt_serial));
    CHECK(group_serial_offset(CFG_GEO)  == offsetof(PlcIO::PlcOutputConfig_t, geo_serial));
}

TEST_CASE("read + format values off an image") {
    PlcIO::PlcOutputConfig_t c{};
    c.net_dhcp     = 1;
    c.ntp_poll_s   = 64;
    c.mqtt_port    = 1883;
    c.geo_latitude = 48.8566f;
    strcpy(c.net_ip, "192.168.1.5");

    char buf[80];

    format_value(*find("net_dhcp"), &c, buf, sizeof(buf));
    CHECK(strcmp(buf, "1") == 0);

    format_value(*find("ntp_poll_s"), &c, buf, sizeof(buf));
    CHECK(strcmp(buf, "64") == 0);

    format_value(*find("mqtt_port"), &c, buf, sizeof(buf));
    CHECK(strcmp(buf, "1883") == 0);

    // f32 shown via format_f32 (trailing zeros trimmed).
    format_value(*find("geo_latitude"), &c, buf, sizeof(buf));
    CHECK(strcmp(buf, "48.8566") == 0);

    format_value(*find("net_ip"), &c, buf, sizeof(buf));
    CHECK(strcmp(buf, "192.168.1.5") == 0);

    CHECK(read_scalar(*find("ntp_poll_s"), &c) == 64);
}

TEST_CASE("parse_scalar: integers and bounds") {
    int32_t v = 0;

    // Integer within range.
    CHECK(parse_scalar(*find("ntp_poll_s"), "64", &v));
    CHECK(v == 64);

    // Out of range (u16 field, max 65535) and below min (0).
    CHECK_FALSE(parse_scalar(*find("ntp_poll_s"), "70000", &v));
    CHECK_FALSE(parse_scalar(*find("ntp_poll_s"), "-5", &v));

    // Malformed.
    CHECK_FALSE(parse_scalar(*find("ntp_poll_s"), "abc", &v));
    CHECK_FALSE(parse_scalar(*find("ntp_poll_s"), "12x", &v));
    CHECK_FALSE(parse_scalar(*find("ntp_poll_s"), "", &v));

    // Boolean-ish u8 bound.
    CHECK(parse_scalar(*find("net_dhcp"), "1", &v));
    CHECK_FALSE(parse_scalar(*find("net_dhcp"), "2", &v));
}

TEST_CASE("parse_f32: decimal degrees and bounds") {
    float f = 0.0f;
    CHECK(parse_f32(*find("geo_latitude"), "48.8566", &f));
    CHECK(f == doctest::Approx(48.8566));
    CHECK(parse_f32(*find("geo_latitude"), "-90", &f));
    CHECK(f == doctest::Approx(-90.0));
    CHECK_FALSE(parse_f32(*find("geo_latitude"), "91", &f));    // > +90 deg
    CHECK_FALSE(parse_f32(*find("geo_latitude"), "abc", &f));
    CHECK_FALSE(parse_f32(*find("geo_latitude"), "", &f));
}

TEST_CASE("validate_ipv4") {
    CHECK(validate_ipv4("192.168.1.5"));
    CHECK(validate_ipv4("0.0.0.0"));
    CHECK(validate_ipv4("255.255.255.0"));
    CHECK_FALSE(validate_ipv4("256.1.1.1"));
    CHECK_FALSE(validate_ipv4("1.2.3"));
    CHECK_FALSE(validate_ipv4("1.2.3.4.5"));
    CHECK_FALSE(validate_ipv4("1.2.3."));
    CHECK_FALSE(validate_ipv4("a.b.c.d"));
    CHECK_FALSE(validate_ipv4(""));               // empty rejected by default
    CHECK(validate_ipv4("", /*allow_empty=*/true));
}

TEST_CASE("validate_hostname") {
    CHECK(validate_hostname("pool.ntp.org", 64));
    CHECK(validate_hostname("broker-1.local", 64));
    CHECK_FALSE(validate_hostname("bad_host", 64));   // underscore
    CHECK_FALSE(validate_hostname("no spaces", 64));
    CHECK_FALSE(validate_hostname("", 64));           // empty rejected by default
    CHECK(validate_hostname("", 64, /*allow_empty=*/true));
    CHECK_FALSE(validate_hostname("toolong", 4));     // 7 chars, cap 4
}

TEST_CASE("ConfigHandler registry: library + site params") {
    TestHandler h;
    CHECK(h.count() == CONFIG_PARAM_COUNT);
    CHECK(strcmp(h.param(0).name, CONFIG_PARAMS[0].name) == 0);

    // name, out_offset, in_offset, type, str, unit, desc, notes, deflt, min, max, capacity, str_kind, group
    static const ConfigParam site[] = {
        {"site_a", 0, 0, SCALAR_U8, nullptr, "", "", "", "", 0, 1, 0, STR_NONE, CFG_NET},
        {"site_b", 0, 0, SCALAR_U8, nullptr, "", "", "", "", 0, 1, 0, STR_NONE, CFG_NET},
    };
    h.set_site_params(site, 2);
    CHECK(h.count() == (uint8_t)(CONFIG_PARAM_COUNT + 2));
    CHECK(strcmp(h.param(CONFIG_PARAM_COUNT).name, "site_a") == 0);
    CHECK(strcmp(h.param((uint8_t)(CONFIG_PARAM_COUNT + 1)).name, "site_b") == 0);

    // format() reads through image(); default image is zeroed.
    h.img.mqtt_port = 8883;
    char buf[32];
    // find mqtt_port's index in the library table
    for (uint8_t i = 0; i < CONFIG_PARAM_COUNT; i++)
        if (strcmp(h.param(i).name, "mqtt_port") == 0) {
            h.format(i, buf, sizeof(buf));
            CHECK(strcmp(buf, "8883") == 0);
        }
}
