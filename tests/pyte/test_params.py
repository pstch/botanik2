"""CONFIG page: the parameter table, the selected-row detail panel, and an
inline value edit.

Content checks (not a full-screen golden): the values come from the live
parameter store and an edit round-trips through it, so a content check is the
scale-invariant assertion here (cf. test_logs.py).
"""

import keys
from helpers import boot, goto

ALT_6 = keys.ESC + b"5"      # CONFIG is page index 4 (ALT_5)
CTRL_U = b"\x15"             # clear the edit line


def test_config_detail_and_edit(spawn):
    """One boot covers the table, the detail panel and the edit round-trip: the
    latter two can only be asserted on a rendered table anyway."""
    term = boot(spawn)
    goto(term, ALT_6)
    assert term.wait_for("PARAMETER"), "config table header missing"
    assert term.wait_for("DEFAULT"), "default column header missing"
    assert term.wait_for("ntp_poll_sec"), "config parameter rows missing"
    # Move the cursor down to ntp_poll_sec (table index 9: net_serial, net_dhcp, net_ip,
    # net_mask, net_dns1, net_dns2, net_gw, ntp_serial, ntp_enable, ntp_poll_sec);
    # the detail panel tracks the selection.
    for _ in range(9):
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
    """The panel's Notes row, and that an authored note actually reaches it."""
    term = boot(spawn)
    goto(term, ALT_6, "PARAMETER")
    assert term.wait_for("Notes"), "panel has no Notes row"
    # net_dhcp carries one; the row below Description is where it lands.
    term.send(keys.DOWN)
    assert term.wait_for("static net_ip/mask/gw rows are ignored"), \
        "an authored note did not reach the panel"
