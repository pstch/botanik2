"""Boot / page-switch helpers, and the shell-editor screen assertion."""

import keys
from normalize import drop_log_lines, strip_noise

# Final markers of the async boot handshake (DNS -> NTP -> MQTT). Reaching both
# is what "booted" means here, so a test acts on a firmware that has finished it
# rather than one still mid-flight.
NET_BOOT_DONE = ("MQTT connected", "NTP synced")


def boot(spawn, **kwargs):
    """Launch a sim and wait for boot, network handshake included."""
    kwargs.setdefault("watch", NET_BOOT_DONE)  # latch the markers from the first byte
    term = spawn(**kwargs)
    # Asserted, not merely waited on: a marker the sim can no longer produce
    # would otherwise burn the whole budget in every test and still pass, which
    # is exactly what an unreachable mock broker once did.
    assert term.wait_for(NET_BOOT_DONE), (
        f"network boot never completed; saw {sorted(term._seen)}\n"
        f"screen was:\n{term.snapshot()}")
    return term


# The status bar renders the ACTIVE tab as "N:NAME" and the rest as bare digits,
# so the label is both the cheapest thing to sync a page switch on and the one
# marker every page is guaranteed to have.
TABS = {keys.ALT_1: "1:HOME", keys.ALT_2: "2:SHELL", keys.ALT_3: "3:LOGS",
        keys.ALT_4: "4:ALARMS", keys.ALT_5: "5:PARAMS", keys.ALT_6: "6:MONITOR",
        keys.ALT_7: "7:TASKS", keys.ALT_8: "8:NOTES"}


def goto(term, page_key, marker=None):
    """Switch page and wait for the redraw, syncing on `marker` (default: the tab).

    wait_screen, not wait_for: a tab label is only true while that page is
    active, and a latched marker would answer for a visit two tests ago. Content
    sync rather than settle() throughout - it stops at the first frame that says
    yes, where a quiet window has to wait out the 4800-baud dribble.
    """
    term.send(page_key)
    marker = marker if marker is not None else TABS[page_key]
    assert term.wait_screen(marker), f"page did not render marker {marker!r}"
    return term


def shell(spawn, **kwargs):
    """Boot and switch to the SHELL page, ready for editor input."""
    return goto(boot(spawn, **kwargs), keys.ALT_2)


# The SHELL page's chrome once normalize.py has masked the volatile cells. Both
# rows are identical in every reconcile scenario.
SHELL_BAR = " 1 2:SHELL 3 4 5 6 7 8              <TIME>                  cpu:<C> stk:<S>"
ALARM_BAR = " ALARMS :<BAR>"


def assert_editor(term, *rows, **kw):
    """Assert the SHELL screen is the chrome plus exactly `rows` of editor.

    Stands in for a golden file: once the volatile cells are masked and the blank
    and log rows dropped, a reconcile screen is only ever SHELL_BAR + the
    editor's rows + ALARM_BAR, so a 24-line fixture would spend 23 lines
    restating chrome to pin one to three rows of payload.

    Polls like assert_screen, and on the same terms - under dilation a render can
    span ticks, so a transient partial frame must not fail a correct screen.

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
