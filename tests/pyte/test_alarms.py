"""Alarms page: SPACE cycles the filter views.

The ALL view's table layout is pinned by test_pages.py (golden `page_alarms`).
What is left here is the cycling itself, which a golden cannot express: the
badge naming the current view, and the neighbours either side of it, must move
when SPACE is pressed.

Deliberately not asserted: which rows are listed in RECENT. BOTANIK_NO_LOAD_ALARMS
(see conftest) pins the platform alarms, but the site conditions trip on their
own under the dilated time scale, and RECENT orders active-first via _before(),
so both its row set and their order are timing-dependent. ALL is the one view
with a stable order - alarms.h:244 returns before the sort, keeping table order -
which is why it, and not this one, is the view with a golden. Row rendering
itself is covered by tests/test_alarms_render.cpp.
"""

import keys
from helpers import boot, goto


def test_alarms_views_cycle(spawn):
    term = boot(spawn)
    goto(term, keys.ALT_4, "4:ALARMS")   # RECENT by default
    assert term.wait_for(("[recent]", "> all")), "did not land in the RECENT view"
    term.send(keys.SPACE)                # RECENT -> ALL (the full catalog)
    assert term.wait_for(("[all]", "> active")), "SPACE did not cycle RECENT -> ALL"
