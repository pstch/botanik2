/** @file  The one law that needs quality: roof_lockout, generated from ST.
 *
 *  Both weather rows declare 0, which reads as fine weather, so a station
 *  that has stopped answering would OPEN the roof. The two _qlt companions
 *  are what shut it, and they are plain BOOLs that ST can read.
 */
#include <cstdio>
#include "libbotanik/vars/model.h"
#include "libbotanik/vars/declare.h"
#include "accessor.h"
#include "POUS.h"
#include "POUS.c"

using namespace Botanik;
using namespace Botanik::Vars;

static const Descriptor g_rows[] = {
    SV("cli_rain_mmph"      , T_F32 , 0.0  , 1, "mm/h", 530, "rain rate"    , ""),
    SV("cli_rain_mmph_qlt"  , T_BOOL, 0    , 0, ""    , 531, "rain believed", ""),
    SV("cli_wind_mps"       , T_F32 , 0.0  , 1, "m/s" , 532, "wind mean"    , ""),
    SV("cli_wind_mps_qlt"   , T_BOOL, 0    , 0, ""    , 533, "wind believed", ""),
    SV("cli_windGust_mps"   , T_F32 , 0.0  , 1, "m/s" , 534, "wind gust"    , ""),
    SV("cli_windShutBegin"  , T_F32 , 12.0 , 1, "m/s" , 535, "taper from"   , ""),
    SV("cli_windShutEnd"    , T_F32 , 20.8 , 1, "m/s" , 536, "shut at"      , ""),
    SV("cli_windShutMax"    , T_F32 , 0.5  , 2, ""    , 537, "taper ceiling", ""),
    SV("cli_rainLockout_sec", T_U16 , 900  , 0, "s"   , 538, "rain overrun" , ""),
    SV("sys_supplyLock"     , T_BOOL, 0    , 0, ""    , 539, "supply lock"  , ""),
    SV("sys_scanDt_ms"      , T_U32 , 1000 , 0, "ms"  , 540, "scan period"  , ""),
    SV("cli_rainLockout"    , T_BOOL, 0    , 0, ""    , 541, "rain lock"    , ""),
    SV("cli_windLockout"    , T_BOOL, 0    , 0, ""    , 542, "wind lock"    , ""),
    SV("cli_windowLockout"  , T_BOOL, 0    , 0, ""    , 543, "window lock"  , ""),
    SV("cli_windowCap"      , T_F32 , 0.0  , 2, ""    , 544, "opening cap"  , ""),
    /* What Bank::alloc_local() would hand out: the FB's implicit EN/ENO and
     * its VAR_OUTPUTs, three rows plus the interface per instance. */
    SV("blk_rainEN"         , T_BOOL, 1    , 0, ""    , 545, "FB enable"    , ""),
    SV("blk_rainENO"        , T_BOOL, 0    , 0, ""    , 546, "FB enable out", ""),
    SV("blk_rainIN"         , T_BOOL, 0    , 0, ""    , 547, "FB in"        , ""),
    SV("blk_rainPT"         , T_U32 , 0    , 0, "ms"  , 548, "FB preset"    , ""),
    SV("blk_rainDt"         , T_U32 , 0    , 0, "ms"  , 549, "FB dt"        , ""),
    SV("blk_rainQ"          , T_BOOL, 0    , 0, ""    , 550, "FB output"    , ""),
    SV("blk_rainET"         , T_U32 , 0    , 0, "ms"  , 551, "FB elapsed"   , ""),
    /* VAR_TEMP is a row too, for now. */
    SV("tmp_roofBlind"      , T_BOOL, 0    , 0, ""    , 552, "temp blind"   , ""),
    SV("tmp_roofW"          , T_F32 , 0.0  , 1, "m/s" , 553, "temp wind"    , ""),
    SV("tmp_roofCeiling"    , T_F32 , 0.0  , 2, ""    , 554, "temp ceiling" , ""),
};

static void bind_roof(ROOF_LOCKOUT_data__& d, Bank& io) {
    d.RAIN_MMPH     = io.resolve_inout<float>("cli_rain_mmph");
    d.RAIN_MMPH_QLT = io.resolve_inout<bool>("cli_rain_mmph_qlt");
    d.WIND_MPS      = io.resolve_inout<float>("cli_wind_mps");
    d.WIND_MPS_QLT  = io.resolve_inout<bool>("cli_wind_mps_qlt");
    d.GUST_MPS      = io.resolve_inout<float>("cli_windGust_mps");
    d.SHUT_BEGIN    = io.resolve_inout<float>("cli_windShutBegin");
    d.SHUT_END      = io.resolve_inout<float>("cli_windShutEnd");
    d.SHUT_MAX      = io.resolve_inout<float>("cli_windShutMax");
    d.RAIN_HOLD_S   = io.resolve_inout<uint16_t>("cli_rainLockout_sec");
    d.SUPPLY_LOCK   = io.resolve_inout<bool>("sys_supplyLock");
    d.DT_MS         = io.resolve_inout<uint32_t>("sys_scanDt_ms");
    d.RAIN_LOCK     = io.resolve_inout<bool>("cli_rainLockout");
    d.WIND_LOCK     = io.resolve_inout<bool>("cli_windLockout");
    d.WINDOW_LOCK   = io.resolve_inout<bool>("cli_windowLockout");
    d.CAP           = io.resolve_inout<float>("cli_windowCap");
    d.RAIN_FOR.EN    = io.resolve_inout<bool>("blk_rainEN");
    d.RAIN_FOR.ENO   = io.resolve_inout<bool>("blk_rainENO");
    d.RAIN_FOR.IN    = io.resolve_inout<bool>("blk_rainIN");
    d.RAIN_FOR.PT    = io.resolve_inout<uint32_t>("blk_rainPT");
    d.RAIN_FOR.DT_MS = io.resolve_inout<uint32_t>("blk_rainDt");
    d.RAIN_FOR.Q     = io.resolve_inout<bool>("blk_rainQ");
    d.RAIN_FOR.ET    = io.resolve_inout<uint32_t>("blk_rainET");
    d.BLIND   = io.resolve_inout<bool>("tmp_roofBlind");
    d.W       = io.resolve_inout<float>("tmp_roofW");
    d.CEILING = io.resolve_inout<float>("tmp_roofCeiling");
}

int main() {
    static Bank io;
    io.register_vars(g_rows, (uint16_t)(sizeof g_rows / sizeof g_rows[0]));
    io.freeze();
    static ROOF_LOCKOUT_data__ d;
    bind_roof(d, io);

    const VarRef rain = io.find("cli_rain_mmph"), rq = io.find("cli_rain_mmph_qlt");
    const VarRef wind = io.find("cli_wind_mps"),  wq = io.find("cli_wind_mps_qlt");
    const VarRef cap  = io.find("cli_windowCap"), lock = io.find("cli_windowLockout");

    struct Case { const char* what; float mm; bool mq; float ms; bool wq; float want; };
    static const Case cases[] = {
        { "believed, calm and dry"    , 0.0f, 1, 0.0f, 1, 1.00f },
        { "believed, raining"         , 0.4f, 1, 0.0f, 1, 0.00f },
        { "believed, taper at 16.4"   , 0.0f, 1, 16.4f, 1, 0.25f },
        { "believed, gale at 22"      , 0.0f, 1, 22.0f, 1, 0.00f },
        /* The point of the whole exercise: rows read fine weather, and the
         * roof must still shut. */
        { "RAIN STATION DEAD"         , 0.0f, 0, 0.0f, 1, 0.00f },
        { "WIND STATION DEAD"         , 0.0f, 1, 0.0f, 0, 0.00f },
    };
    int bad = 0;
    for (const Case& c : cases) {
        io.var(rain).publish(from_f32(T_F32, c.mm), 1000);
        io.var(wind).publish(from_f32(T_F32, c.ms), 1000);
        io.var(rq).publish(from_bool(T_BOOL, c.mq), 1000);
        io.var(wq).publish(from_bool(T_BOOL, c.wq), 1000);
        /* The overrun must not carry between cases. */
        io.var(io.find("blk_rainQ")).publish(from_bool(T_BOOL, false), 1000);
        ROOF_LOCKOUT_body__(&d);
        const float got = io.var(cap).as_f32();
        const bool ok = got > c.want - 0.01f && got < c.want + 0.01f;
        printf("  %-24s cap %4.2f (want %4.2f) lock=%d %s\n", c.what, (double)got,
               (double)c.want, (int)io.var(lock).as_bool(), ok ? "ok" : "FAIL");
        if (!ok) bad++;
    }
    printf("%s\n", bad == 0 ? "The ST law shuts the roof on a dead station."
                            : "MISMATCH");
    return bad != 0;
}
