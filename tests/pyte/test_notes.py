"""NOTES page: a full-screen editor over the operator's note.

Content checks rather than full-screen goldens. The note is whatever the test
types, so the interesting assertions are behavioural - the text wraps and stays
readable, the viewport scrolls once there is more of it than screen, and the
note survives a reboot - none of which a frozen screen says any better.
"""

import keys
from helpers import boot, goto


def notes(spawn, **kwargs):
    """Boot and switch to the NOTES page."""
    term = boot(spawn, **kwargs)
    goto(term, keys.ALT_7, marker="^O save")
    return term


def _type_lines(term, lines):
    """Type `lines` as hard-broken rows.

    Settle every few lines: the sim reads its console through a 256-byte RX ring
    behind a throttled UART, and a long unbroken burst can overrun it. A drop
    early in the burst is the failure mode to avoid - the tail still renders, so
    only an assertion on the *first* line would ever catch it.
    """
    for i, line in enumerate(lines):
        if i:
            term.send(keys.ENTER)
        term.send(keys.text(line))
        if i % 4 == 3:
            term.settle()
    term.settle()


def test_notes_wraps_and_reports_state(spawn):
    """A paragraph past the screen width soft-wraps, and the status row tracks
    whether what is on screen has reached flash."""
    term = notes(spawn)
    assert term.wait_for("0/2047 bytes"), "status row missing on an empty note"
    assert term.wait_for("saved"), "an untouched note should not read as modified"

    # 89 chars over an 80-column screen with a 2-column gutter, so a row holds
    # 78. The editor owns the wrapping (the terminal's autowrap is off), so
    # assert where the break actually falls - and that the gutter marks the row
    # the operator started but not its wrapped tail, which is the whole point of
    # having one.
    line = "the tank three valve sticks when it is cold, tap it before blaming the level sensor again"
    term.send(keys.text(line))
    assert term.wait_for(line[58:78]), "typed text never reached the row end"
    term.settle()
    assert term.screen.display[1].rstrip() == "> " + line[:78], "first row is not a full gutter + 78 columns"
    assert term.screen.display[2].rstrip() == "  " + line[78:], "wrapped tail is not gutter-blanked"
    assert term.wait_for("modified"), "edits did not mark the note dirty"

    term.send(keys.CTRL_O)
    assert term.wait_for("saved"), "^O did not clear the dirty marker"


def test_notes_scrolls_when_taller_than_the_screen(spawn):
    """More rows than the viewport: the window follows the cursor down, and
    paging back up brings the first line into view again."""
    term = notes(spawn)
    last = 29
    _type_lines(term, ["line%02d" % i for i in range(last + 1)])

    # The cursor is on the last line, so the window sits at the bottom of the
    # note: the tail is visible and the head has scrolled off (the content area
    # is 21 rows once the status bar, the alarm bar and our status row are out).
    assert term.wait_for("line%02d" % last), "the line being typed is not in view"
    assert "line00" not in term.screen.display[1], "the head should have scrolled away"
    assert term.screen.display[1].startswith("> "), "each note line carries the gutter"

    # Page back to the top. One press only moves a screenful, and the last lands
    # on the first row rather than scrolling past it.
    for _ in range(4):
        term.send(keys.PAGE_UP)
        term.settle()
    assert term.wait_for("line00"), "paging up never reached the first line"


def test_notes_survives_a_reboot(spawn, tmp_path):
    """The note is persisted, so it is still there after a power cycle - the
    whole point of it over a scratch buffer."""
    store = tmp_path / "notes_store"
    note = "spring 2026: valve 3 is seized, do not cycle it"

    term = notes(spawn, store=store)
    term.send(keys.text(note))
    term.settle()
    term.send(keys.CTRL_O)
    assert term.wait_for("saved"), "^O did not report the note saved"

    # A second sim over the same store is the reboot.
    again = notes(spawn, store=store)
    assert again.wait_for(note), "the note did not come back after a restart"
    assert again.wait_for("saved"), "a restored note should not read as modified"


def test_notes_flushes_on_leaving_the_page(spawn, tmp_path):
    """Switching away is a save: nothing else is going to ask, and losing an
    edit to a keystroke that only meant to look at the logs would be poor."""
    store = tmp_path / "notes_flush_store"
    note = "pump B runs hot above 40C"

    term = notes(spawn, store=store)
    term.send(keys.text(note))
    term.settle()
    goto(term, keys.ALT_2)               # to SHELL without pressing ^O
    term.settle()

    again = notes(spawn, store=store)
    assert again.wait_for(note), "leaving the page did not flush the note"
