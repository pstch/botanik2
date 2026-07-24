"""CONFIG page: renders the parameter table and edits a value inline.

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
    assert term.wait_for("ntp_poll_s"), "config parameter rows missing"


def test_config_edit_roundtrip(spawn):
    term = boot(spawn)
    goto(term, ALT_6)
    assert term.wait_for("ntp_poll_s")
    # Move the cursor down to ntp_poll_s (library index 7), open the inline edit,
    # clear it, type a new value, and commit.
    for _ in range(7):
        term.send(keys.DOWN)
    term.send(keys.ENTER)
    assert term.wait_for("ntp_poll_s ="), "edit prompt did not open"
    term.send(CTRL_U)
    term.send(b"120")
    term.send(keys.ENTER)
    # The edit round-trips through the PLC write-back and repaints the row.
    assert term.wait_for("120"), "edited value did not round-trip"
