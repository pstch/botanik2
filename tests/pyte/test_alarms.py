"""Alarms page: the diffed table renders visually like a full redraw.

Both views are structure-only. BOTANIK_NO_LOAD_ALARMS (see conftest) pins the
*platform* alarms, but the site conditions trip on their own under the dilated
time scale, so which rows are listed - and in which order - is timing-dependent
in RECENT just as it is in ALL. Assert the table chrome, which is
scale-invariant, rather than a momentary alarm set.
"""

import keys
from helpers import boot, goto


def test_alarms_recent(spawn):
    """RECENT (default) view renders the alarm table."""
    term = boot(spawn)
    goto(term, keys.ALT_4)          # switch to ALARMS (page 4), RECENT by default
    assert term.wait_for(("| CODE     | ON | LAST EVENT", "listed", "SPACE: all")), \
        "RECENT view did not render its table chrome"


def test_alarms_all(spawn):
    """ALL view (SPACE cycles RECENT -> ALL) renders the alarm-catalog table.

    Structure-only: the ALL view floats *active* alarms to the top of a scrollable
    window, so under high BOTANIK_TIME_SCALE the fast greenhouse churns which rows
    are visible, their order, the ON column and the active count - there is no
    stable subset to pin against a golden. So assert the table chrome renders (the
    column header and the footer mode line), which is scale-invariant, rather than
    the momentary alarm state (that is what the alarms page exists to show live).
    """
    term = boot(spawn)
    goto(term, keys.ALT_4)
    term.send(b" ")                 # SPACE: RECENT -> ALL (lists the full catalog)
    term.settle()
    assert term.wait_for(("| CODE     | ON | LAST EVENT", "listed", "SPACE: active")), \
        "ALL view did not render its table chrome"
