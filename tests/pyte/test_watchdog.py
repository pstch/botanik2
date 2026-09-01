"""The watchdog actually resets a unit that stops kicking it.

Off in every other pyte file (the clock is manual, so a case that stops running
beats would look wedged); this one turns it on and asserts the thing it exists
for, at the window the firmware actually ships.

**Two clock domains, and that is the point.** The window is measured on the
uptime clock, which is VIRTUAL here - it advances a beat at a time, driven over
the control pipe. Only the servicing thread's poll cadence is wall clock. So the
firmware's whole 10 s window costs about a second of real time, and a change
that made the watchdog wait real seconds would stall this test for the full
window instead of finishing in beats.
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
    term = shell(spawn, quiet_logs=True, watchdog=True)
    term.until(lambda: _uptime(term) is not None)
    term.run(200)
    before = _uptime(term)
    assert before is not None and before > 0, "the clock never advanced"

    # `reboot` moves the LIFECYCLE: the laws stop, sys_shutdown_sec runs so
    # the retained rows reach flash, and only then is the kick withheld. So
    # the beats here cover that hold as well as the watchdog window; the
    # servicing thread notices from OUTSIDE the loop it is watching.
    term.send(b"reboot" + keys.ENTER)
    term.run(10100)                   # STOP_PARK_MS + HW_WATCHDOG_MS, in beats

    assert term.until(lambda: (_uptime(term) or 99999) < before), \
        "the unit kept running after the kick was withheld\n%s" % term.snapshot()
