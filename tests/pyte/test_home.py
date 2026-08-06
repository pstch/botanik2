"""Home page: what a reboot lands on.

The landing screen's own layout is pinned by test_pages.py (golden `page_home`);
what is left here is the behaviour a golden cannot express - that `reset`
re-execs the firmware and comes back to that same screen.
"""

import keys
from helpers import boot, goto


# The home page's lower band (rows 16+) is a live log tail; at high
# BOTANIK_TIME_SCALE the fast greenhouse floods it with alarm transitions that
# scroll the boot log away, so the widget rows (0..15) are the comparable part.
# The same clip test_pages.py uses, against the same golden.
HOME_WIDGET = (0, 16)


def test_home_after_reset(spawn, assert_screen):
    """`reset` typed in the shell re-execs the firmware; it reboots to Home.

    HAL::reset() execve's /proc/self/exe and the PTY fds survive, so we observe
    the screen clear and redraw to the freshly-booted Home (main.cpp always
    switch_to(0) on boot). Asserting it against the *same* golden as the cold
    boot is the actual claim here: not that Home renders, but that a reboot is
    indistinguishable from a cold start.

    Its own sim rather than the shared one - this restarts the firmware.
    """
    term = boot(spawn)
    goto(term, keys.ALT_2)               # SHELL
    term.send(keys.text("reset"))
    term.send(keys.ENTER)
    term.settle(timeout=6.0)             # allow for the re-exec + reboot redraw
    assert_screen(term, "page_home", rows=HOME_WIDGET)
