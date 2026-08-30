/** @file  Does matiec's generated C address our bank?
 *
 *  Compiles out/POUS.c as C++ over the shim, binds the generated
 *  DESTRAT_data__ against a real Vars::Bank, and runs the body.
 */
#include <cstdio>
#include "libbotanik/vars/model.h"
#include "libbotanik/vars/declare.h"
#include "accessor.h"
#include "POUS.h"
/* Included, not linked: matiec makes its POUs `static` and has the resource
 * file pull the bodies in. */
#include "POUS.c"

using namespace Botanik;
using namespace Botanik::Vars;

/* The CONFIGURATION's VAR_GLOBAL, as our table declares it. */
static const Descriptor g_rows[] = {
    SV("cli_dTStrat_degC"    , T_F32, 2.0 , 1, "degC", 510, "stratification", ""),
    SV("cli_dTStratLow_degC" , T_F32, 2.0 , 1, "degC", 511, "low rung", ""),
    SV("cli_dTStratMed_degC" , T_F32, 4.0 , 1, "degC", 512, "med rung", ""),
    SV("cli_dTStratHigh_degC", T_F32, 6.0 , 1, "degC", 513, "high rung", ""),
    SV("cli_dTStratHyst_degC", T_F32, 0.5 , 1, "degC", 514, "hysteresis", ""),
    SV("cli_fanLowDem"       , T_F32, 0.3 , 2, ""    , 515, "low demand", ""),
    SV("cli_fanMedDem"       , T_F32, 0.6 , 2, ""    , 516, "med demand", ""),
    SV("cli_fanHighDem"      , T_F32, 1.0 , 2, ""    , 517, "high demand", ""),
    SV("cli_stratDem"        , T_F32, 0.3 , 2, ""    , 520, "destrat demand", ""),
};

/* What the CONFIGURATION's connection list generates. */
static void bind_destrat(DESTRAT_data__& d, Bank& io) {
    d.STRAT    = io.resolve_inout<float>("cli_dTStrat_degC");
    d.LOW      = io.resolve_inout<float>("cli_dTStratLow_degC");
    d.MED      = io.resolve_inout<float>("cli_dTStratMed_degC");
    d.HIGH     = io.resolve_inout<float>("cli_dTStratHigh_degC");
    d.HYST     = io.resolve_inout<float>("cli_dTStratHyst_degC");
    d.FAN_LOW  = io.resolve_inout<float>("cli_fanLowDem");
    d.FAN_MED  = io.resolve_inout<float>("cli_fanMedDem");
    d.FAN_HIGH = io.resolve_inout<float>("cli_fanHighDem");
    d.DEMAND   = io.resolve_inout<float>("cli_stratDem");
    /* The nested FB's own rows are locals: allocated, not named. This is what
     * stage 2 of the plan would hand out. Here they alias the program's. */
    d.BANDS.IN   = d.STRAT;
    d.BANDS.LO   = d.LOW;
    d.BANDS.MED  = d.MED;
    d.BANDS.HI   = d.HIGH;
    d.BANDS.HYST = d.HYST;
    d.BANDS.Q    = io.resolve_inout<uint8_t>("blk_stratQ");
    /* EN/ENO are IMPLICIT on every FB, and its private VAR is here too. Three
     * anonymous rows per instance before the interface is counted - the cost
     * stage 2 of the plan is paying for. */
    d.BANDS.EN   = io.resolve_inout<bool>("blk_stratEN");
    d.BANDS.ENO  = io.resolve_inout<bool>("blk_stratENO");
    d.BANDS.BAND = io.resolve_inout<uint8_t>("blk_stratBand");
}

int main() {
    static Bank io;
    /* What Bank::alloc_local() would hand out, spelled by hand here. */
    static const Descriptor locals[] = {
        SV("blk_stratQ"   , T_U8  , 0, 0, "", 521, "vote", ""),
        SV("blk_stratEN"  , T_BOOL, 1, 0, "", 522, "FB enable", ""),
        SV("blk_stratENO" , T_BOOL, 0, 0, "", 523, "FB enable out", ""),
        SV("blk_stratBand", T_U8  , 0, 0, "", 524, "rung in force", ""),
    };
    io.register_vars(g_rows, (uint16_t)(sizeof g_rows / sizeof g_rows[0]));
    io.register_vars(locals, (uint16_t)(sizeof locals / sizeof locals[0]));
    io.freeze();

    static DESTRAT_data__ d;
    bind_destrat(d, io);

    const VarRef strat = io.find("cli_dTStrat_degC");
    const VarRef dem   = io.find("cli_stratDem");
    int bad = 0;
    struct { float gap; float want; } cases[] = {
        { 7.0f, 1.0f }, { 5.0f, 0.6f }, { 3.0f, 0.3f }, { 0.5f, 0.0f },
    };
    for (auto& c : cases) {
        io.var(strat).publish(from_f32(T_F32, c.gap), 1000);
        DESTRAT_body__(&d);
        const float got = io.var(dem).as_f32();
        const bool ok = (got > c.want - 0.01f) && (got < c.want + 0.01f);
        printf("  gap %4.1f -> demand %4.2f  (want %4.2f) %s\n",
               (double)c.gap, (double)got, (double)c.want, ok ? "ok" : "FAIL");
        if (!ok) bad++;
    }
    printf("%s\n", bad == 0 ? "matiec's body drove our bank." : "MISMATCH");
    return bad != 0;
}
