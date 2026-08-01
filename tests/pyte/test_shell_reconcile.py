"""Shell page: the TextEditor reconciliation edge cases (console/editor.cpp).

Correct reconciliation must be visually identical to a full redraw, so the
expected screen doubles as the oracle: any wrong splice / IL-DL / prompt-flip
shows up as a mismatch. Geometry: cols=80, PS1 "botanik> " (cap 71), PS2 "> "
(cap 78), PS3 "...> " (cap 75). A digit ruler is used as filler so column
positions stay legible.

ENTER submits a line; a hard '\\n' is inserted with SHIFT_ENTER (Alt+Enter).
"""

import keys
from helpers import assert_editor, shell

RULER = "0123456789" * 8          # 80 chars; first 71 land on the PS1 row


def _type(term, s):
    term.send(keys.text(s))


# 1. Baseline: insert and delete that stay on one row (edit_amount only).

def test_insert_single_row(spawn):
    term = shell(spawn)
    _type(term, "echo hello world")
    assert_editor(term, "botanik> echo hello world")


def test_delete_mid_row(spawn):
    term = shell(spawn)
    _type(term, "abcXYZdef")
    term.send(keys.LEFT * 6)             # cursor before 'X'
    term.send(keys.DELETE * 3)           # remove "XYZ" -> "abcdef"
    assert_editor(term, "botanik> abcdef")


# 2. Overflow past the row cap: a soft wrap opens a PS2 continuation row
#    (row_delta>0, _grow_editor scrolls origin, _new_row paints the opened row).

def test_overflow_soft_wrap(spawn):
    term = shell(spawn)
    _type(term, RULER)                   # 80 chars: 71 on PS1, 9 wrap to PS2
    assert_editor(term,
                  "botanik> " + RULER[:71],
                  "> " + RULER[71:])


# 3. Backspace across the wrap boundary merges two rows back into one
#    (row_delta<0: DL + overlap left-splice + clear_to_eol tail).

def test_backspace_row_merge(spawn):
    term = shell(spawn)
    _type(term, RULER)                   # two rows
    term.send(keys.BACKSPACE * 12)       # 80 -> 68 chars, back to a single row
    assert_editor(term, "botanik> " + RULER[:68])


# 4. Mid-line insert that cascades: the char pushed off the row reflows down,
#    exercising the overlap branch repeatedly.

def test_midline_cascade(spawn):
    term = shell(spawn)
    _type(term, RULER)                   # wrapped line
    term.send(keys.CTRL_A)               # logical line start (buffer pos 0)
    term.send(keys.RIGHT * 40)           # into the middle of the PS1 row
    _type(term, "ZZZZZ")                 # push 5 chars; tail reflows across rows
    spliced = RULER[:40] + "ZZZZZ" + RULER[40:]
    assert_editor(term,
                  "botanik> " + spliced[:71],
                  "> " + spliced[71:])


# 5. Prompt-class flip PS2<->PS3: turning a soft-wrap boundary into a hard break
#    grows the continuation prompt "> " -> "...> " (gutter rewrite, editor.cpp).

def test_ps2_to_ps3_flip(spawn):
    term = shell(spawn)
    _type(term, RULER)                   # PS1 row + PS2 continuation
    term.send(keys.CTRL_A)
    term.send(keys.RIGHT * 71)           # to the wrap boundary
    term.send(keys.SHIFT_ENTER)          # hard break here: PS2 row becomes PS3
    assert_editor(term,
                  "botanik> " + RULER[:71],
                  "...> " + RULER[71:])


# 6. A hard '\n' inserted exactly at the soft-wrap boundary: the post-edit owner
#    row differs from the pre-edit owner (_find_splice_span).

def test_newline_at_boundary(spawn):
    term = shell(spawn)
    _type(term, "7" * 71)                # fills the PS1 row exactly, no wrap yet
    term.send(keys.SHIFT_ENTER)
    _type(term, "tail")
    assert_editor(term,
                  "botanik> " + "7" * 71,
                  "...> tail")


# 7. One batch insert that soft-wraps into several rows in a single edit
#    (row_delta>1: one IL of N rows, then the forward repaint walk).

def test_multirow_batch_insert(spawn):
    term = shell(spawn)
    long_line = RULER + RULER[:100]      # ~180 chars in one send -> ~3 rows
    _type(term, long_line)
    assert_editor(term,
                  "botanik> " + long_line[:71],
                  "> " + long_line[71:149],
                  "> " + long_line[149:])


# 8. Editor taller than the screen: origin clamps to 0 in _grow_editor and the
#    top rows scroll off; the visible window must stay correct.

def test_taller_than_screen(spawn):
    term = shell(spawn)
    _type(term, "line00")
    for i in range(1, 30):               # 30 hard-broken rows > 23 visible
        term.send(keys.SHIFT_ENTER)
        _type(term, "line%02d" % i)
        if i % 10 == 0:
            term.settle()                # keep the RX ring from overrunning
    # Assert the visible editor window (rows 1..22) only. Row 23 is where the
    # scrolled-off overflow rows overprint the alarm bar - a garbled artifact
    # whose bar content also drifts with the fast greenhouse; not what this
    # test checks. line00 has scrolled off the top.
    assert_editor(term, *["...> line%02d" % i for i in range(1, 23)],
                  clip=(0, 23), alarm_bar=False)


# 9. Delete-forward vs backspace: same resulting buffer, different cursor. A
#    follow-up insert that depends on the cursor makes the difference visible.

def test_delete_forward_then_insert(spawn):
    term = shell(spawn)
    _type(term, "abc")
    term.send(keys.CTRL_A)               # cursor at pos 0
    term.send(keys.DELETE)               # remove 'a' -> "bc", cursor at 0
    _type(term, "X")                     # -> "Xbc"
    assert_editor(term, "botanik> Xbc")


def test_backspace_then_insert(spawn):
    term = shell(spawn)
    _type(term, "abc")
    term.send(keys.LEFT)                 # cursor before 'c'
    term.send(keys.BACKSPACE)            # remove 'b' -> "ac", cursor at 1
    _type(term, "X")                     # -> "aXc"
    assert_editor(term, "botanik> aXc")


# 10. Empty the buffer back to a single PS1 row (row count shrinks to 1).

def test_clear_to_empty(spawn):
    term = shell(spawn)
    _type(term, RULER)                   # multi-row
    term.send(keys.CTRL_U)               # clear the whole input
    assert_editor(term, "botanik>")
