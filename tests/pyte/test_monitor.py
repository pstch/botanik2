"""MONITOR page navigation. Its layout is pinned by test_pages.py (page_monitor).

Own sim per test: entering a group view is page state the next test would inherit.
"""

import re

import keys
from helpers import boot, goto


def _group(term):
    """The group name in the footer's [brackets], or None."""
    m = re.search(r"\[([a-z_]+)\]", term.snapshot())
    return m.group(1) if m else None


def test_monitor_scrolls_to_the_end(spawn):
    """END jumps the selection to the last variable, scrolling the window to it."""
    term = boot(spawn)
    goto(term, keys.ALT_6, "SCOPE")
    for _ in range(3):
        term.send(keys.DOWN)
    term.send(keys.END_KEY)
    # Rows follow the variable table's own order, so the last one is the last
    # actuator.
    assert term.wait_for("irr_tray8"), "did not scroll to the last group"


def test_monitor_group_modes(spawn):
    """SPACE / LEFT / BACKSPACE cycle the group view (scroll_table.h)."""
    term = boot(spawn)
    goto(term, keys.ALT_6, "SCOPE")
    # WHICH group comes first is not this test's business - the groups are the
    # name prefixes of whatever the site and the library publish, so the first
    # moves whenever either does. Read the group entered rather than predict it.
    assert term.wait_for("[all]"), "the monitor did not open on the all-groups view"

    term.send(keys.SPACE)
    term.settle()
    entered = _group(term)
    assert entered not in (None, "all"), "SPACE did not enter a group"

    term.send(keys.LEFT)
    assert term.wait_for("[all]"), "LEFT did not return to the all-groups view"

    # BACKSPACE is the way back to the whole set without cycling every group.
    term.send(keys.SPACE)
    term.settle()
    assert _group(term) == entered, "SPACE did not enter a group again"
    term.send(keys.BACKSPACE)
    assert term.wait_for("[all]"), "BACKSPACE did not reset to the all-groups view"
