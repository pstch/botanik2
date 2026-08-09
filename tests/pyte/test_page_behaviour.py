"""Per-page behaviour a golden cannot express, for the pages that need little.

What pressing something does, for the pages that need little. Kept in one file
because a CTest entry is a whole file and each pays an interpreter startup - a
file per assertion made the startup tax the main cost of these.

Own sim per test: the reset test restarts the firmware, and entering a monitor
group is page state the next test would inherit.
"""

import re

import keys
from helpers import boot, goto, shell

# HOME's rows 16+ are a live log tail the greenhouse floods; the widget rows are
# the comparable part, and the only full-screen golden left in the suite.
HOME_WIDGET = (0, 16)


def test_home_after_reset(spawn, assert_screen):
    """`reset` re-execs the firmware and it reboots to Home.

    HAL::reset() execve's /proc/self/exe and the PTY fds survive, so the screen
    clears and redraws to a freshly booted Home (main.cpp always switch_to(0)).
    Asserting the SAME golden as the cold boot is the claim: not that Home
    renders, but that a reboot is indistinguishable from a cold start.
    """
    term = boot(spawn)
    goto(term, keys.ALT_2)                       # SHELL
    term.send(b"reset" + keys.ENTER)
    assert_screen(term, "page_home", rows=HOME_WIDGET)


def test_network_comes_up(spawn):
    """The DNS -> NTP -> MQTT handshake finishes, per the firmware's own report.

    Asked with `net` rather than watched for in the boot log. The log lines are
    transient - two rows in a 7-row tail that the greenhouse's alarm churn
    scrolls past, and on a busy console they can go unpainted on a firmware whose
    network is perfectly healthy, which is exactly what they were doing here.
    `net` prints current state on demand, so this asks a question that has an
    answer instead of hoping to catch a line going by.
    """
    term = shell(spawn, quiet_logs=True)
    term.send(b"net" + keys.ENTER)
    # The report's LAST row, so the wait covers the whole of it: at 4800 baud a
    # dozen rows take several frames, and syncing on the first one reads a
    # half-painted answer.
    assert term.wait_screen("MQTT broker"), "`net` did not finish printing"
    report = "\n".join(term.screen.display)
    for want in ("online", "synced", "ready"):
        assert want in report, f"network not up: no '{want}' in\n{report}"


def test_logs_marks(spawn):
    """SPACE emits a `--- MARK ---` line (LogsPage::_emit_mark).

    A content check, not a golden: the page is nothing but the live scrollback,
    which the fast greenhouse floods, scrolling any pinned line away.
    """
    term = boot(spawn)
    goto(term, keys.ALT_3)
    term.send(keys.SPACE)
    assert term.wait_for("--- MARK ---"), "SPACE did not emit a MARK line"


def test_monitor_scrolls_to_the_end(spawn):
    """END jumps the selection to the last variable, scrolling the window to it."""
    term = boot(spawn)
    goto(term, keys.ALT_6, "SCOPE")
    for _ in range(3):
        term.send(keys.DOWN)
    term.send(keys.END_KEY)
    # Rows follow the variable table's own order, so the last is the last actuator.
    assert term.wait_screen("irr_tray8"), "did not scroll to the last group"


def test_monitor_group_modes(spawn):
    """SPACE / LEFT / BACKSPACE cycle the group view (scroll_table.h).

    wait_screen throughout: "[all]" is on screen in half these states and gone in
    the other half, which a latched wait_for could not tell apart - it answered
    yes to every one of them from the first view.
    """
    term = boot(spawn)
    goto(term, keys.ALT_6, "SCOPE")
    # WHICH group comes first is not this test's business - the groups are the
    # name prefixes of whatever the site and the library publish, so the first
    # moves whenever either does. Read the group entered rather than predict it.
    assert term.wait_screen("[all]"), "the monitor did not open on the all-groups view"

    # Poll for a settled group name rather than reading one the instant "[all]"
    # goes: the footer repaints a cell at a time, so the first frame without
    # "[all]" can still hold half of the new label.
    term.send(keys.SPACE)
    assert term.poll(lambda: _group(term) not in (None, "all")), \
        "SPACE did not enter a group"
    entered = _group(term)

    term.send(keys.LEFT)
    assert term.wait_screen("[all]"), "LEFT did not return to the all-groups view"

    # BACKSPACE is the way back to the whole set without cycling every group.
    term.send(keys.SPACE)
    assert term.wait_screen("[%s]" % entered), "SPACE did not enter the same group again"
    term.send(keys.BACKSPACE)
    assert term.wait_screen("[all]"), "BACKSPACE did not reset to the all-groups view"


def _group(term):
    """The group name in the footer's [brackets], or None."""
    m = re.search(r"\[([a-z_]+)\]", term.snapshot())
    return m.group(1) if m else None
