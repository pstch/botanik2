"""Home page screens."""

import keys
from helpers import boot, goto


# The home page's lower band (rows 16+) is a live log tail; at high
# BOTANIK_TIME_SCALE the fast greenhouse floods it with alarm transitions that
# scroll the boot log away, so every home test asserts the widget rows (0..15)
# only. The log tail's behaviour is covered scale-invariantly by test_logs_marks.
HOME_WIDGET = (0, 16)


def test_home_on_boot(spawn, assert_screen):
    """The home widget on the landing screen after boot (volatile fields normalized).

    boot() already spans several HOME_REFRESH_MS cycles (5s sim = 0.1s wall at
    scale 50), so this covers the periodic repaint too - no separate delay test.
    """
    term = boot(spawn)
    assert_screen(term, "home_boot", rows=HOME_WIDGET)


def test_home_after_reset(spawn, assert_screen):
    """`reset` typed in the shell re-execs the firmware; it reboots to Home.

    HAL::reset() execve's /proc/self/exe and the PTY fds survive, so we observe
    the screen clear and redraw to the freshly-booted Home (main.cpp always
    switch_to(0) on boot). A reboot lands on the same screen as a cold boot, so
    it asserts against the same golden.
    """
    term = boot(spawn)
    goto(term, keys.ALT_2)               # SHELL
    term.send(keys.text("reset"))
    term.send(keys.ENTER)
    term.settle(timeout=6.0)             # allow for the re-exec + reboot redraw
    assert_screen(term, "home_boot", rows=HOME_WIDGET)
