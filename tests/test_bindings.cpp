/**
 * @file test_bindings.cpp
 * @brief ConfigBinder: when a component is handed its configuration.
 *
 * The rules a component depends on and cannot check for itself, so they are
 * asserted against a target that only counts pushes.
 */

#include <cstdint>

#include "doctest.h"
#include "libbotanik/vars/bindings.h"
#include "libbotanik/vars/declare.h"
#include "libbotanik/vars/model.h"
#include "libbotanik/vars/params.h"

namespace bindings {

using namespace Botanik;

namespace {

    struct Target {
        uint32_t pushes    = 0;
        uint32_t announced = 0;
        int32_t  value     = -1;
    };

    void apply_target(void* t, const Vars::Bank& io, bool announce) {
        Target& tgt = *static_cast<Target*>(t);
        tgt.pushes++;
        if (announce) tgt.announced++;
        tgt.value = (int32_t)io.var("grp_value").i();
    }

    /** @brief A staged group and an unstaged row, in one small bank. */
    struct Fixture {
        Vars::Bank         bank;
        Vars::ConfigBinder binder;
        Target             staged;
        Target             loose;

        Fixture() : binder(Botanik::SetupContext(), &bank) {
            using namespace Botanik::Vars;
            static const Descriptor v[] = {
                P("grp_serial", T_U32, 1, 0, 9999, 0, "", 0, "", ""),
                P("grp_value",  T_I32, 7, 0, 9999, 0, "", 1, "", ""),
            };
            bank.register_vars(v, 2);
            bank.freeze();
            binder.add("grp",   &staged, &apply_target);
            binder.add(nullptr, &loose,  &apply_target);
        }

        void set(const char* name, int32_t v) {
            bank.var(bank.find(name)).set((float)v);
        }
    };

}  // namespace

TEST_CASE("binder: the first push is unconditional and does not announce") {
    Fixture f;
    CHECK(f.staged.pushes == 0);          // nothing before the first pass

    f.binder.apply_all();
    CHECK(f.staged.pushes == 1);
    CHECK(f.staged.value == 7);           // the stored value, not the struct default
    // A first push is the store arriving, not an operator changing something.
    CHECK(f.staged.announced == 0);
}

TEST_CASE("binder: a staged group waits for its serial") {
    Fixture f;
    f.binder.apply_all();

    // The edit alone must not reach the component: that is what staging IS.
    f.set("grp_value", 42);
    f.binder.apply_all();
    CHECK(f.staged.pushes == 1);
    CHECK(f.staged.value == 7);

    f.set("grp_serial", 2);
    f.binder.apply_all();
    CHECK(f.staged.pushes == 2);
    CHECK(f.staged.value == 42);
    CHECK(f.staged.announced == 1);       // a real change, so it is announced

    f.binder.apply_all();                 // serial unchanged: nothing more
    CHECK(f.staged.pushes == 2);
}

TEST_CASE("binder: an unstaged binding is pushed every pass") {
    Fixture f;
    f.binder.apply_all();
    f.binder.apply_all();
    f.binder.apply_all();
    CHECK(f.loose.pushes == 3);
    // No serial to wait for, so an edit lands on the next pass.
    f.set("grp_value", 99);
    f.binder.apply_all();
    CHECK(f.loose.value == 99);
}

TEST_CASE("binder: a group with no rows is never pushed at all") {
    // A site that does not configure a component leaves it at its struct
    // defaults; group_serial answers 0, and 0 is not a value to push on.
    Vars::Bank bank;
    Vars::ConfigBinder binder(Botanik::SetupContext(), &bank);
    Target absent;
    bank.freeze();
    binder.add("nosuch", &absent, &apply_target);

    binder.apply_all();
    binder.apply_all();
    CHECK(absent.pushes == 0);
}

TEST_CASE("binder: a binder with no bank pushes nothing") {
    Vars::ConfigBinder binder(Botanik::SetupContext(), nullptr);
    Target t;
    binder.add("grp", &t, &apply_target);
    binder.apply_all();
    CHECK(t.pushes == 0);
}

}  // namespace bindings
