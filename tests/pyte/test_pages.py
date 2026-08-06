"""One settled-screen golden per page in the tab order.

This is the suite that holds the per-page invariant: every page
BotanikFirmware::register_pages() registers is visited here, and its settled
screen compared against a committed golden. Adding a page means adding a row to
PAGES and running `make -C build pytest-update`; a page with no row here has no
display test, which is the thing this file exists to prevent.

What a golden is for, and what it is not
----------------------------------------
It pins LAYOUT: which rows a page draws, in what order, with what labels,
columns, rules and chrome. It does not pin VALUES - normalize.py masks every
cell the greenhouse moves (readings, clock, uptime, load, alarm state), because
those are what the page exists to show live and what made the earlier
generation of goldens flaky. So a column that moves, a header that is renamed,
a panel that stops being drawn all fail here; a warmer afternoon does not.

Behaviour belongs in the per-page files next door (an edit round-trip in
test_params, MARK in test_logs, cycling in test_alarms). A golden cannot express
"pressing this changed that", and those files no longer restate the chrome this
one covers.

One sim, not one per page: the boot handshake is the expensive part of a screen
test, and paying it six times would make the invariant the most expensive thing
in the gate. Nothing here mutates state - it only looks - so the pages cannot
contaminate each other. See spawn_module in conftest.py.
"""

import re

import pytest

import keys
from helpers import boot, goto

# (page key, golden name, marker, rows, setup)
#
#   marker : a string the page paints, to sync on instead of settle()'s quiet
#            window. Every page here has a live band of some kind, so the
#            normalized screen never goes quiet and settle() would just burn its
#            timeout. The status bar renders the active tab as "N:NAME", which
#            is the one marker every page is guaranteed to have.
#   rows   : (start, stop) to clip the comparison, for a page whose lower band
#            is something the golden has no business pinning.
#   setup  : keys to send after the switch, to reach the state worth pinning.
PAGES = [
    # HOME's rows 16+ are the live log tail; the widget block above it is the
    # part with a layout to regress.
    (keys.ALT_1, "page_home", "1:HOME", (0, 16), None),

    # SHELL is the bar, an empty prompt, and the bar - its log tail drops out via
    # drop_log_lines. The editor's own rendering is test_shell_reconcile's job.
    (keys.ALT_2, "page_shell", "2:SHELL", None, None),

    # LOGS is nothing BUT the live scrollback, so drop_log_lines empties its
    # content area and the golden pins the frame alone. That is thin, and
    # deliberately so: there is no way to freeze the log without a knob the
    # firmware does not have. Log content is asserted in test_logs.py instead.
    (keys.ALT_3, "page_logs", "3:LOGS", None, None),

    # ALARMS in RECENT (the default) sorts unacked-active first, then by start
    # time, so both its row set AND their order move with the fast greenhouse.
    # ALL lists the fixed catalog in table order - the only view with a layout
    # that holds still. One SPACE cycles RECENT -> ALL.
    (keys.ALT_4, "page_alarms", "4:ALARMS", None, keys.SPACE),

    # PARAMS is the most stable page in the tree: a fixed table over a private
    # config store (conftest gives each sim its own), so the values shown are
    # the declared defaults. Pinned BEFORE any edit - test_params.py commits one,
    # and does so in its own sim.
    (keys.ALT_5, "page_params", "5:PARAMS", None, None),

    # MONITOR animates every reading in its VALUE column; normalize.py masks that
    # column by geometry, so what is left is the scope/name/unit/override layout.
    (keys.ALT_6, "page_monitor", "6:MONITOR", None, None),

    # TASKS is the scheduler's own table: the task/group/every-ms/phase columns
    # hold still, the mean-us/peak-us ones are host-dependent timing and are
    # masked. The row SET is the registration list, which is the part with a
    # regression to catch.
    (keys.ALT_7, "page_tasks", "7:TASKS", None, None),

    # NOTES is a full-screen editor over the operator's note, persisted under its
    # own storage key - conftest gives each sim a private store, so it opens
    # empty. Frame, status row and an empty buffer is the whole layout.
    (keys.ALT_8, "page_notes", "8:NOTES", None, None),
]


@pytest.fixture(scope="module")
def pages_term(spawn_module):
    """One booted sim, shared by every page in this module."""
    return boot(spawn_module)


@pytest.mark.parametrize(
    "page_key,golden,marker,rows,setup",
    PAGES,
    ids=[p[1] for p in PAGES],
)
def test_page_renders(pages_term, assert_screen, page_key, golden, marker, rows, setup):
    """The page's settled screen matches its golden."""
    goto(pages_term, page_key, marker)
    if setup is not None:
        pages_term.send(setup)
    assert_screen(pages_term, golden, rows=rows)


# The tab strip that opens the status bar: the active page renders as "N:NAME"
# and the others as a bare "N", e.g. " 1 2 3 4 5 6:MONITOR". Anchored at the
# start of the line and stopping at the first token that is not a tab, so the
# wall clock further along the bar ("17:01:10", which also begins with a digit)
# is not mistaken for one.
_TAB_STRIP = re.compile(r"^(?:\s+\d(?![\d:])|\s+\d:[A-Z]+)+")


def test_every_registered_page_has_a_golden(pages_term):
    """The invariant itself: no page in the tab order is missing from PAGES.

    terminal.cpp draws the tab strip from the page list, so the bar IS the
    registration list read back off the running firmware - and it is the one
    place a newly registered page shows up without anyone editing this file. A
    page added to register_pages() with no row in PAGES has no display test, and
    lands here as a tab number nothing visits.

    Only the ACTIVE tab carries its name, so the comparison is on the tab
    numbers; PAGES pins each number's name through its ALT_n key and marker.
    """
    strip = _TAB_STRIP.match(pages_term.screen.display[0])
    assert strip, f"no tab strip in status bar: {pages_term.screen.display[0]!r}"

    shown = {tok[0] for tok in strip.group(0).split()}
    covered = {marker[0] for _, _, marker, _, _ in PAGES}
    assert shown == covered, (
        f"tab strip has pages PAGES does not cover: {sorted(shown - covered)} "
        f"(and covers pages the firmware does not show: {sorted(covered - shown)}). "
        f"Add the page to PAGES with a golden.")
