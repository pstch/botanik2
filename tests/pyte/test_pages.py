"""One settled-screen golden per page in the tab order.

The per-page invariant: every page register_pages() registers is visited here and
compared against a committed golden. Adding a page means a row in PAGES and
`make -C build pytest-update`; a page with no row has no display test, which is
what this file exists to prevent.

A golden pins LAYOUT - rows, order, labels, columns, rules, chrome - not values:
normalize.py masks every cell the greenhouse moves. Behaviour belongs in the
per-page files next door; a golden cannot express "pressing this changed that".

One sim for the file: nothing here mutates, so the pages cannot contaminate each
other, and the boot handshake is paid once.
"""

import re

import pytest

import keys
from helpers import boot, goto

# (page key, golden, marker, clipped rows, setup keys)
#
# Every page has a live band of some kind, so the normalized screen never goes
# quiet and settle() would pay its full budget; the marker is the status bar's
# "N:NAME" for the active tab, which every page has.
PAGES = [
    # HOME's rows 16+ are the live log tail; the widget block is the layout.
    (keys.ALT_1, "page_home", "1:HOME", (0, 16), None),
    # SHELL is the bar, an empty prompt, and the bar. The editor's own rendering
    # is test_shell_reconcile's job.
    (keys.ALT_2, "page_shell", "2:SHELL", None, None),
    # LOGS is nothing BUT the live scrollback, which drop_log_lines removes from
    # both sides - so this pins the frame alone. Content: test_logs.py.
    (keys.ALT_3, "page_logs", "3:LOGS", None, None),
    # ALARMS in ALL (one SPACE from the default RECENT): RECENT sorts active-first
    # so its rows and their order move with the greenhouse, while ALL keeps the
    # fixed catalog in table order.
    (keys.ALT_4, "page_alarms", "4:ALARMS", None, keys.SPACE),
    # PARAMS over a private config store, so the values are the declared
    # defaults. Pinned before any edit - test_params commits one, in its own sim.
    (keys.ALT_5, "page_params", "5:PARAMS", None, None),
    # MONITOR animates every reading; normalize.py masks the VALUE column by
    # geometry, leaving the scope/name/unit/override layout.
    (keys.ALT_6, "page_monitor", "6:MONITOR", None, None),
    # TASKS: the task/group/every-ms/phase columns hold still, mean/peak us are
    # host timing and masked. The row SET is the registration list.
    (keys.ALT_7, "page_tasks", "7:TASKS", None, None),
    # NOTES opens empty (private store): frame, status row, empty buffer.
    (keys.ALT_8, "page_notes", "8:NOTES", None, None),
]


@pytest.fixture(scope="module")
def pages_term(spawn_module):
    return boot(spawn_module)


@pytest.mark.parametrize("page_key,golden,marker,rows,setup", PAGES,
                         ids=[p[1] for p in PAGES])
def test_page_renders(pages_term, assert_screen, page_key, golden, marker, rows, setup):
    goto(pages_term, page_key, marker)
    if setup is not None:
        pages_term.send(setup)
    assert_screen(pages_term, golden, rows=rows)


# The status bar's tab strip: the active page renders "N:NAME", the others a bare
# "N". Anchored at the line start and stopping at the first token that is not a
# tab, so the wall clock further along is not mistaken for one.
_TAB_STRIP = re.compile(r"^(?:\s+\d(?![\d:])|\s+\d:[A-Z]+)+")


def test_every_registered_page_has_a_golden(pages_term):
    """No page in the tab order is missing from PAGES.

    terminal.cpp draws the strip from the page list, so the bar IS the
    registration read back off the running firmware - the one place a new page
    shows up without anyone editing this file.
    """
    strip = _TAB_STRIP.match(pages_term.screen.display[0])
    assert strip, f"no tab strip in status bar: {pages_term.screen.display[0]!r}"

    shown = {tok[0] for tok in strip.group(0).split()}
    covered = {marker[0] for _, _, marker, _, _ in PAGES}
    assert shown == covered, (
        f"tab strip has pages PAGES does not cover: {sorted(shown - covered)} "
        f"(and covers pages the firmware does not show: {sorted(covered - shown)}). "
        f"Add the page to PAGES with a golden.")
