"""What ref's own table puts on screen: its HOME, its programs, its rows.

The library's screen tests name only library rows; these name the site's, so
they sit beside the table they read. One sim per test.
"""

import keys
from helpers import boot, goto, shell

# HOME's rows 16+ are a live log tail the greenhouse floods; the widget rows are
# the comparable part, and the only full-screen golden left in the suite.
HOME_WIDGET = (0, 16)


def test_home_after_reset(spawn, assert_screen):
    """`reset` re-execs the firmware and it reboots to Home.

    HAL::reset() execve's /proc/self/exe and the PTY fds survive, so the screen
    clears and redraws to a freshly booted Home. Asserting the SAME golden as the
    cold boot is the claim: a reboot is indistinguishable from a cold start.
    """
    term = boot(spawn)
    goto(term, keys.ALT_2)                       # SHELL
    term.send(b"reset confirm" + keys.ENTER)
    assert_screen(term, "page_home", rows=HOME_WIDGET)


def test_monitor_forces_a_row_and_releases_it(spawn):
    """MONITOR's ^O / ^R / ^S drive a real override through the real bank.

    Worth a whole-firmware test rather than a page unit test: ^S goes to
    storage, and ^S is 0x13 - XOFF at the tty layer, and the Minitel separator
    at the parser. Both are places the key can be eaten before the page sees it.
    """
    term = boot(spawn)
    goto(term, keys.ALT_6, "VARIABLE")
    term.send(keys.text("air_temp_hi1"))         # one reading and its quality
    assert term.wait_screen("[/air_temp_hi1]"), "the filter did not narrow the table"
    # The zone is unbelievable until the simulated field has answered its three
    # probes. ^V below is about taking a BELIEVED row away, so wait for one.
    assert term.wait_screen("Quality      : GOOD"), "the ceiling never came up"

    term.send(keys.CTRL_O)
    assert term.wait_screen("air_temp_hi1 ="), "^O did not open the override field"
    term.send(keys.CTRL_U + b"33.5" + keys.ENTER)
    assert term.wait_screen("33.5"), "the override did not reach the OVERRIDE column"
    # TWO: the value, and the belief that came with it. Forcing a reading says
    # to run on it, and the quality is a row of its own now, so both are real
    # overrides an operator can see and release.
    assert term.wait_screen("2 unsaved"), "the legend did not count the unsaved override"

    # ^V forces the row's `_qlt` COMPANION, which is where a quality lives now:
    # the reading stays where it was put and stops being believed.
    term.send(keys.CTRL_V)
    assert term.wait_screen("forced to read invalid"), "^V did not force the quality"
    assert term.wait_screen("33.5"), "forcing the quality moved the value"
    term.send(keys.CTRL_V)
    assert term.wait_screen("forced to read valid"), "^V did not force it back"
    assert term.wait_screen("33.5"), "the flip moved the forced value"

    term.send(keys.CTRL_S)
    assert term.wait_screen("override saved"), "^S did not reach storage"
    assert term.wait_screen("1 unsaved", present=False), "the row is still owed to flash"

    term.send(keys.CTRL_R)
    assert term.wait_screen("override released"), "^R did not release it"
    assert term.wait_screen("33.5", present=False), "the forced value is still on the row"


def test_tasks_page_reports_program_health(spawn):
    """HEALTH is one vocabulary for both kinds of entry, blank on one of them.

    A component reports its own verdict and a control program that opted in
    reports the law's. The blank is now exactly one case - a program with no
    health row - and it is the half worth pinning: a page that read a verdict
    off that task anyway would fault, not misprint.
    """
    term = boot(spawn)
    goto(term, keys.ALT_7, "TASK")

    # The site's programs head the table, the library's sys_ tasks follow. By
    # name alone sys_alarms would come first, so this is the ordering, not the
    # alphabet.
    def cells(col):
        rows = [l for l in term.view() if l.startswith("  | ")]
        return [r.split("|")[col].strip() for r in rows
                if r.split("|")[1].strip() not in ("", "TASK")]

    def names():
        return [n for n in cells(1) if n]
    assert term.until(lambda: len(names()) > 3), "the table never painted"
    # This site has two laws, so it is the FIRST rows that must be theirs: by
    # name alone sys_alarms would sort above both.
    assert not any(n.startswith("sys_") for n in names()[:2]), \
        "a library task sits above the site's programs: %s" % names()

    # vent opts into health in programs.csv, so it has a verdict to
    # print. Any of the three words will do: what is being tested is that the
    # column reaches the page, not what the plant happens to be doing.
    term.send(keys.text("vent"))
    assert term.wait_screen("[/vent]"), "the filter did not narrow the table"
    words = ("HEALTHY", "DEGRADED", "FAULTED")
    # The COLUMN, and waited on: the legend is PRIO_CHROME and goes out a frame
    # ahead of the band it describes, so the table below it is still the old one.
    assert term.until(lambda: names() == ["vent"]), \
        "the table did not narrow: %s" % names()
    assert term.until(lambda: all(c in words for c in cells(3))), \
        "a program that opted into health reported none: %s" % cells(3)
    # The period left the table for the panel, beside the share it takes.
    assert term.wait_screen("Every ms"), "the panel is missing the declared period"

    # ...and one that did not opt in prints nothing there. Same page, same
    # column, so this is the blank half of the claim above. Read off the
    # UNFILTERED table rather than through a second filter: one table showing
    # both answers is the claim, and it needs no refiltering to make it.
    term.send(keys.ESC)
    assert term.wait_screen("type to filter"), "ESC did not clear the filter"

    def health_of(task):
        for l in term.view():
            if not l.startswith("  | "):
                continue
            parts = l.split("|")
            if len(parts) > 3 and parts[1].strip() == task:
                return parts[3].strip()
        return None

    assert term.until(lambda: health_of("vent") in words), \
        "the law that opted in reported no verdict: %s" % health_of("vent")
    assert term.until(lambda: health_of("water") == ""), \
        "a program with no health row printed one: %s" % health_of("water")


def test_the_task_table_is_complete(spawn):
    """The count is the check, and the shell is where to ask it.

    A registration that does not happen is invisible: it fails no build and no
    unit test, the work simply never runs. Twice in this tree that cost a
    subsystem silently - the platform alarms, then the whole network. What
    catches it is that the total moves.

    Asked through the shell rather than read off the TASKS page on purpose: the
    page is a name-sorted scrolling table, so a scroll that stops short reads
    exactly like an absent task. `tasks stats` answers in one line and cannot be
    misread that way.
    """
    term = shell(spawn, quiet_logs=True)
    term.send(b"tasks stats" + keys.ENTER)

    # Every task a full build registers, the site's control programs included.
    # Move it deliberately when the set changes; an unexplained DROP is the bug
    # this exists to catch. Last moved when Vars::QualityMirror went: a quality
    # is the value of a `_qlt` row now, so there is nothing to copy per beat.
    assert term.wait_for("Registered ....... : 24"), \
        "the task table changed size - a registration was added or silently lost"
    assert term.wait_for("0 refused"), "a registration was refused: MAX_TASKS"


def test_a_named_task_answers_for_itself(spawn):
    """`tasks get <name>` is the decisive per-task check (design/scan-cycle-rework.md).

    One from each phase, so a whole phase going missing is caught by name and
    not only by the count above.
    """
    term = shell(spawn, quiet_logs=True)
    for name, phase in (("sys_console_rx", "input"), ("sys_persist", "config"),
                        ("sys_health", "service"), ("vent", "control"),
                        ("sys_mbrtu_tx", "output"), ("sys_console_tx", "present")):
        term.send(b"tasks get " + name.encode() + keys.ENTER)
        assert term.wait_for(phase), "no task named %s in %s" % (name, phase)


def test_monitor_acts_on_the_row_it_drew(spawn):
    """A live view selects on READINGS, so its rows move with no keypress.

    `override` lists exactly the forced rows. Force one, and the row under the
    cursor in the unfiltered `all` view is a different variable from the one
    `override` shows at the same index - which is the drift a key must not
    follow. The panel names the row, so it is what proves which one is armed.
    """
    term = boot(spawn)
    goto(term, keys.ALT_6, "VARIABLE")
    # A group with no _qlt companion, so every row the filter leaves is a
    # number the override field will take. The tank law did not opt into
    # quality, so its totals are the rows without one.
    term.send(keys.text("tank_"))
    assert term.wait_screen("[/tank_]"), "the filter did not narrow the table"

    # Force the SECOND row, so the override view's only entry is not the one
    # sitting at index 0 of the filtered `all` view.
    term.send(keys.DOWN)
    term.send(keys.CTRL_O)
    assert term.wait_screen("="), "^O did not open the override field"
    term.send(keys.CTRL_U + b"31.5" + keys.ENTER)
    assert term.wait_screen("31.5"), "the override did not land"

    # Into the override view: one row, and it is the one just forced.
    term.send(keys.LEFT)
    assert term.wait_screen("[override]"), "the view did not change"

    # ^R acts on what the panel names. If the key re-walked the table instead
    # of using the drawn row, it would release something else - and the value
    # would still be on screen.
    term.send(keys.CTRL_R)
    assert term.wait_screen("override released"), "^R did not release it"
    assert term.wait_screen("31.5", present=False), "the forced value survived: the key hit another row"
