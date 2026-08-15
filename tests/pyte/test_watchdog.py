"""The watchdog actually resets a unit that stops kicking it.

Off in every other pyte file (the clock is manual, so a case that stops running
beats would look wedged); this one turns it on with a short window and asserts
the thing it exists for.
"""
import keys
from helpers import shell


def _uptime(term):
    """Seconds off the status bar's HH:MM:SS clock, or None before it paints."""
    for row in term.screen.display:
        for word in row.split():
            if word.count(":") == 2 and word.replace(":", "").isdigit():
                h, m, s = (int(x) for x in word.split(":"))
                return h * 3600 + m * 60 + s
    return None


def test_a_unit_that_stops_kicking_is_reset(spawn):
    # 300 ms of window: `reboot` drops control and withholds the kick, and the
    # servicing thread notices from OUTSIDE the loop it is watching.
    term = shell(spawn, quiet_logs=True, watchdog=300)
    term.until(lambda: _uptime(term) is not None)
    term.run(200)
    before = _uptime(term)
    assert before is not None and before > 0, "the clock never advanced"

    term.send(b"reboot" + keys.ENTER)
    # Past the window, in beats: the elapsed check reads the same virtual clock
    # the beats drive.
    term.run(200)

    assert term.until(lambda: (_uptime(term) or 99999) < before), \
        "the unit kept running after the kick was withheld\n%s" % term.snapshot()
