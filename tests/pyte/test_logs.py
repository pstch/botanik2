"""Logs page: manual MARK annotations via SPACE."""

import keys
from helpers import boot, goto


def test_logs_marks(spawn):
    """SPACE emits a `--- MARK ---` line (LogsPage::_emit_mark).

    A content check, not a golden: the page is nothing but the live scrollback,
    which the fast greenhouse floods, scrolling any pinned line away.
    """
    term = boot(spawn)
    goto(term, keys.ALT_3)
    term.send(keys.SPACE)
    assert term.wait_for("--- MARK ---"), "SPACE did not emit a MARK line"
