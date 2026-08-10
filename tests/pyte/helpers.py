"""Boot / page-switch helpers, and the shell-editor screen assertion."""

import keys
from normalize import drop_log_lines, strip_noise

# The status bar renders the ACTIVE tab as "N:NAME" and the rest as bare digits,
# so the label is both the cheapest thing to sync a page switch on and the one
# marker every page is guaranteed to have.
TABS = {keys.ALT_1: "1:HOME", keys.ALT_2: "2:SHELL", keys.ALT_3: "3:LOGS",
        keys.ALT_4: "4:ALARMS", keys.ALT_5: "5:PARAMS", keys.ALT_6: "6:MONITOR",
        keys.ALT_7: "7:TASKS", keys.ALT_8: "8:NOTES"}

_boot_failed = None            # first boot failure in this file, if any


def boot(spawn, **kwargs):
    """Launch a sim and wait for the console to come up.

    Synced on the status bar's tab strip, which is chrome: always on screen, on
    every page, for as long as the firmware lives. Never on a handshake log line
    ("NTP synced" / "MQTT connected") - those are TRANSIENT in a 7-row tail, and
    alarm churn scrolls them past unpainted on a busy console.
    test_page_behaviour asks `net` for that state instead.
    """
    global _boot_failed
    # A firmware that cannot start cannot start for the next test either, and the
    # full wait once per test turns a 4-second suite into a quarter of an hour.
    # Report the first failure and short-circuit: every test still fails.
    if _boot_failed is not None:
        raise AssertionError(f"not retrying: {_boot_failed}")

    term = spawn(**kwargs)
    if not term.wait_screen(TABS[keys.ALT_1]):
        _boot_failed = "console never came up (no status bar)"
        raise AssertionError(f"{_boot_failed}\nscreen was:\n{term.snapshot()}")
    return term


def goto(term, page_key, marker=None):
    """Switch page and wait for the redraw, syncing on `marker` (default: the tab).

    wait_screen, not wait_for: a tab label is only true while that page is
    active, and a latched marker would answer for a visit two tests ago.
    """
    term.send(page_key)
    marker = marker if marker is not None else TABS[page_key]
    assert term.wait_screen(marker), f"page did not render marker {marker!r}"
    return term


def shell(spawn, quiet_logs=False, **kwargs):
    """Boot and switch to the SHELL page, ready for editor input.

    `quiet_logs` runs `logs off` first, which a test asserting on a command's
    REPLY wants: reply and alarm churn compete for the same throttled console,
    and under host load the churn wins, so the reply is never painted.

    Off by default because it puts two lines in the transcript, which a test
    asserting the whole editor screen counts as content.
    """
    term = goto(boot(spawn, **kwargs), keys.ALT_2)
    if quiet_logs:
        term.send(b"logs off" + keys.ENTER)
        assert term.wait_screen("output disabled"), "`logs off` did not report"
    return term


# The SHELL page's chrome once normalize.py has masked the volatile cells. Both
# rows are identical in every reconcile scenario.
SHELL_BAR = " 1 2:SHELL 3 4 5 6 7 8              <TIME>                  cpu:<C> stk:<S>"
ALARM_BAR = " ALARMS :<BAR>"


def assert_editor(term, *rows, **kw):
    """Assert the SHELL screen is the chrome plus exactly `rows` of editor.

    Stands in for a golden file: once the volatile cells are masked and the blank
    and log rows dropped, a reconcile screen is only ever SHELL_BAR + the
    editor's rows + ALARM_BAR, so a 24-line fixture would spend 23 lines
    restating chrome. Polls like assert_screen: under dilation a render can span
    ticks, so a transient partial frame must not fail a correct screen.

    `clip=(a, b)` compares only that half-open band; `alarm_bar=False` drops the
    trailing bar from the expectation when the band excludes it.
    """
    clip = kw.pop("clip", None)
    want = [SHELL_BAR] + list(rows) + ([ALARM_BAR] if kw.pop("alarm_bar", True) else [])
    assert not kw, f"unexpected kwargs {sorted(kw)}"

    def got():
        view = term.view()
        return drop_log_lines(strip_noise(view[clip[0]:clip[1]] if clip else view))

    term.poll(lambda: got() == want)
    assert got() == want, "editor screen mismatch\n  want: %s\n  got:  %s" % (
        "\n        ".join(want), "\n        ".join(got()))
