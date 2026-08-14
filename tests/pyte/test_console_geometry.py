"""The console's own geometry: `term_cols` / `term_rows` reach the screen.

The `term_` rows are the library's own parameters and describe the terminal on
the wire, so what has to be asserted is the size of what is DRAWN, not the line
reporting the edit. Two facts on screen say it: the status bar spans the full
width, and the alarm bar sits on the physical last row.

Own sim per test - both of these commit to the parameter store.
"""

import keys
from helpers import boot, shell


def _geometry(term):
    """(status-bar width, row the alarm bar sits on), as the screen shows it.

    The width is the bar's own, not the emulator's: pyte is always 80x24, so a
    narrower console simply leaves the columns past it blank. The metrics field
    is right-aligned, so the last non-blank column tracks the width exactly.
    """
    rows = [r.rstrip() for r in term.screen.display]
    alarm = [i for i, r in enumerate(rows) if r.startswith(" ALARMS")]
    return len(rows[0]), (alarm[-1] if alarm else None)


def _wait_geometry(term, want, why):
    """Run beats until the screen reads @p want, then say so."""
    term.until(lambda: _geometry(term) == want)
    assert _geometry(term) == want, "%s\nwanted %s, got %s\n%s" % (
        why, want, _geometry(term), term.snapshot())


def _cmd(term, text, reply):
    term.send(text.encode() + keys.ENTER)
    assert term.wait_screen(reply), \
        "%r did not report %r\n%s" % (text, reply, term.snapshot())


def test_a_saved_geometry_comes_back_at_the_next_boot(spawn, tmp_path):
    """The `term_` group is applied on its COMMIT SERIAL, which a save stores
    along with the rest and a load restores unchanged - so nothing on the way
    back up is an edge, and the console has to adopt the stored rows itself."""
    store = tmp_path / "geometry_store"

    term = shell(spawn, quiet_logs=True, store=store)
    term.until(lambda: _geometry(term)[1] is not None)
    width, alarm_row = _geometry(term)
    assert alarm_row == 23, "the sim should boot at the configured 24 rows"

    _cmd(term, "params set term_cols 50", "term_cols = 50")
    _cmd(term, "params set term_rows 16", "term_rows = 16")
    _cmd(term, "params save term_cols", "term_cols saved")
    _cmd(term, "params save term_rows", "term_rows saved")

    again = boot(spawn, store=store)                 # a second sim, same store
    _wait_geometry(again, (width - 30, 15),           # 30 columns and 8 rows less
                   "the saved geometry did not come back at the next boot")


def test_the_group_applies_on_its_commit_serial(spawn):
    """An edit STAGES: the three rows describe one terminal, and applying them
    one at a time walks the screen through geometries that match nothing."""
    term = shell(spawn, quiet_logs=True)
    term.until(lambda: _geometry(term)[1] is not None)
    width, alarm_row = _geometry(term)

    _cmd(term, "params set term_cols 50", "term_cols = 50")
    _cmd(term, "params set term_rows 16", "term_rows = 16")
    term.run(60)
    assert _geometry(term) == (width, alarm_row), \
        "a staged `term_` edit resized the screen before the commit"

    _cmd(term, "params set term_serial 2", "term_serial = 2")
    _wait_geometry(term, (width - 30, 15),
                   "the commit serial did not apply the staged geometry")
