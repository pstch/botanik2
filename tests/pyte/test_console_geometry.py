"""The console's own geometry: `console_cols` / `console_rows` reach the screen.

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
    """The `console_` group is applied on its COMMIT SERIAL, which a save stores
    along with the rest and a load restores unchanged - so nothing on the way
    back up is an edge, and the console has to adopt the stored rows itself."""
    store = tmp_path / "geometry_store"

    # The emulator is bigger than the sim boots, so a widen past the 80x25
    # floor is visible at all.
    term = shell(spawn, quiet_logs=True, store=store, screen=(110, 34))
    term.until(lambda: _geometry(term)[1] is not None)
    width, alarm_row = _geometry(term)
    assert alarm_row == 24, "the sim should boot at the configured 25 rows"

    _cmd(term, "params set console_cols 100", "console_cols = 100")
    _cmd(term, "params set console_rows 30", "console_rows = 30")
    _cmd(term, "params save console_cols", "console_cols saved")
    _cmd(term, "params save console_rows", "console_rows saved")

    again = boot(spawn, store=store, screen=(110, 34))   # a second sim, same store
    _wait_geometry(again, (width + 20, 29),           # 20 columns and 5 rows more
                   "the saved geometry did not come back at the next boot")


def test_the_group_applies_on_its_commit_serial(spawn):
    """An edit STAGES: the three rows describe one terminal, and applying them
    one at a time walks the screen through geometries that match nothing."""
    term = shell(spawn, quiet_logs=True, screen=(110, 34))
    term.until(lambda: _geometry(term)[1] is not None)
    width, alarm_row = _geometry(term)

    _cmd(term, "params set console_cols 100", "console_cols = 100")
    _cmd(term, "params set console_rows 30", "console_rows = 30")
    term.run(60)
    assert _geometry(term) == (width, alarm_row), \
        "a staged `term_` edit resized the screen before the commit"

    _cmd(term, "params set console_serial 2", "console_serial = 2")
    _wait_geometry(term, (width + 20, 29),
                   "the commit serial did not apply the staged geometry")
