"""Small helpers shared across the page/reconcile suites."""

import time

import keys
from normalize import drop_log_lines, meaningful_lines


# Final markers of the async network boot handshake (DNS -> NTP -> MQTT). They
# land in the boot log shown on the Home page; reaching both is what "booted"
# means here, so a test acts on a firmware that has finished its handshake
# rather than one still mid-flight.
NET_BOOT_DONE = ("MQTT connected", "NTP synced")


def boot(spawn, **kwargs):
    """Launch a sim, wait for boot (incl. the network handshake) to finish."""
    kwargs.setdefault("watch", NET_BOOT_DONE)  # latch the markers from the first byte
    term = spawn(**kwargs)
    # Content sync on the markers, and nothing else: it is strictly stronger
    # than a quiet window, so the settle() that used to run first was pure
    # latency. Assert it - a marker the sim can no longer produce would
    # otherwise burn wait_for()'s whole timeout in every test and still pass,
    # which is exactly what an unreachable mock broker once did.
    assert term.wait_for(NET_BOOT_DONE), \
        f"network boot never completed; saw {sorted(term._seen)}"
    return term


def goto(term, page_key, marker=None):
    """Switch page and wait for the redraw.

    `marker` syncs on content instead of settle()'s quiet window. Prefer it for
    a page whose content never goes quiet (MONITOR animates every reading): the
    normalized screen there keeps changing, so settle() finds no quiet window
    and always burns its full timeout.
    """
    term.send(page_key)
    if marker is not None:
        assert term.wait_for(marker), f"page did not render marker {marker!r}"
    else:
        term.settle()
    return term


def shell(spawn, **kwargs):
    """Boot and switch to the SHELL page, ready for editor input."""
    term = boot(spawn, **kwargs)
    goto(term, keys.ALT_2)
    return term


# The SHELL page's chrome, as it reads once normalize.py has masked the volatile
# cells. Both rows are identical in every reconcile scenario.
SHELL_BAR = " 1 2:SHELL 3 4 5 6 7 8              <TIME>                  cpu:<C> stk:<S>"
ALARM_BAR = " ALARMS :<BAR>"


def assert_editor(term, *rows, **kw):
    """Assert the SHELL screen is the chrome plus exactly `rows` of editor.

    Stands in for a golden file. Once normalize.py masks the volatile cells and
    meaningful_lines drops the blank and log rows, a reconcile screen is only
    ever SHELL_BAR + the editor's rows + ALARM_BAR - so a 24-line fixture spent
    23 lines restating the same two constants to pin one to three rows of
    payload. Spelling the payload out here puts the expectation in the test,
    where the reader wants it, and takes the goldens' regen churn with it.

    Polls like the assert_screen fixture: under time dilation a render can span
    ticks, so a transient partial frame must not fail a correct screen.

    `clip=(a, b)` compares only that half-open row band (for a screen whose
    lower rows hold something the test isn't about); `alarm_bar=False` drops the
    trailing bar from the expectation when the band excludes it.
    """
    clip = kw.pop("clip", None)
    want = [SHELL_BAR] + list(rows) + ([ALARM_BAR] if kw.pop("alarm_bar", True) else [])
    deadline = time.time() + kw.pop("timeout", 8.0)
    assert not kw, f"unexpected kwargs {sorted(kw)}"
    while True:
        lines = term.screen.display
        got = drop_log_lines(meaningful_lines(lines[clip[0]:clip[1]] if clip else lines))
        if got == want or time.time() >= deadline:
            break
        term._pump(0.02)
    assert got == want, "editor screen mismatch\n  want: %s\n  got:  %s" % (
        "\n        ".join(want), "\n        ".join(got))
