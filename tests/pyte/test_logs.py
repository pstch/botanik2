"""Logs page: manual MARK annotations via SPACE."""

import keys
from helpers import boot, goto


def test_logs_marks(spawn):
    """SPACE emits a `--- MARK ---` line into the log tail (LogsPage::_emit_mark).

    Asserts the behaviour (a MARK appears after SPACE), not a full-screen golden:
    the logs page is nothing but the live scrollback, which under high
    BOTANIK_TIME_SCALE the fast greenhouse floods with alarm transitions, scrolling
    any pinned lines away. A content check is scale-invariant where a golden isn't.
    """
    term = boot(spawn)
    goto(term, keys.ALT_3)               # LOGS (page index 2)
    term.send(keys.SPACE)
    assert term.wait_for("--- MARK ---"), "SPACE did not emit a MARK line"
