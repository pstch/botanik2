"""SHELL page scrollback: PAGE_UP/PAGE_DOWN and Shift+arrow walk the transcript.

The shell's bare UP/DOWN are command history, so the transcript gets the page
keys (a screenful) and the SHIFTED arrows (a line) - and every OTHER key returns
the view to the live bottom, the way a Linux terminal's scroll-on-keystroke does.
That last part is not a nicety: submitting a line scrolls the content band itself
(line_feed), which would move a frozen window without the scrollback knowing.

Assertions are relative - a line left the screen and came back - rather than
naming a line at a row: what the transcript holds depends on how far each
command's output wrapped and on a 32-line ring, neither of which is under test.

wait_screen(), not wait_for(): a scroll is about what is on screen NOW, and about
a line having LEFT it, which a latched marker cannot answer.

One sim for the file, filled once. Each test takes a freshly submitted newest
line from the `newest` fixture, which also snaps the view back to the bottom, so
no test depends on where the one before it left the window.
"""

import itertools

import pytest

import keys
from helpers import shell

FILL = 20                       # enough rejected commands to overflow the window
MARKER = " more "               # the scrolled-up badge, Scrollback::refresh_scroll_indicator


@pytest.fixture(scope="module")
def sim(spawn_module):
    term = shell(spawn_module)
    # The live log tail would keep pushing the transcript along under a frozen
    # view; these tests are about the keys, so take it out of the picture.
    term.send(b"logs off" + keys.ENTER)
    assert term.wait_for("output disabled"), "`logs off` did not report"
    for i in range(FILL):
        term.send(b"get zz_%02d" % i + keys.ENTER)
    assert term.wait_screen("zz_%02d" % (FILL - 1)), "the fill never reached the screen"
    return term


_seq = itertools.count()


@pytest.fixture
def newest(sim):
    """The shared shell at the live bottom, and the name of its newest line."""
    tag = "zz_top%02d" % next(_seq)
    sim.send(keys.CTRL_U)                      # any key returns the view to the bottom
    sim.send(b"get " + tag.encode() + keys.ENTER)
    assert sim.wait_screen(tag), "the shell stopped taking commands"
    return tag


def test_page_up_scrolls_back_and_page_down_returns(sim, newest):
    sim.send(keys.PAGE_UP)
    assert sim.wait_screen(newest, False), "PAGE_UP did not move the newest line off"
    assert sim.wait_screen(MARKER), "no scrolled-up marker after PAGE_UP"

    sim.send(keys.PAGE_DOWN)
    assert sim.wait_screen(newest), "PAGE_DOWN did not return to the live bottom"
    assert sim.wait_screen(MARKER, False), "the marker outlived the return"


def test_shift_arrows_scroll_one_line(sim, newest):
    # The marker counts the lines between the view and the live bottom, so it
    # reads back exactly how far a key moved - which is the whole claim here.
    sim.send(keys.SHIFT_UP)
    assert sim.wait_screen("v 1 more"), "Shift+Up did not scroll exactly one line"
    sim.send(keys.SHIFT_UP)
    assert sim.wait_screen("v 2 more"), "a second Shift+Up did not go one further"

    sim.send(keys.SHIFT_DOWN)
    assert sim.wait_screen("v 1 more"), "Shift+Down did not come back one line"
    sim.send(keys.SHIFT_DOWN)
    assert sim.wait_screen(MARKER, False), "Shift+Down did not reach the bottom"
    assert sim.wait_screen(newest), "the live bottom did not come back"

    # A bare UP is still the command history - which is why the scroll had to be
    # the shifted arrow - so it snaps back like any other key.
    sim.send(keys.SHIFT_UP)
    assert sim.wait_screen("v 1 more"), "Shift+Up stopped working"
    sim.send(keys.UP)
    assert sim.wait_screen(MARKER, False), "a bare UP did not return to the bottom"


def test_a_keystroke_returns_to_the_live_bottom(sim, newest):
    sim.send(keys.PAGE_UP)
    assert sim.wait_screen(newest, False), "PAGE_UP did not scroll back"

    # Typing is what a Linux terminal snaps to the bottom on, and here it also
    # keeps the frozen window from being scrolled out from under the scrollback
    # by the editor.
    sim.send(keys.text("x"))
    assert sim.wait_screen(newest), "a keystroke did not return to the bottom"
    assert sim.wait_screen(MARKER, False), "the marker outlived the keystroke"
