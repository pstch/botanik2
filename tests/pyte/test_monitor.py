"""MONITOR page: renders the PLC status-variable table and scrolls.

Content checks (not a full-screen golden): the values are live PLC telemetry the
emulator animates, so a content check is the scale-invariant assertion here
(cf. test_config.py).
"""

import keys
from helpers import boot, goto

ALT_7 = keys.ESC + b"6"      # MONITOR is page index 5 (ALT_6)


def test_monitor_renders(spawn):
    term = boot(spawn)
    goto(term, ALT_7)
    assert term.wait_for("GROUP"), "monitor table header missing"
    assert term.wait_for("power"), "first group missing"
    assert term.wait_for("pwr_L1_V"), "status variable rows missing"


def test_monitor_scrolls(spawn):
    term = boot(spawn)
    goto(term, ALT_7)
    assert term.wait_for("pwr_L1_V")
    # A few DOWNs move the selection cursor; END jumps it to the last variable,
    # auto-scrolling the window so a late-group variable comes into view.
    for _ in range(3):
        term.send(keys.DOWN)
    term.send(keys.END_KEY)
    assert term.wait_for("sys_supply_lockout"), "did not scroll to the last group"
