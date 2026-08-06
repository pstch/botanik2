"""PARAMS page: the detail panel tracks the cursor, and an edit round-trips.

The table's layout - columns, rules, the detail panel's rows - is pinned by
test_pages.py (golden `page_params`), so the header checks that used to open
these tests are gone. What stays is behaviour a golden cannot express: that the
panel follows the selection, that an edit commits through the store, and that
the legend's (group) tracks the cursor.

These keep their own sims: an edit here commits to the parameter store, and the
golden in test_pages.py is taken against the declared defaults.
"""

import keys
from helpers import boot, goto

CTRL_U = b"\x15"             # clear the edit line


def test_config_detail_and_edit(spawn):
    """The detail panel follows the selection, and an edit round-trips."""
    term = boot(spawn)
    goto(term, keys.ALT_5, "PARAMETER")
    # Move the cursor down to ntp_poll_sec (table index 4: net_serial, net_dhcp,
    # ntp_serial, ntp_enable, ntp_poll_sec - the text parameters are their own
    # block at the end now, so they no longer sit between these); the detail
    # panel tracks the selection.
    for _ in range(4):
        term.send(keys.DOWN)
    assert term.wait_for("NTP poll interval"), "detail panel did not follow selection"
    # ENTER opens the inline editor on the bottom row; clear, retype, stage.
    # Editing a field only stages it - it commits when the group's serial bumps.
    term.send(keys.ENTER)
    assert term.wait_for("ntp_poll_sec ="), "inline edit did not open"
    term.send(CTRL_U)
    term.send(b"120")
    term.send(keys.ENTER)
    # Commit: move up to the "ntp_serial" group serial (index 7) and bump it. That
    # ships the staged group, and the group's owner picks it up.
    for _ in range(2):
        term.send(keys.UP)
    term.send(keys.ENTER)
    assert term.wait_for("ntp_serial ="), "inline edit did not open on the serial"
    term.send(CTRL_U)
    term.send(b"2")
    term.send(keys.ENTER)
    # The commit round-trips through the store and repaints the row.
    assert term.wait_for("120"), "committed value did not round-trip"


def test_notes_row(spawn):
    """An authored note actually reaches the panel's Notes row."""
    term = boot(spawn)
    goto(term, keys.ALT_5, "PARAMETER")
    # net_dhcp carries one; the row below Description is where it lands.
    term.send(keys.DOWN)
    assert term.wait_for("ignores the static ip/mask/gw rows"), \
        "an authored note did not reach the panel"


def test_group_follows_selection(spawn):
    """The legend's (group) must track the cursor.

    It did not: _group_of() returned a pointer into one shared static, and
    drawing the legend re-entered it to name the neighbouring views - which
    walks every parameter. The group shown was whichever came last in the
    table, for every row.
    """
    term = boot(spawn)
    goto(term, keys.ALT_5, "PARAMETER")
    assert term.wait_for("(net)"), "first row is a net_* parameter"
    # Two net_* scalars lead the block; the third row is already ntp_*, since
    # the net_* text parameters sit in their own block further down.
    for _ in range(3):
        term.send(keys.DOWN)
    assert term.wait_for("(ntp)"), "group did not follow the cursor into ntp_*"
