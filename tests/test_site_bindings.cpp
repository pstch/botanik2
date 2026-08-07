/**
 * @file test_site_bindings.cpp
 * @brief The selected site's FIELD-BUS bindings, checked against the rules the
 *        RTU master assumes but cannot enforce.
 *
 * test_site_names.cpp already checks that every name a table quotes resolves,
 * and that no two published rows claim the same TCP slave register. This file
 * checks the other address space: what each row occupies on the wire.
 *
 * The case that earned it: 43 meter rows declared a two-register span while
 * stepping the address by one, so every reading after the first in a block
 * decoded the high word of its predecessor. Nothing failed - not the build, not
 * the boot, not a test. Only a real meter would have said so.
 */
#include <cstdint>
#include <cstring>
#include <string>   // doctest streams a const char* as a pointer

#include "doctest.h"

#include "libbotanik_site.h"    // resolves BOTANIK_SITE -> BOTANIK_SITE_HEADER
#include "libbotanik.h"         // Vars::Bank, Modbus::is_bit, Config::MODBUS_*

#include BOTANIK_SITE_HEADER

using namespace Botanik;
using namespace Botanik::Vars;

namespace {

const VarTables& tables() { return Site::g_site.vars; }

bool on_the_bus(const Variable& d) {
    return d.bind.transport == XP_MODBUS;
}

/** @brief Registers, or coils, that @p d occupies: [reg, reg + regs). */
uint32_t span_end(const Variable& d) {
    const uint16_t n = d.bind.regs != 0 ? d.bind.regs : 1;
    return (uint32_t)d.bind.reg + n;
}

/** @brief doctest stringifies a const char* as an address, so names go
 *  through std::string or a failure names nothing useful. */
std::string nm(const Variable& d) { return std::string(d.name); }

}  // namespace

TEST_CASE("site bindings: no two field-bus rows overlap on a device") {
    const VarTables& t = tables();

    for (uint16_t i = 0; i < t.count; ++i) {
        const Variable& a = t.var[i];
        if (!on_the_bus(a)) continue;
        for (uint16_t j = (uint16_t)(i + 1); j < t.count; ++j) {
            const Variable& b = t.var[j];
            if (!on_the_bus(b)) continue;
            // Same device, and the same address space: bit function codes and
            // register function codes are numbered apart.
            if (a.bind.device != b.bind.device) continue;
            if (Modbus::is_bit(a.bind.fc) != Modbus::is_bit(b.bind.fc)) continue;
            // A read and a write of the same coil are the same coil.
            const bool overlap = (uint32_t)a.bind.reg < span_end(b) &&
                                 (uint32_t)b.bind.reg < span_end(a);
            INFO("dev " << (int)a.bind.device
                        << ": '" << nm(a) << "' [" << a.bind.reg << ","
                        << span_end(a) << ") overlaps '" << nm(b) << "' ["
                        << b.bind.reg << "," << span_end(b) << ")");
            CHECK_FALSE(overlap);
        }
    }
}

TEST_CASE("site bindings: a row's span matches the width its wire type needs") {
    const VarTables& t = tables();

    for (uint16_t i = 0; i < t.count; ++i) {
        const Variable& d = t.var[i];
        if (!on_the_bus(d)) continue;
        if (Modbus::is_bit(d.bind.fc)) {
            INFO("'" << nm(d) << "' is a bit row spanning " << d.bind.regs);
            CHECK(d.bind.regs <= 1);
            continue;
        }
        // 32-bit wire values take a register pair; 16-bit ones take one.
        const uint16_t want = (uint16_t)((type_width(d.bind.wire) + 1) / 2);
        INFO("'" << nm(d) << "' wire type wants " << want
                 << " register(s), declares " << d.bind.regs);
        CHECK(d.bind.regs == want);
    }
}

TEST_CASE("site bindings: every field-bus input can go stale") {
    // expire_stale() skips a row whose timeout_ms is 0, and the driver does not
    // invalidate on a failed read - so such a row would hold its last value for
    // ever after the device died, reading GOOD the whole time.
    const VarTables& t = tables();

    for (uint16_t i = 0; i < t.count; ++i) {
        const Variable& d = t.var[i];
        if (!on_the_bus(d) || d.role != ROLE_INPUT) continue;
        INFO("'" << nm(d) << "' is a field-bus input with no timeout");
        CHECK(d.timeout_ms > 0);
    }
}

TEST_CASE("site bindings: no read block can exceed a device's own request limit") {
    // The SDM630 refuses a request for more than 40 parameters (80 registers),
    // and answers with an exception rather than data. The planner's ceiling is
    // MODBUS_MAX_REGS_PER_BLOCK; this checks the site stays inside it, which is
    // what keeps a merged block legal at the far end.
    const VarTables& t = tables();

    for (uint16_t i = 0; i < t.count; ++i) {
        const Variable& d = t.var[i];
        if (!on_the_bus(d) || Modbus::is_bit(d.bind.fc)) continue;
        INFO("'" << nm(d) << "' spans " << d.bind.regs << " registers");
        CHECK(d.bind.regs <= Config::MODBUS_MAX_REGS_PER_BLOCK);
    }
}
