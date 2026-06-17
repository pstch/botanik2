"""Logs page: manual MARK annotations via SPACE."""

import keys
from helpers import boot, goto


def test_logs_marks(spawn, assert_screen):
    """SPACE emits a `--- MARK ---` line into the log tail (LogsPage::_emit_mark).

    A mark is dropped if the TX buffer isn't empty or a redraw is pending, so we
    settle between presses to let each one land and scroll the tail.
    """
    term = boot(spawn)
    goto(term, keys.ALT_3)               # LOGS (page index 2)
    for _ in range(3):
        term.send(keys.SPACE)
        term.settle()
    assert_screen(term, "logs_marks")
