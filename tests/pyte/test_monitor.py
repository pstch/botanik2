"""MONITOR page: renders the site status-variable table and scrolls.

Content checks (not a full-screen golden): the values are live readings the
simulated field animates, so a content check is the scale-invariant assertion
here (cf. test_params.py).
"""

import keys
from helpers import boot, goto

ALT_7 = keys.ESC + b"6"      # MONITOR is page index 5 (ALT_6)


def test_monitor_renders_and_scrolls(spawn):
    """One boot covers both: scrolling can only be asserted on a rendered table."""
    term = boot(spawn)
    goto(term, ALT_7, "SCOPE")
    assert term.wait_for("SCOPE"), "monitor table header missing"
    assert term.wait_for("pwr_L1_V"), "status variable rows missing"
    # The SCOPE column says where a reading lives, which the group column (now
    # the view) never did.
    assert term.wait_for("mb in"), "scope column did not classify a Modbus input"
    # A few DOWNs move the selection cursor; END jumps it to the last variable,
    # auto-scrolling the window so a late-group variable comes into view.
    for _ in range(3):
        term.send(keys.DOWN)
    term.send(keys.END_KEY)
    assert term.wait_for("sys_supply_lockout"), "did not scroll to the last group"


def test_monitor_group_modes(spawn):
    """SPACE / LEFT / RIGHT cycle the group view (scroll_table.h's mode machinery)."""
    term = boot(spawn)
    goto(term, ALT_7, "SCOPE")
    # The footer names the view SPACE would move to, so it is what shows the
    # cycle actually moved. "all" is mode 0, so the first named group is next.
    assert term.wait_for("power"), "footer did not offer the first group"

    term.send(b" ")                       # -> the power group
    assert term.wait_for("pwr_L1_V"), "group view lost its variables"

    term.send(keys.LEFT)                  # back to "all"
    assert term.wait_for("[all]"), "LEFT did not return to the all-groups view"

    # BACKSPACE is the way back to the whole set without cycling every group.
    term.send(b" ")
    assert term.wait_for("[power]"), "SPACE did not enter a group"
    term.send(keys.BACKSPACE)
    assert term.wait_for("[all]"), "BACKSPACE did not reset to the all-groups view"
