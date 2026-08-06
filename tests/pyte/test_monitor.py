"""MONITOR page: scrolling and the group views.

The table's layout - the SCOPE/VARIABLE/VALUE/UNIT/OVERRIDE columns, the rules,
the detail panel - is pinned by test_pages.py (golden `page_monitor`), so the
header and first-page row checks that used to open these tests are gone. What
stays is the navigation, which a golden cannot express: that the window scrolls
to reach the end of the table, and that the view cycling changes the row set.
"""

import re

import keys
from helpers import boot, goto


def test_monitor_scrolls_to_the_end(spawn):
    term = boot(spawn)
    goto(term, keys.ALT_6, "SCOPE")
    # A few DOWNs move the selection cursor; END jumps it to the last variable,
    # auto-scrolling the window so a late-group variable comes into view. Rows
    # follow the variable table's own order now that no group table imposes one,
    # so the last of them is the last actuator.
    for _ in range(3):
        term.send(keys.DOWN)
    term.send(keys.END_KEY)
    assert term.wait_for("irr_tray8"), "did not scroll to the last group"


def test_monitor_group_modes(spawn):
    """SPACE / LEFT / RIGHT cycle the group view (scroll_table.h's mode machinery)."""
    term = boot(spawn)
    goto(term, keys.ALT_6, "SCOPE")
    # WHICH group comes first is not this test's business: the groups are the
    # name prefixes of whatever the site publishes, and the library publishes
    # rows of its own, so the first one moves whenever either does. What has to
    # hold is that SPACE leaves "all" for a named group, LEFT comes back, and
    # BACKSPACE resets - so read the group the view actually entered rather than
    # predicting it.
    assert term.wait_for("[all]"), "the monitor did not open on the all-groups view"

    term.send(b" ")                       # -> the first named group
    term.settle()
    entered = _current_group(term)
    assert entered not in (None, "all"), "SPACE did not enter a group"

    term.send(keys.LEFT)                  # back to "all"
    assert term.wait_for("[all]"), "LEFT did not return to the all-groups view"

    # BACKSPACE is the way back to the whole set without cycling every group.
    term.send(b" ")
    term.settle()
    assert _current_group(term) == entered, "SPACE did not enter a group again"
    term.send(keys.BACKSPACE)
    assert term.wait_for("[all]"), "BACKSPACE did not reset to the all-groups view"


def _current_group(term):
    """The group name in the footer's [brackets], or None."""
    m = re.search(r"\[([a-z_]+)\]", term.snapshot())
    return m.group(1) if m else None
