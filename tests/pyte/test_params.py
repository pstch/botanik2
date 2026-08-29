"""PARAMS page behaviour.

Own sim per test: an edit here commits to the parameter store.

Cursor arithmetic below counts rows in the ROLE_PARAM block, which the library's
parameters open (its own go last within its block, which places them first):
  0 monitor_baud  1 monitor_log_level
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


def test_filter_narrows_the_table(spawn):
    """Typing filters the rows by name, and ESC gives the rest back.

    The badge is asserted as well as the rows: it is the only acknowledgement of
    a keystroke that changes nothing else on a table with no match.
    """
    term = _params(spawn)
    assert term.wait_screen("monitor_baud"), "the unfiltered table is missing its first row"

    term.send(keys.text("ntp"))
    assert term.wait_screen("[/ntp]"), "the legend badge did not show the filter"
    assert term.wait_screen("ntp_poll_sec"), "a matching row went missing"
    assert term.wait_screen("monitor_baud", present=False), "a non-matching row survived"

    term.send(keys.BACKSPACE)
    assert term.wait_screen("[/nt]"), "BACKSPACE did not pop a character"

    term.send(keys.ESC)
    assert term.wait_screen("monitor_baud"), "ESC did not clear the filter"


def test_filter_and_edit_compose(spawn):
    """An edit opened from a filtered table commits to the row it names.

    _param_at() maps a DISPLAY row to a parameter index, and the filtered map is
    not the identity one - so an edit on a narrowed table is where an off-by-one
    would land on the wrong parameter.
    """
    term = _params(spawn)
    term.send(keys.text("ntp_poll"))
    assert term.wait_screen("ntp_poll_sec"), "the filter did not find the row"

    term.send(keys.ENTER)
    assert term.wait_for("ntp_poll_sec ="), "inline edit did not open on the filtered row"
    term.send(keys.CTRL_U + b"90" + keys.ENTER)
    assert term.wait_screen("90"), "the committed value did not reach the row"
