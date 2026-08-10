"""The kill ring is console-wide: what one page cuts, another pastes.

A per-page clipboard would pass every single-page test in the suite and still
be useless, because the reason to copy a variable name off a table is to type a
command with it. So the claim worth pinning is the crossing itself.
"""

import keys
from helpers import boot, goto

# The Alt+W badge is a THREE-SECOND affordance held in uptime, so the suite's
# default 50x dilation puts it on screen for ~60 ms of wall time - about three
# pump cycles, during which the console is still painting the name into it under
# its byte budget. That is how this file used to read a half-painted row and,
# before the helper below tolerated it, raise IndexError inside a poll().
#
# 5x leaves the badge up for ~600 ms: long enough to paint whole, still eight
# times faster than the plant. Nothing else here is timing-sensitive, so this is
# the one file that buys its determinism back with wall time.
BADGE_SCALE = 5


def _badge_name(term):
    """The name in the mode line's Alt+W badge, or None if it is not showing.

    None also covers "showing, but not painted yet". The console emits under a
    per-tick byte budget, so the badge arrives progressively: the row reads
    "copied " with nothing after it, then a few characters of the name, then the
    whole of it. This is polled, so a half-painted row is "not yet" - it must
    not raise, or the retry the caller is built around never happens.
    """
    for row in term.screen.display:
        if "copied " in row:
            rest = row.split("copied ", 1)[1].split()
            return rest[0] if rest else None
    return None


def _copied_name(term):
    """Wait for a COMPLETE badge, then read the name out of it.

    Not wait_screen("copied "): that fires on the prefix, which the byte budget
    can deliver a frame or more before the name behind it, and the read then
    finds nothing to return. Waiting on the parsed name is the same wait done
    against the thing actually being read.

    Nor is the first frame that carries a name enough: the name itself arrives a
    few characters at a time, so a single read returns "sys_fi" as readily as
    "sys_fieldbus_dead" - and every assertion here is a substring match, which
    would accept it. The wait is therefore for a name that has stopped growing,
    two consecutive pumps agreeing, which is the only observation available that
    says the repaint finished.
    """
    prev = [None]

    def settled():
        name = _badge_name(term)
        if name is None:
            prev[0] = None
            return False
        was, prev[0] = prev[0], name
        return was == name

    assert term.poll(settled), "Alt+W did not report a copy"
    return _badge_name(term)


def test_alt_w_on_monitor_yanks_into_the_shell(spawn):
    """Alt+W copies the selected row's name; ^Y at the prompt pastes it."""
    term = goto(boot(spawn, time_scale=BADGE_SCALE), keys.ALT_6)

    # Whichever row the cursor starts on: the test does not care which, only
    # that the badge names it and the same text arrives at the prompt.
    term.send(keys.ALT_W)
    name = _copied_name(term)

    goto(term, keys.ALT_2)
    term.send(keys.text("get ") + keys.CTRL_Y)
    assert term.wait_screen("get " + name), \
        f"the name copied on MONITOR ({name!r}) did not reach the prompt"


def test_alt_w_copies_the_row_the_cursor_is_on(spawn):
    """Move the selection first and it is the new row that gets copied - the
    badge is read off _selected, not off the top of the table."""
    term = goto(boot(spawn, time_scale=BADGE_SCALE), keys.ALT_6)
    term.send(keys.ALT_W)
    first = _copied_name(term)

    term.send(keys.DOWN)
    term.send(keys.ALT_W)
    assert term.poll(lambda: _badge_name(term) not in (None, first)), \
        f"the badge still names {first!r} after moving the cursor"
