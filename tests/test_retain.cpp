/**
 * @file test_retain.cpp
 * @brief F_RETAIN: the rows whose value IS the history, and what survives a
 *        reboot.
 *
 * Driven against a stub store rather than HAL::Storage, so a case can say "the
 * unit went down here" by simply making a second Bank over the same store - and
 * so nothing here depends on a file the other cases share.
 */
#include <cstdint>
#include <cstring>
#include <map>
#include <string>

#include "doctest.h"

#include "libbotanik/vars/model.h"
#include "libbotanik/vars/declare.h"

using namespace Botanik;
using namespace Botanik::Vars;

namespace {

/** @brief A store in a std::map, standing in for the flash. */
struct MemStore : public VarStore {
    std::map<std::string, uint64_t> params, overrides, retained;
    std::map<std::string, bool>     ovr_valid;
    int saves = 0;                       ///< retained writes attempted
    bool refuse = false;                 ///< make the "storage said no" path testable

    bool load(const Variable& d, Value& v, char*, uint16_t) override {
        auto it = params.find(d.name);
        if (it == params.end()) return false;
        v.v = Scalar::U(it->second);
        return true;
    }
    bool save(const Variable& d, const Value& v, const char*, uint16_t) override {
        params[d.name] = v.v.u;
        return true;
    }
    bool load_override(const Variable& d, Value& v) override {
        auto it = overrides.find(d.name);
        if (it == overrides.end()) return false;
        v.ovr = Scalar::U(it->second);
        v.ovr_valid = ovr_valid[d.name];
        return true;
    }
    bool save_override(const Variable& d, const Value& v) override {
        overrides[d.name] = v.ovr.u;
        ovr_valid[d.name] = v.ovr_valid;
        return true;
    }
    bool clear_override(const Variable& d) override {
        overrides.erase(d.name);
        return true;
    }
    bool load_retained(const Variable& d, Value& v) override {
        auto it = retained.find(d.name);
        if (it == retained.end()) return false;
        v.v = Scalar::U(it->second);
        return true;
    }
    bool save_retained(const Variable& d, const Value& v) override {
        saves++;
        if (refuse) return false;
        retained[d.name] = v.v.u;
        return true;
    }
};

// A table with one of each interesting kind. G() is the computed factory; the
// retained one carries F_RETAIN, its neighbour does not.
const Variable g_rows[] = {
    GR("total_litres", T_U32, Scalar::U(0), 0, "L", "litres delivered", ""),
    G ("plain_count",  T_U32, Scalar::U(0), 0, "",  "not retained",     ""),
    GR("total_hours",  T_F32, Scalar::F(0.0f), 1, "h", "hours run",     ""),
};
const uint16_t COUNT = (uint16_t)(sizeof(g_rows) / sizeof(g_rows[0]));

Value    g_vals[COUNT];
uint16_t g_index[COUNT];

VarTables tables() {
    VarTables t;
    t.var   = g_rows;
    t.val   = g_vals;
    t.count = COUNT;
    t.index = g_index;
    t.pool  = nullptr;
    t.pool_bytes = 0;
    return t;
}

}  // namespace

TEST_CASE("retain: only a computed row carrying F_RETAIN is retained") {
    // The predicate is one function so the load and save paths cannot disagree.
    // A parameter already persists, an input is overwritten by the next scan,
    // and restoring an OUTPUT from flash would energise a contactor before
    // anything had looked at the plant.
    const Variable computed = G("c", T_U32, Scalar::U(0), 0, "", "", "");
    CHECK_FALSE(Bank::retainable(computed));      // no flag

    for (uint8_t role = 0; role < 4; ++role) {
        Variable v = computed;
        v.flags = (uint8_t)(v.flags | F_RETAIN);
        v.role  = (VarRole)role;
        const bool want = (v.role == ROLE_COMPUTED);
        INFO("role " << (int)role);
        CHECK(Bank::retainable(v) == want);
    }
}

TEST_CASE("retain: a total survives the reboot, a plain computed row does not") {
    MemStore store;

    {
        VarTables t = tables();
        Bank a(t);
        a.init();
        a.set_store(&store);

        a.var(a.find("total_litres")).set_i(4321);
        a.var(a.find("plain_count")).set_i(99);
        CHECK(a.save_retained() >= 1);
    }

    // The unit goes down and comes back: a fresh bank over the same store.
    VarTables t2 = tables();
    Bank b(t2);
    b.init();
    b.set_store(&store);

    CHECK(b.var(b.find("total_litres")).i() == 0);   // nothing restored yet
    const uint16_t n = b.load_retained();
    CHECK(n >= 1);

    INFO("the total carries on from where it was");
    CHECK(b.var(b.find("total_litres")).i() == 4321);
    INFO("...and a computed row that did not ask to be retained starts clean");
    CHECK(b.var(b.find("plain_count")).i() == 0);
}

TEST_CASE("retain: a restored total reads as good, not as no-data") {
    // Otherwise every reader falls back to the declared default - which is the
    // zero the restore existed to avoid.
    MemStore store;
    store.retained["total_litres"] = 777;

    VarTables t = tables();
    Bank b(t);
    b.init();
    b.set_store(&store);
    REQUIRE(b.var(b.find("total_litres")).live_quality() == Q_NO_DATA);

    b.load_retained();

    CHECK(b.var(b.find("total_litres")).live_valid());
    CHECK(b.var(b.find("total_litres")).i() == 777);
}

TEST_CASE("retain: what is saved is what the program computed, not a force") {
    // A maintainer forcing a totalizer to exercise an alarm must not have that
    // written into the plant's history.
    MemStore store;
    VarTables t = tables();
    Bank a(t);
    a.init();
    a.set_store(&store);

    const VarRef r = a.find("total_litres");
    a.var(r).set_i(500);
    a.override_set(a.var(r), Scalar::U(999999), true);
    REQUIRE(a.var(r).i() == 999999);          // the program's readers see the force

    a.save_retained();

    INFO("the stored value is the live one under the override");
    CHECK(store.retained["total_litres"] == 500u);
}

TEST_CASE("retain: a store that refuses is not silently believed") {
    MemStore store;
    store.refuse = true;

    VarTables t = tables();
    Bank a(t);
    a.init();
    a.set_store(&store);
    a.var(a.find("total_litres")).set_i(7);

    CHECK(a.save_retained() == 0);            // nothing counted as written
    CHECK(store.saves > 0);                   // ...but it was attempted
    CHECK(store.retained.empty());
}

TEST_CASE("retain: with no store at all, nothing pretends to persist") {
    VarTables t = tables();
    Bank a(t);
    a.init();
    CHECK(a.save_retained() == 0);
    CHECK(a.load_retained() == 0);
}
