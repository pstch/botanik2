"""Alarms page: both views render their table chrome, and SPACE cycles them.

Structure-only. BOTANIK_NO_LOAD_ALARMS (see conftest) pins the *platform*
alarms, but the site conditions trip on their own under the dilated time scale,
so which rows are listed - and in which order - is timing-dependent in RECENT
just as it is in ALL (the ALL view floats active alarms to the top of a
scrollable window). There is no stable row set to pin, so assert the chrome,
which is scale-invariant; the momentary alarm state is what the page exists to
show live, and the table's row rendering is covered by tests/test_alarms_render.
"""

import keys
from helpers import boot, goto

TABLE = "| CODE     | VARIABLE"


def test_alarms_views(spawn):
    term = boot(spawn)
    goto(term, keys.ALT_4)          # switch to ALARMS (page 4), RECENT by default
    # The legend is a fixed-column line: the neighbouring views either side of the
    # [mode] badge, then the position counter every table now carries.
    assert term.wait_for((TABLE, "[recent]", "> all", "active")), \
        "RECENT view did not render its table chrome"
    term.send(keys.SPACE)           # RECENT -> ALL (lists the full catalog)
    assert term.wait_for(("[all]", "> active")), "SPACE did not cycle RECENT -> ALL"
