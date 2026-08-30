/** @file  A generated program against the REAL site table.
 *
 *  No row table here and no binding table: the rows are the site's own, and
 *  `__INIT_EXTERNAL` resolves each by name. What is left to write is the
 *  cases.
 */
#include <cstdio>
#include "libbotanik_site.h"
#include "libbotanik.h"
#include "site_bank.h"
#include "resolve_ci.h"

static Botanik::Vars::Bank* g_io = 0;
#define BOTANIK_ST_IO (*g_io)

#include "accessor.h"
#include "POUS.h"
#include "POUS.c"

using namespace Botanik;
using namespace Botanik::Vars;

int main() {
    static Bank io;
    g_io = &io;
    TestSite::build(io);            /* the site's own 425 rows */

    static DESTRAT_CONTROLLER_data__ d;
    DESTRAT_CONTROLLER_init__(&d, 0);   /* the generated bind() */

    const VarRef gap = io.find("cli_dTStrat_degC");
    const VarRef dem = io.find("cli_stratDem");
    const float low  = io.var(io.find("cli_fanLowDem")).as_f32();
    const float med  = io.var(io.find("cli_fanMedDem")).as_f32();
    const float high = io.var(io.find("cli_fanHighDem")).as_f32();

    struct Case { const char* what; float gap; float want; };
    const Case cases[] = {
        { "a wide gap takes the top rung", 8.0f,  high },
        { "a rung engages AT its gap"    , 2.0f,  low  },
        { "and holds inside the hyst"    , 1.0f,  low  },
        { "released a hysteresis below"  , -0.1f, 0.0f },
    };
    int bad = 0;
    for (const Case& c : cases) {
        io.var(gap).publish(from_f32(T_F32, c.gap), 1000);
        DESTRAT_CONTROLLER_body__(&d);
        const float got = io.var(dem).as_f32();
        const bool ok = got > c.want - 0.01f && got < c.want + 0.01f;
        printf("  %-32s gap %5.1f -> %4.2f (want %4.2f) %s\n", c.what,
               (double)c.gap, (double)got, (double)c.want, ok ? "ok" : "FAIL");
        if (!ok) bad++;
    }
    (void)med;
    printf("%s\n", bad == 0 ? "Generated ST drove the site's own table."
                            : "MISMATCH");
    return bad != 0;
}
