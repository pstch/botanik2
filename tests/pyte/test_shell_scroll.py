"""SHELL page scrollback: PAGE_UP/PAGE_DOWN and Shift+arrow walk the transcript.

The shell's bare UP/DOWN are command history, so the transcript gets the page
keys (a screenful) and the SHIFTED arrows (a line) - and every OTHER key returns
the view to the live bottom, the way a Linux terminal's scroll-on-keystroke
does. That last part is not a nicety: submitting a line scrolls the content band
itself (line_feed), which would move a frozen window without the scrollback
knowing.

The assertions are relative - "the newest line left the screen and came back" -
rather than naming a line at a row. What the transcript holds depends on how
many rows each command's output wrapped to and on a 32-line ring, neither of
which this test is about.
"""

import time

import keys
from helpers import shell

# Enough rejected commands to overflow the window and saturate the ring, so
# there is history to scroll into whatever each one printed.
FILL = 20
NEWEST = "zz_%02d" % (FILL - 1)
# The scrolled-up marker, " v N more " (Scrollback::refresh_scroll_indicator).
MARKER = " more "


def _wait_screen(term, needle, want=True, timeout=6.0):
    """Pump until `needle` is (or is no longer) on the CURRENT screen.

    wait_for() latches a marker as it goes by, which is the wrong question here:
    a scroll is exactly about what is on screen NOW, and about a line having
    LEFT it.
    """
    deadline = time.time() + timeout
    while True:
        on = any(needle in row for row in term.screen.display)
        if on == want or time.time() >= deadline:
            return on == want
        term._pump(0.02)


def _fill(term):
    """A shell with a transcript taller than its window."""
    # The live log tail would keep pushing the transcript along under a frozen
    # view; this test is about the keys, so take it out of the picture.
    term.send(b"logs off" + keys.ENTER)
    assert term.wait_for("output disabled"), "`logs off` did not report"
    for i in range(FILL):
        term.send(b"get zz_%02d" % i + keys.ENTER)
    assert _wait_screen(term, NEWEST), "the fill never reached the screen"
    term.settle()


def test_page_up_scrolls_back_and_page_down_returns(spawn):
    term = shell(spawn)
    _fill(term)

    term.send(keys.PAGE_UP)
    assert _wait_screen(term, NEWEST, want=False), \
        "PAGE_UP did not move the newest line off the window"
    assert _wait_screen(term, MARKER), "no scrolled-up marker after PAGE_UP"

    term.send(keys.PAGE_DOWN)
    assert _wait_screen(term, NEWEST), "PAGE_DOWN did not return to the live bottom"
    assert _wait_screen(term, MARKER, want=False), "the marker outlived the return"


def test_shift_arrows_scroll_one_line(spawn):
    term = shell(spawn)
    _fill(term)

    # The marker counts the lines between the view and the live bottom, so it
    # reads back exactly how far a key moved - which is the whole claim here.
    term.send(keys.SHIFT_UP)
    assert _wait_screen(term, "v 1 more"), "Shift+Up did not scroll exactly one line"
    term.send(keys.SHIFT_UP)
    assert _wait_screen(term, "v 2 more"), "a second Shift+Up did not go one further"

    term.send(keys.SHIFT_DOWN)
    assert _wait_screen(term, "v 1 more"), "Shift+Down did not come back one line"
    term.send(keys.SHIFT_DOWN)
    assert _wait_screen(term, MARKER, want=False), "Shift+Down did not reach the bottom"
    assert _wait_screen(term, NEWEST), "the live bottom did not come back"

    # A bare UP is still the command history - which is why the scroll had to be
    # the shifted arrow - so it snaps back like any other key.
    term.send(keys.SHIFT_UP)
    assert _wait_screen(term, "v 1 more"), "Shift+Up stopped working"
    term.send(keys.UP)
    assert _wait_screen(term, MARKER, want=False), "a bare UP did not return to the bottom"


def test_a_keystroke_returns_to_the_live_bottom(spawn):
    term = shell(spawn)
    _fill(term)

    term.send(keys.PAGE_UP)
    assert _wait_screen(term, NEWEST, want=False), "PAGE_UP did not scroll back"

    # Typing is what a Linux terminal snaps to the bottom on, and here it is
    # also what keeps the frozen window from being scrolled out from under the
    # scrollback by the editor.
    term.send(keys.text("x"))
    assert _wait_screen(term, NEWEST), "a keystroke did not return to the bottom"
    assert _wait_screen(term, MARKER, want=False), "the marker outlived the keystroke"

    # The line is still editable, and submitting it lands at the bottom too.
    term.send(keys.CTRL_U)
    term.send(b"get zz_done" + keys.ENTER)
    assert _wait_screen(term, "zz_done"), "the shell stopped taking commands"
