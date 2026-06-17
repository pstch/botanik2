"""Home page screens."""

import keys
from helpers import boot, goto


def test_home_on_boot(spawn, assert_screen):
    """The landing screen after boot (volatile fields normalized)."""
    term = boot(spawn)
    assert_screen(term, "home_boot")


def test_home_after_delay(spawn, assert_screen):
    """Home stays correct across a refresh cycle; only volatile fields move."""
    term = boot(spawn)
    # Let at least one HOME_REFRESH_MS (5s sim -> 0.5s wall at scale 10) elapse.
    term.settle(quiet=0.6, timeout=3.0)
    assert_screen(term, "home_after_delay")


def test_home_after_reset(spawn, assert_screen):
    """`reset` typed in the shell re-execs the firmware; it reboots to Home.

    HAL::reset() execve's /proc/self/exe and the PTY fds survive, so we observe
    the screen clear and redraw to the freshly-booted Home (main.cpp always
    switch_to(0) on boot).
    """
    term = boot(spawn)
    goto(term, keys.ALT_2)               # SHELL
    term.send(keys.text("reset"))
    term.send(keys.ENTER)
    term.settle(timeout=6.0)             # allow for the re-exec + reboot redraw
    assert_screen(term, "home_after_reset")
