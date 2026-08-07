"""PARAMS page behaviour. Its layout is pinned by test_pages.py (page_params).

Own sim per test: an edit here commits to the parameter store, and page_params is
taken against the declared defaults.

Cursor arithmetic below counts rows in the ROLE_PARAM block, which the library's
parameters open (its own go last within its block, which places them first):
  0 serial_console_baud  1 serial_monitor_baud
  2 net_serial  3 net_dhcp  4 net_ip  5 net_mask  6 net_gw  7 net_dns1  8 net_dns2
  9 ntp_serial  10 ntp_enable  11 ntp_poll_sec
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
    _down(term, 11)                                  # ntp_poll_sec
    assert term.wait_for("NTP poll interval"), "detail panel did not follow selection"

    # ENTER opens the inline editor on the bottom row. An edit only STAGES the
    # field; it commits when the group's serial bumps.
    term.send(keys.ENTER)
    assert term.wait_for("ntp_poll_sec ="), "inline edit did not open"
    term.send(keys.CTRL_U + b"120" + keys.ENTER)

    for _ in range(2):                               # up to ntp_serial (index 9)
        term.send(keys.UP)
    term.send(keys.ENTER)
    assert term.wait_for("ntp_serial ="), "inline edit did not open on the serial"
    term.send(keys.CTRL_U + b"2" + keys.ENTER)
    assert term.wait_for("120"), "committed value did not round-trip"


def test_notes_row(spawn):
    """An authored note reaches the panel's Notes row (net_dhcp carries one)."""
    term = _params(spawn)
    _down(term, 3)
    assert term.wait_for("ignores the static ip/mask/gw rows"), \
        "an authored note did not reach the panel"


def test_group_follows_selection(spawn):
    """The legend's (group) tracks the cursor.

    It did not: _group_of() returned a pointer into one shared static, and
    drawing the legend re-entered it to name the neighbouring views, so every row
    showed whichever group came last in the table.
    """
    term = _params(spawn)
    assert term.wait_for("(serial)"), "first row is a library serial_* parameter"
    _down(term, 2)
    assert term.wait_for("(net)"), "group did not follow the cursor into net_*"
    _down(term, 7)
    assert term.wait_for("(ntp)"), "group did not follow the cursor into ntp_*"
