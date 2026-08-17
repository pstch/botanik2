"""PARAMS page behaviour.

Own sim per test: an edit here commits to the parameter store.

Cursor arithmetic below counts rows in the ROLE_PARAM block, which the library's
parameters open (its own go last within its block, which places them first):
  0 monitor_baud  1 monitor_level
  2 net_serial  3 net_enable  4 net_dhcp  5 net_ip  6 net_mask  7 net_gw
  8 net_dns1  9 net_dns2
  10 ntp_serial  11 ntp_enable  12 ntp_poll_sec
"""

import keys
from helpers import boot, goto


def _params(spawn):
    term = boot(spawn)
    return goto(term, keys.ALT_5, "PARAMETER")


def _down(term, n):
    for _ in range(n):
        term.send(keys.DOWN)


def test_detail_and_edit(spawn):
    """The detail panel follows the selection, and an edit round-trips."""
    term = _params(spawn)
    _down(term, 12)                                  # ntp_poll_sec
    assert term.wait_for("NTP poll interval"), "detail panel did not follow selection"

    # ENTER opens the inline editor on the bottom row. An edit only STAGES the
    # field; it commits when the group's serial bumps.
    term.send(keys.ENTER)
    assert term.wait_for("ntp_poll_sec ="), "inline edit did not open"
    term.send(keys.CTRL_U + b"120" + keys.ENTER)

    for _ in range(2):                               # up to ntp_serial (index 10)
        term.send(keys.UP)
    term.send(keys.ENTER)
    assert term.wait_for("ntp_serial ="), "inline edit did not open on the serial"
    term.send(keys.CTRL_U + b"2" + keys.ENTER)
    assert term.wait_for("120"), "committed value did not round-trip"


def test_group_follows_selection(spawn):
    """The legend's (group) tracks the cursor.

    Guards the defect where _group_of() returns a pointer into one shared static:
    drawing the legend re-enters it to name the neighbouring views, so every row
    shows whichever group came last in the table.
    """
    term = _params(spawn)
    assert term.wait_for("(monitor)"), "first row is the library's monitor_baud"
    _down(term, 2)
    assert term.wait_for("(net)"), "group did not follow the cursor into net_*"
    _down(term, 8)
    assert term.wait_for("(ntp)"), "group did not follow the cursor into ntp_*"
