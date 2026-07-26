"""CONFIG page: the parameter table, the selected-row detail panel, and an
inline value edit.

Content checks (not a full-screen golden): the values are PLC-config-driven and
an edit round-trips asynchronously through the emulated PLC, so a content check
is the scale-invariant assertion here (cf. test_logs.py).
"""

import keys
from helpers import boot, goto

ALT_6 = keys.ESC + b"5"      # CONFIG is page index 4 (ALT_5)
CTRL_U = b"\x15"             # clear the edit line


def test_config_page_renders(spawn):
    term = boot(spawn)
    goto(term, ALT_6)
    assert term.wait_for("PARAMETER"), "config table header missing"
    assert term.wait_for("DEFAULT"), "default column header missing"
    assert term.wait_for("ntp_poll_s"), "config parameter rows missing"


def test_config_detail_and_edit(spawn):
    term = boot(spawn)
    goto(term, ALT_6)
    assert term.wait_for("ntp_poll_s")
    # Move the cursor down to ntp_poll_s (library index 7); the detail panel
    # tracks the selection.
    for _ in range(7):
        term.send(keys.DOWN)
    assert term.wait_for("NTP poll interval"), "detail panel did not follow selection"
    # ENTER opens the inline editor on the bottom row; clear, retype, commit.
    term.send(keys.ENTER)
    assert term.wait_for("ntp_poll_s ="), "inline edit did not open"
    term.send(CTRL_U)
    term.send(b"120")
    term.send(keys.ENTER)
    # The edit round-trips through the PLC write-back and repaints the table row.
    assert term.wait_for("120"), "edited value did not round-trip"
