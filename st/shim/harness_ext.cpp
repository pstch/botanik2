/** @file  A whole program generated from ST: its bind, its law, its block.
 *
 *  `roof_lockout` declares VAR_EXTERNAL, so matiec emits `__INIT_EXTERNAL`
 *  per row - the generated `bind()` - and no copy in or out. Its `TOF_MS`
 *  instance keeps plain members, exactly as `Plc::Tof` does, so nothing here
 *  allocates a row. There is no hand-written binding table.
 */
#include <cstdio>
#include "libbotanik/vars/model.h"
#include "libbotanik/vars/declare.h"
#include "resolve_ci.h"

static Botanik::Vars::Bank* g_io = 0;
#define BOTANIK_ST_IO (*g_io)

#include "accessor.h"
#include "POUS.h"
#include "POUS.c"

using namespace Botanik;
using namespace Botanik::Vars;

static const Descriptor g_rows[] = {
    SV("cli_rain_mmph"        , T_F32 , 0.0  , 1, "mm/h", 530, "rain rate"    , ""),
    SV("cli_rain_mmph_qlt"    , T_BOOL, 0    , 0, ""    , 531, "rain believed", ""),
    SV("cli_wind_mps"         , T_F32 , 0.0  , 1, "m/s" , 532, "wind mean"    , ""),
    SV("cli_wind_mps_qlt"     , T_BOOL, 0    , 0, ""    , 533, "wind believed", ""),
    SV("cli_windGust_mps"     , T_F32 , 0.0  , 1, "m/s" , 534, "wind gust"    , ""),
    SV("cli_windShutBegin_mps", T_F32 , 12.0 , 1, "m/s" , 535, "taper from"   , ""),
    SV("cli_windShutEnd_mps"  , T_F32 , 20.8 , 1, "m/s" , 536, "shut at"      , ""),
    SV("cli_windShutMax"      , T_F32 , 0.5  , 2, ""    , 537, "taper ceiling", ""),
    SV("cli_rainLockout_sec"  , T_U16 , 900  , 0, "s"   , 538, "rain overrun" , ""),
    SV("sys_supply_lockout"   , T_BOOL, 0    , 0, ""    , 539, "supply lock"  , ""),
    SV("sys_scan_dt_ms"       , T_U32 , 1000 , 0, "ms"  , 540, "scan period"  , ""),
    SV("cli_rainLockout"      , T_BOOL, 0    , 0, ""    , 541, "rain lock"    , ""),
    SV("cli_windLockout"      , T_BOOL, 0    , 0, ""    , 542, "wind lock"    , ""),
    SV("cli_windowLockout"    , T_BOOL, 0    , 0, ""    , 543, "window lock"  , ""),
    SV("cli_windowCap"        , T_F32 , 0.0  , 2, ""    , 544, "opening cap"  , ""),
};

int main() {
    static Bank io;
    g_io = &io;
    io.register_vars(g_rows, (uint16_t)(sizeof g_rows / sizeof g_rows[0]));
    io.freeze();

    static ROOF_LOCKOUT_data__ d;
    ROOF_LOCKOUT_init__(&d, 0);          /* the generated bind() */

    const VarRef rain = io.find("cli_rain_mmph"), rq = io.find("cli_rain_mmph_qlt");
    const VarRef wind = io.find("cli_wind_mps"),  wq = io.find("cli_wind_mps_qlt");
    const VarRef cap  = io.find("cli_windowCap"), lock = io.find("cli_windowLockout");

    struct Case { const char* what; float mm; bool mq; float ms; bool wq; float want; };
    static const Case cases[] = {
        { "believed, calm and dry"    , 0.0f, 1, 0.0f, 1, 1.00f },
        { "believed, raining"         , 0.4f, 1, 0.0f, 1, 0.00f },
        { "believed, taper at 16.4"   , 0.0f, 1, 16.4f, 1, 0.25f },
        { "believed, gale at 22"      , 0.0f, 1, 22.0f, 1, 0.00f },
        { "RAIN STATION DEAD"         , 0.0f, 0, 0.0f, 1, 0.00f },
        { "WIND STATION DEAD"         , 0.0f, 1, 0.0f, 0, 0.00f },
    };
    int bad = 0;
    for (const Case& c : cases) {
        io.var(rain).publish(from_f32(T_F32, c.mm), 1000);
        io.var(wind).publish(from_f32(T_F32, c.ms), 1000);
        io.var(rq).publish(from_bool(T_BOOL, c.mq), 1000);
        io.var(wq).publish(from_bool(T_BOOL, c.wq), 1000);
        d.RAIN_FOR.Q = false;            /* the overrun must not carry between cases */
        ROOF_LOCKOUT_body__(&d);
        const float got = io.var(cap).as_f32();
        const bool ok = got > c.want - 0.01f && got < c.want + 0.01f;
        printf("  %-24s cap %4.2f (want %4.2f) lock=%d %s\n", c.what, (double)got,
               (double)c.want, (int)io.var(lock).as_bool(), ok ? "ok" : "FAIL");
        if (!ok) bad++;
    }
    printf("%s\n", bad == 0 ? "Generated bind, generated law, no binding table."
                            : "MISMATCH");
    return bad != 0;
}
