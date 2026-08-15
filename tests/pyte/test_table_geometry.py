"""The scrolling tables lay their columns out for the terminal they are on.

The `console_` group is the only thing that moves the geometry at runtime, so the
resize goes through it and what is asserted is what the TABLE draws: every
bordered row spans the display between equal margins, and none overruns it.

The emulator is wider than the sim boots, so a widen past 80 is visible; a
console narrower than the emulator simply leaves the columns past it blank.
"""

import keys
from helpers import boot, shell, goto

# Every table page, by its tab key and the status-bar label a switch syncs on.
TABLES = [(keys.ALT_4, "4:ALARMS"), (keys.ALT_5, "5:PARAMS"),
          (keys.ALT_6, "6:MONITOR"), (keys.ALT_7, "7:TASKS")]

# The table's own rows: the header, the rules (whose caps carry the scroll
# arrows) and the data rows. Everything else on a table page is panel text.
_EDGES = "  |", "  +", "  ^", "  v"


def _table_rows(term):
    return [r.rstrip() for r in term.screen.display if r.startswith(_EDGES)]


def _cmd(term, text, reply):
    term.send(text.encode() + keys.ENTER)
    assert term.wait_screen(reply), \
        "%r did not report %r\n%s" % (text, reply, term.snapshot())


def _resize(term, cols, serial):
    """Set the width and commit it; the group applies on its serial."""
    goto(term, keys.ALT_2)
    _cmd(term, "params set console_cols %d" % cols, "console_cols = %d" % cols)
    _cmd(term, "params set console_serial %d" % serial, "console_serial = %d" % serial)


def _assert_fits(term, key, label, cols):
    """Every bordered row of this page's table spans exactly the display."""
    goto(term, key)
    want = cols - 2                       # a blank margin each side

    def ok():
        rows = _table_rows(term)
        return bool(rows) and all(len(r) == want and r[-1] in "|+^v" for r in rows)

    term.until(ok)
    rows = _table_rows(term)
    assert rows, "%s drew no table\n%s" % (label, term.snapshot())
    for r in rows:
        assert r[-1] in "|+^v", \
            "%s at %d cols: a row was clipped mid-cell\n%r\n%s" % (
                label, cols, r, term.snapshot())
        assert len(r) == want, \
            "%s at %d cols: a row is %d wide, wanted %d\n%r\n%s" % (
                label, cols, len(r), want, r, term.snapshot())


def test_the_tables_follow_the_terminal_width(spawn):
    """Widened, the columns take the new width; back at the floor, they still fit."""
    term = shell(spawn, quiet_logs=True, screen=(110, 24))

    for key, label in TABLES:
        _assert_fits(term, key, label, 80)          # the sim's boot geometry

    _resize(term, 100, 2)
    for key, label in TABLES:
        _assert_fits(term, key, label, 100)

    # 80x25 is the floor (Config::SCREEN_MIN_*), so "narrowed" means back to it.
    _resize(term, 80, 3)
    for key, label in TABLES:
        _assert_fits(term, key, label, 80)
