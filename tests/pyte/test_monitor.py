"""MONITOR page: renders the PLC status-variable table and scrolls.

Content checks (not a full-screen golden): the values are live PLC telemetry the
emulator animates, so a content check is the scale-invariant assertion here
(cf. test_config.py).
"""

import keys
from helpers import boot, goto

ALT_7 = keys.ESC + b"7"      # MONITOR is page index 6


def test_monitor_renders(spawn):
    term = boot(spawn)
    goto(term, ALT_7)
    assert term.wait_for("GROUP"), "monitor table header missing"
    assert term.wait_for("ac_supply"), "first group missing"
    assert term.wait_for("grid_v"), "status variable rows missing"


def test_monitor_scrolls(spawn):
    term = boot(spawn)
    goto(term, ALT_7)
    assert term.wait_for("grid_v")
    # Scroll to the bottom of the table; a late-group variable should appear.
    for _ in range(30):
        term.send(keys.DOWN)
    assert term.wait_for("sunset_min"), "did not scroll to the last group"
