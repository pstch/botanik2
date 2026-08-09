"""Shell page: the TextEditor reconciliation edge cases (console/editor.cpp).

Correct reconciliation must be visually identical to a full redraw, so the
expected screen doubles as the oracle: a wrong splice / IL-DL / prompt-flip shows
up as a mismatch. Geometry: cols=80, PS1 "botanik> " (cap 71), PS2 "> " (cap 78),
PS3 "...> " (cap 75). A digit ruler is the filler, so column positions stay
legible. ENTER submits a line; a hard '\\n' is SHIFT_ENTER (Alt+Enter).

One sim for the file: nothing here submits a line, so the transcript stays empty
and ^U + ^L is a full reset between scenarios (see the `term` fixture).
"""

import pytest

import keys
from helpers import assert_editor, shell

RULER = "0123456789" * 8          # 80 chars; first 71 land on the PS1 row


@pytest.fixture(scope="module")
def sim(spawn_module):
    return shell(spawn_module)


@pytest.fixture
def term(sim):
    """The shared shell, reset to an empty prompt.

    ^U clears the input; ^L is the console's own full repaint, which matters
    after a scenario that grew the editor past the screen and overprinted the
    alarm bar (console.cpp redraws the chrome before the page).
    """
    sim.send(keys.CTRL_U + keys.CTRL_L)
    assert_editor(sim, "botanik>")
    return sim


# Each case is (keys to send, expected editor rows). The comment on each says
# which branch of the reconciliation it is there to exercise - that, and not the
# keystrokes, is why the case exists.
SPLICED = RULER[:40] + "ZZZZZ" + RULER[40:]      # a mid-row insert that reflows
BATCH = RULER + RULER[:100]                      # ~180 chars in one send, ~3 rows

CASES = {
    # Baseline: insert and delete that stay on one row (edit_amount only).
    "insert_single_row": (
        [b"echo hello world"],
        ["botanik> echo hello world"]),
    "delete_mid_row": (
        [b"abcXYZdef", keys.LEFT * 6, keys.DELETE * 3],      # cursor before 'X', kill "XYZ"
        ["botanik> abcdef"]),

    # Overflow past the row cap opens a PS2 continuation row (row_delta>0:
    # _grow_editor scrolls the origin, _new_row paints the opened row).
    "overflow_soft_wrap": (
        [RULER.encode()],
        ["botanik> " + RULER[:71], "> " + RULER[71:]]),

    # Backspace across the wrap boundary merges the rows back into one
    # (row_delta<0: DL + overlap left-splice + clear_to_eol tail).
    "backspace_row_merge": (
        [RULER.encode(), keys.BACKSPACE * 12],               # 80 -> 68 chars
        ["botanik> " + RULER[:68]]),

    # Mid-line insert that cascades: the chars pushed off the row reflow down,
    # exercising the overlap branch repeatedly.
    "midline_cascade": (
        [RULER.encode(), keys.CTRL_A, keys.RIGHT * 40, b"ZZZZZ"],
        ["botanik> " + SPLICED[:71], "> " + SPLICED[71:]]),

    # Prompt-class flip PS2<->PS3: turning a soft-wrap boundary into a hard break
    # grows the continuation prompt "> " -> "...> " (gutter rewrite).
    "ps2_to_ps3_flip": (
        [RULER.encode(), keys.CTRL_A, keys.RIGHT * 71, keys.SHIFT_ENTER],
        ["botanik> " + RULER[:71], "...> " + RULER[71:]]),

    # A hard '\n' exactly at the soft-wrap boundary: the post-edit owner row
    # differs from the pre-edit one (_find_splice_span).
    "newline_at_boundary": (
        [b"7" * 71, keys.SHIFT_ENTER, b"tail"],              # 71 fills PS1 exactly
        ["botanik> " + "7" * 71, "...> tail"]),

    # One batch insert soft-wrapping into several rows in a single edit
    # (row_delta>1: one IL of N rows, then the forward repaint walk).
    "multirow_batch_insert": (
        [BATCH.encode()],
        ["botanik> " + BATCH[:71], "> " + BATCH[71:149], "> " + BATCH[149:]]),

    # Delete-forward vs backspace: same buffer, different cursor. A follow-up
    # insert is what makes the difference visible.
    "delete_forward_then_insert": (
        [b"abc", keys.CTRL_A, keys.DELETE, b"X"],            # "bc" cursor 0 -> "Xbc"
        ["botanik> Xbc"]),
    "backspace_then_insert": (
        [b"abc", keys.LEFT, keys.BACKSPACE, b"X"],           # "ac" cursor 1 -> "aXc"
        ["botanik> aXc"]),

    # Kill and yank: the cut is a plain remove, but the yank re-inserts a whole
    # span mid-buffer, which is the multi-char splice branch under another name.
    "kill_word_back": (
        [b"set cli_setpoint 21", keys.ALT_B, keys.CTRL_W],
        ["botanik> set 21"]),
    "kill_to_eol_then_yank": (
        [b"echo hello world", keys.CTRL_A, keys.RIGHT * 5, keys.CTRL_K,
         keys.CTRL_A, keys.CTRL_Y],
        ["botanik> hello worldecho"]),

    # A region kill: ^Space anchors, the motion drags the span, ^W takes it.
    "kill_region": (
        [b"echo hello world", keys.CTRL_A, keys.RIGHT * 5,
         keys.CTRL_SPACE, keys.RIGHT * 6, keys.CTRL_W],
        ["botanik> echo world"]),

    # Empty the buffer back to a single PS1 row (row count shrinks to 1).
    "clear_to_empty": (
        [RULER.encode(), keys.CTRL_U],
        ["botanik>"]),
}


@pytest.mark.parametrize("sends,rows", CASES.values(), ids=CASES.keys())
def test_reconcile(term, sends, rows):
    for chunk in sends:
        term.send(chunk)
    assert_editor(term, *rows)


def _prompt_row(term):
    """Screen row the PS1 prompt is on (the editor is bottom-anchored)."""
    for y, line in enumerate(term.screen.display):
        if line.startswith("botanik>"):
            return y
    raise AssertionError("no prompt row\n" + term.snapshot())


def _reversed_span(term, row):
    """[first, last) columns drawn reverse-video on `row`, or None."""
    cells = term.screen.buffer[row]
    cols = [x for x in range(term.screen.columns) if cells[x].reverse]
    return (cols[0], cols[-1] + 1) if cols else None


def test_region_is_highlighted(term):
    """The region is reverse-video on the wire, not just a pair of offsets.

    Asserted on pyte's per-cell attributes rather than on the text, because that
    is the whole mechanism: the editor re-stamps its rows and the terminal's cell
    diff turns that into the few SGR runs that actually changed.
    """
    term.send(b"echo hello world")
    assert_editor(term, "botanik> echo hello world")
    row = _prompt_row(term)
    assert _reversed_span(term, row) is None, "nothing should be highlighted yet"

    # Anchor after "echo ", then drag over "hello ". PS1 is 9 columns.
    term.send(keys.CTRL_A + keys.RIGHT * 5 + keys.CTRL_SPACE)
    term.send(keys.RIGHT * 6)
    term.poll(lambda: _reversed_span(term, row) == (14, 20))
    assert _reversed_span(term, row) == (14, 20), \
        "the region did not land on the columns holding 'hello '"

    # ^G releases it and the row goes plain again, text untouched.
    term.send(keys.CTRL_G)
    term.poll(lambda: _reversed_span(term, row) is None)
    assert _reversed_span(term, row) is None, "^G left the highlight behind"
    assert_editor(term, "botanik> echo hello world")


def test_taller_than_screen(term):
    """Editor taller than the screen: origin clamps to 0 in _grow_editor, the top
    rows scroll off, and the visible window stays correct."""
    term.send(b"line00")
    for i in range(1, 30):               # 30 hard-broken rows > 23 visible
        term.send(keys.SHIFT_ENTER)
        term.send(b"line%02d" % i)
        if i % 10 == 0:                  # keep the RX ring from overrunning
            assert term.wait_screen("line%02d" % i), "the editor fell behind the burst"
    # Rows 1..22 only: row 23 is where the scrolled-off overflow overprints the
    # alarm bar, a garbled artifact this test is not about. line00 has gone.
    assert_editor(term, *["...> line%02d" % i for i in range(1, 23)],
                  clip=(0, 23), alarm_bar=False)
