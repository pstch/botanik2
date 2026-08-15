"""The kill ring is console-wide: what one page cuts, another pastes.

A per-page clipboard would pass every single-page test in the suite and still be
useless: the reason to copy a variable name off a table is to type a command with
it. The claim worth pinning is the crossing itself.
"""

import keys
from helpers import boot, goto

# The Alt+W badge is a THREE-SECOND affordance held in uptime - 300 beats, and
# the clock only moves when this harness says so, so it cannot expire while a
# read is in flight. It can still be caught HALF-PAINTED under the byte budget,
# which is what _copied_name() below waits out.


def _badge_name(term):
    """The name in the mode line's Alt+W badge, or None if it is not showing.

    None also covers "showing, but not painted yet": under the per-tick byte
    budget the badge arrives progressively. This is polled, so a half-painted row
    must read as "not yet" and must not raise, or the caller never retries.
    """
    for row in term.screen.display:
        if "copied " in row:
            rest = row.split("copied ", 1)[1].split()
            return rest[0] if rest else None
    return None


def _copied_name(term):
    """Wait for a COMPLETE badge, then read the name out of it.

    Not wait_screen("copied "): the byte budget can deliver that prefix a frame
    or more before the name, leaving nothing to read. Nor is the first frame
    carrying a name enough - it arrives a few characters at a time, so a single
    read returns "sys_fi" as readily as "sys_fieldbus_dead", and the assertions
    here are substring matches that would accept it. So the wait is for a name
    that has stopped growing, two consecutive pumps agreeing.
    """
    prev = [None]

    def settled():
        name = _badge_name(term)
        if name is None:
            prev[0] = None
            return False
        was, prev[0] = prev[0], name
        return was == name

    assert term.until(settled), "Alt+W did not report a copy"
    return _badge_name(term)


def test_alt_w_on_monitor_yanks_into_the_shell(spawn):
    """Alt+W copies the selected row's name; ^Y at the prompt pastes it."""
    term = goto(boot(spawn), keys.ALT_6)

    # Whichever row the cursor starts on: the test does not care which, only
    # that the badge names it and the same text arrives at the prompt.
    term.send(keys.ALT_W)
    name = _copied_name(term)

    goto(term, keys.ALT_2)
    term.send(keys.text("get ") + keys.CTRL_Y)
    assert term.wait_screen("get " + name), \
        f"the name copied on MONITOR ({name!r}) did not reach the prompt"
