"""Alarms page: the diffed table renders visually like a full redraw.

The suite runs the sim with BOTANIK_NO_LOAD_ALARMS (see conftest), so the only
active platform alarm is SYS_RST (held for the first five minutes after boot) -
the active set is deterministic. The volatile LAST EVENT timestamp normalizes to
<ALARM_TS>; assert_screen polls until the live screen matches the golden.

  - RECENT (default) view: lists the active SYS_RST row only.
  - ALL view (SPACE cycles RECENT -> ALL): lists the full alarm catalog, SYS_RST
    floated to the top and the rest in table order.
"""

import keys
from helpers import boot, goto


def test_alarms_recent(spawn, assert_screen):
    term = boot(spawn)
    goto(term, keys.ALT_5)          # switch to ALARMS (page 4), RECENT by default
    assert_screen(term, "alarms_recent")


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
    goto(term, keys.ALT_5)
    term.send(b" ")                 # SPACE: RECENT -> ALL (lists the full catalog)
    term.settle()
    assert term.wait_for(("| CODE     | ON | LAST EVENT", "listed", "SPACE: active")), \
        "ALL view did not render its table chrome"
