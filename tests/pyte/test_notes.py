"""NOTES page: a full-screen editor over the operator's note.

Content checks, not goldens: the note is whatever the test types, so the
interesting claims are behavioural - it wraps, the viewport scrolls, and the note
survives a reboot. Own sim per test, since a note is persisted.

The status row is the last row before the alarm bar, hence display[-2].
"""

import keys
from helpers import boot, goto


def notes(spawn, **kwargs):
    """Boot and switch to the NOTES page."""
    term = boot(spawn, **kwargs)
    return goto(term, keys.ALT_8, marker="^O save")


def _type_lines(term, lines):
    """Type `lines` as hard-broken rows.

    Waiting for the last line every few rows keeps a long burst from overrunning
    the sim's 256-byte RX ring. A drop early in the burst still renders the tail,
    so nothing but an assertion on the FIRST line would catch it.
    """
    for i, line in enumerate(lines):
        if i:
            term.send(keys.ENTER)
        term.send(keys.text(line))
        if i % 4 == 3:
            assert term.wait_screen(line), f"the editor never echoed {line!r}"
    assert term.wait_screen(lines[-1]), "the editor never echoed the last line"


def test_notes_wraps_and_reports_state(spawn):
    """A paragraph past the screen width soft-wraps, and the status row tracks
    whether what is on screen has reached flash."""
    term = notes(spawn)
    assert term.wait_for("0/2047 bytes"), "status row missing on an empty note"
    # Read the row rather than wait_for("saved"): the marker would latch here and
    # answer the ^O check below without ever looking at what ^O did.
    assert "saved" in term.screen.display[-2], "an untouched note should not read as modified"

    # 89 chars over 80 columns with a 2-column gutter, so a row holds 78. The
    # editor owns the wrapping (autowrap is off), so assert where the break falls,
    # and that the gutter marks the row the operator started but not its tail.
    line = "the tank three valve sticks when it is cold, tap it before blaming the level sensor again"
    term.send(keys.text(line))
    assert term.wait_screen(line[78:]), "the wrapped tail never reached the second row"
    assert term.screen.display[1].rstrip() == "> " + line[:78], "first row is not gutter + 78 columns"
    assert term.screen.display[2].rstrip() == "  " + line[78:], "wrapped tail is not gutter-blanked"
    assert term.wait_for("modified"), "edits did not mark the note dirty"

    term.send(keys.CTRL_O)
    assert term.wait_screen("saved"), "^O did not clear the dirty marker"


def test_notes_scrolls_when_taller_than_the_screen(spawn):
    """More rows than the viewport: the window follows the cursor down, and
    paging back up brings the first line into view again."""
    term = notes(spawn)
    last = 29
    _type_lines(term, ["line%02d" % i for i in range(last + 1)])

    # The cursor is on the last line, so the window sits at the bottom: the tail
    # is visible and the head has scrolled off (21 content rows once the status
    # bar, the alarm bar and our status row are out).
    assert term.wait_for("line%02d" % last), "the line being typed is not in view"
    assert "line00" not in term.screen.display[1], "the head should have scrolled away"
    assert term.screen.display[1].startswith("> "), "each note line carries the gutter"

    # Page back to the top: one press moves a screenful, the extra ones clamp on
    # the first row. Sent back to back and synced once on content, because a
    # press that only moves the cursor or is clamped changes no cell, and pyte's
    # display holds no cursor.
    for _ in range(4):
        term.send(keys.PAGE_UP)
    assert term.wait_for("line00"), "paging up never reached the first line"


def test_notes_kill_and_yank(spawn):
    """^K / ^W cut and ^Y pastes, over the console-wide ring: this page has no
    shell under it, so the ring cannot belong to CommandShell."""
    term = notes(spawn)
    _type_lines(term, ["valve three sticks", "pump B runs hot"])

    # Waited on the ABSENCE: "pump B runs" is a prefix of the line before the
    # cut too, so waiting for it would pass on the pre-keypress screen.
    term.send(keys.CTRL_W)                       # the word before the cursor
    assert term.wait_screen("hot", present=False), "^W did not cut the last word"
    assert term.screen.display[2].rstrip() == "> pump B runs", \
        "^W took more than the last word"

    term.send(keys.CTRL_Y)                       # ...and back
    assert term.wait_screen("pump B runs hot"), "^Y did not yank the word back"

    # ^K takes the rest of the line, and only that line: the one above it is
    # what a kill-to-buffer-end would have taken with it.
    term.send(keys.CTRL_A + keys.CTRL_K)
    assert term.wait_screen("pump B runs", present=False), "^K did not cut the line"
    assert term.screen.display[1].rstrip() == "> valve three sticks", \
        "^K ate the line above it"


def test_notes_region_kill_and_yank(spawn):
    """^Space anchors a region, ^W takes it, ^Y puts it back - and an edit in
    between drops the mark rather than leaving it pointing at moved text."""
    term = notes(spawn)
    _type_lines(term, ["valve three sticks", "pump B runs hot"])

    # Anchor at the end of the second line, walk to its start: the region is
    # that whole line.
    term.send(keys.CTRL_SPACE + keys.CTRL_A)
    term.send(keys.CTRL_W)
    assert term.wait_screen("pump B runs hot", present=False), "^W did not cut the region"
    assert term.screen.display[1].rstrip() == "> valve three sticks", \
        "the region reached past the line it was anchored on"

    term.send(keys.CTRL_Y)
    assert term.wait_screen("pump B runs hot"), "^Y did not yank the region back"

    # A mark that an edit has invalidated must not survive it: ^W here is a word
    # kill, not a second cut of the old region.
    term.send(keys.CTRL_SPACE + keys.CTRL_A)
    term.send(keys.text("x"))
    assert term.wait_screen("xpump B runs hot"), "the edit never landed"
    term.send(keys.CTRL_W)
    # Word kill: the "x" goes and the line survives. Had the stale region been
    # cut instead, the whole line would have gone with it.
    assert term.wait_screen("> pump B runs hot"), \
        "^W after an edit cut the stale region instead of a word"


def test_notes_survives_a_reboot(spawn, tmp_path):
    """The note is persisted - the whole point of it over a scratch buffer."""
    store = tmp_path / "notes_store"
    note = "spring 2026: valve 3 is seized, do not cycle it"

    term = notes(spawn, store=store)
    term.send(keys.text(note))
    assert term.wait_screen(note), "the note was not echoed before ^O"
    term.send(keys.CTRL_O)
    assert term.wait_screen("saved"), "^O did not report the note saved"

    again = notes(spawn, store=store)            # a second sim over the same store
    assert again.wait_screen(note), "the note did not come back after a restart"
    assert "saved" in again.screen.display[-2], "a restored note should not read as modified"


def test_notes_flushes_on_leaving_the_page(spawn, tmp_path):
    """Switching away is a save: nothing else is going to ask, and losing an edit
    to a keystroke that only meant to look at the logs would be poor."""
    store = tmp_path / "notes_flush_store"
    note = "pump B runs hot above 40C"

    term = notes(spawn, store=store)
    term.send(keys.text(note))
    assert term.wait_screen(note), "the note was not echoed before leaving"
    # The flush is deactivate()'s, so it is done by the time SHELL has painted.
    goto(term, keys.ALT_2)

    again = notes(spawn, store=store)
    assert again.wait_screen(note), "leaving the page did not flush the note"
