"""MONITOR page: scrolling and the group views.

The table's layout - the SCOPE/VARIABLE/VALUE/UNIT/OVERRIDE columns, the rules,
the detail panel - is pinned by test_pages.py (golden `page_monitor`), so the
header and first-page row checks that used to open these tests are gone. What
stays is the navigation, which a golden cannot express: that the window scrolls
to reach the end of the table, and that the view cycling changes the row set.
"""

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
    # The footer names the view SPACE would move to, so it is what shows the
    # cycle actually moved. "all" is mode 0, so the first named group is next.
    assert term.wait_for("grid"), "footer did not offer the first group"

    term.send(b" ")                       # -> the grid group
    assert term.wait_for("grid_L1_V"), "group view lost its variables"

    term.send(keys.LEFT)                  # back to "all"
    assert term.wait_for("[all]"), "LEFT did not return to the all-groups view"

    # BACKSPACE is the way back to the whole set without cycling every group.
    term.send(b" ")
    assert term.wait_for("[grid]"), "SPACE did not enter a group"
    term.send(keys.BACKSPACE)
    assert term.wait_for("[all]"), "BACKSPACE did not reset to the all-groups view"
