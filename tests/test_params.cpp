/**
 * @file test_params.cpp
 * @brief Tests for the Param model (params/params.h): the generic
 *        read/format/validate/parse helpers and the ParamHandler registry,
 *        driven by a self-contained parameter table (the library ships none -
 *        a site declares its parameters over its own payload). The backends
 *        (Plc/Local) and the write path are exercised elsewhere.
 */
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "doctest.h"

#include "libbotanik/params/params.h"
#include "libbotanik/params/store.h"   // ParamStore - the backend the handler drives

using namespace Botanik;
using namespace Botanik::Params;

namespace {
    // Self-contained config payload + parameter table (a site's stand-in).
    struct TestCfg {
        float    geo_latitude;
        uint32_t net;                ///< network group commit serial
        uint16_t ntp_poll_s;
        uint16_t mqtt_port;
        uint8_t  net_dhcp;
        uint8_t  _pad[3];
        char     net_ip[64];
    };

#define OFF(f) (int16_t)offsetof(TestCfg, f)
    const Param PARAMS[] = {
        // field {name, out_offset, type, digits, str, unit, desc, notes}, meta {def, min, max, len, kind, group}
        {{"net",          OFF(net),         SCALAR_U32,    0, nullptr, "",    "commit", ""}, {"0", 0, 1000000000, 0,  STR_NONE, "net"}},
        {{"net_dhcp",     OFF(net_dhcp),     SCALAR_U8,    0, nullptr, "",    "dhcp",   ""}, {"1", 0, 1,      0,  STR_NONE, "net"}},
        {{"net_ip",       OFF(net_ip),       SCALAR_STRING,0, nullptr, "",    "ip",     ""}, {"",  0, 0,      64, STR_IPV4, "net"}},
        {{"ntp_poll_s",   OFF(ntp_poll_s),   SCALAR_U16,   0, nullptr, "s",   "poll",   ""}, {"64",0, 65535,  0,  STR_NONE, "ntp"}},
        {{"mqtt_port",    OFF(mqtt_port),    SCALAR_U16,   0, nullptr, "",    "port",   ""}, {"1883",0,65535, 0,  STR_NONE, "mqtt"}},
        {{"geo_latitude", OFF(geo_latitude), SCALAR_F32,   4, nullptr, "deg", "lat",    ""}, {"48.8566", -90, 90, 0, STR_NONE, "geo"}},
    };
#undef OFF
    const uint8_t PARAM_COUNT = (uint8_t)(sizeof(PARAMS) / sizeof(PARAMS[0]));

    const Param* find(const char* name) {
        for (uint8_t i = 0; i < PARAM_COUNT; i++)
            if (strcmp(PARAMS[i].field.name, name) == 0) return &PARAMS[i];
        return nullptr;
    }

    // A trivial backend: a local image, no write path (set() stubbed). Enough to
    // exercise the registry (count/param/format). Since the store/handler split,
    // the image lives on the store and the handler is the generic one over it.
    struct TestStore : ParamStore {
        TestCfg img{};
        const void* image() const override { return &img; }
        SetResult set(const Param&, const char*) override { return SET_INVALID; }
    };

    struct TestHandler : ParamHandler {
        TestStore store;
        // The base only stores the pointer, so handing it a member that is
        // constructed a moment later is safe here.
        TestHandler() : ParamHandler(&store, nullptr) { set_site_params(PARAMS, PARAM_COUNT); }
        TestCfg& img() { return store.img; }
    };
}

TEST_CASE("config table: sane offsets, string capacities, group tags") {
    for (uint8_t i = 0; i < PARAM_COUNT; i++) {
        const Param& p = PARAMS[i];
        CHECK(p.field.out_offset >= 0);
        CHECK((size_t)p.field.out_offset < sizeof(TestCfg));
        CHECK(p.meta.group != nullptr);
        if (is_string(p)) CHECK(p.meta.len == sizeof(TestCfg::net_ip));
    }
    REQUIRE(find("net_dhcp") != nullptr);
    CHECK(find("net_dhcp")->field.type == SCALAR_U8);
    CHECK(find("net_ip")->field.type == SCALAR_STRING);
    CHECK(strcmp(find("ntp_poll_s")->meta.group, "ntp") == 0);
    CHECK(find("geo_latitude")->field.type == SCALAR_F32);
}

TEST_CASE("read + format values off an image") {
    TestCfg c{};
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

    // f32 shown to the param's `digits` (geo = 4 decimals).
    format_value(*find("geo_latitude"), &c, buf, sizeof(buf));
    CHECK(strcmp(buf, "48.8566") == 0);

    format_value(*find("net_ip"), &c, buf, sizeof(buf));
    CHECK(strcmp(buf, "192.168.1.5") == 0);

    CHECK(read_scalar(*find("ntp_poll_s"), &c) == 64);
}

TEST_CASE("parse_scalar: integers and bounds") {
    int32_t v = 0;

    CHECK(parse_scalar(*find("ntp_poll_s"), "64", &v));
    CHECK(v == 64);

    CHECK_FALSE(parse_scalar(*find("ntp_poll_s"), "70000", &v));
    CHECK_FALSE(parse_scalar(*find("ntp_poll_s"), "-5", &v));

    CHECK_FALSE(parse_scalar(*find("ntp_poll_s"), "abc", &v));
    CHECK_FALSE(parse_scalar(*find("ntp_poll_s"), "12x", &v));
    CHECK_FALSE(parse_scalar(*find("ntp_poll_s"), "", &v));

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

TEST_CASE("ParamHandler registry + format through the image") {
    TestHandler h;
    CHECK(h.count() == PARAM_COUNT);
    CHECK(strcmp(h.param(0).field.name, PARAMS[0].field.name) == 0);

    // find() resolves a registered parameter.
    CHECK(h.find("mqtt_port", 9) != nullptr);
    CHECK(h.find("nope", 4) == nullptr);

    // format() reads through image().
    h.img().mqtt_port = 8883;
    char buf[32];
    for (uint8_t i = 0; i < h.count(); i++)
        if (strcmp(h.param(i).field.name, "mqtt_port") == 0) {
            h.format(i, buf, sizeof(buf));
            CHECK(strcmp(buf, "8883") == 0);
        }
}
