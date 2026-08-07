"""Home page behaviour. Its layout is pinned by test_pages.py (page_home)."""

import keys
from helpers import boot, goto

# Rows 16+ are a live log tail the fast greenhouse floods; the widget rows are
# the comparable part. The same clip test_pages.py uses, against the same golden.
HOME_WIDGET = (0, 16)


def test_home_after_reset(spawn, assert_screen):
    """`reset` re-execs the firmware and it reboots to Home.

    HAL::reset() execve's /proc/self/exe and the PTY fds survive, so the screen
    clears and redraws to a freshly booted Home (main.cpp always switch_to(0)).
    Asserting the SAME golden as the cold boot is the claim: not that Home
    renders, but that a reboot is indistinguishable from a cold start.
    """
    term = boot(spawn)
    goto(term, keys.ALT_2)               # SHELL
    term.send(b"reset" + keys.ENTER)
    assert_screen(term, "page_home", rows=HOME_WIDGET)
