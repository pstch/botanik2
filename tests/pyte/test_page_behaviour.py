"""Whole-firmware behaviour a unit test cannot reach: a real re-exec, the network
state machine, and the task table actually being registered.

One file, because a CTest entry costs an interpreter startup. Own sim per test.
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
    term.send(b"reset" + keys.ENTER)
    assert_screen(term, "page_home", rows=HOME_WIDGET)


def test_monitor_forces_a_row_and_releases_it(spawn):
    """MONITOR's ^O / ^R / ^S drive a real override through the real bank.

    Worth a whole-firmware test rather than a page unit test: ^S goes to
    storage, and ^S is 0x13 - XOFF at the tty layer, and the Minitel separator
    at the parser. Both are places the key can be eaten before the page sees it.
    """
    term = boot(spawn)
    goto(term, keys.ALT_6, "VARIABLE")
    term.send(keys.text("cli_temp_hi"))          # one row plus its three probes
    assert term.wait_screen("[/cli_temp_hi]"), "the filter did not narrow the table"

    term.send(keys.CTRL_O)
    assert term.wait_screen("cli_temp_hi ="), "^O did not open the override field"
    term.send(keys.CTRL_U + b"33.5" + keys.ENTER)
    assert term.wait_screen("33.5"), "the override did not reach the OVERRIDE column"
    assert term.wait_screen("1 unsaved"), "the legend did not count the unsaved override"

    # ^V flips what the force READS as, without touching the value it holds.
    term.send(keys.CTRL_V)
    assert term.wait_screen("override now reads invalid"), "^V did not flip the validity"
    assert term.wait_screen("forced invalid"), "the panel still calls the force good"
    term.send(keys.CTRL_V)
    assert term.wait_screen("override now reads valid"), "^V did not flip it back"
    assert term.wait_screen("33.5"), "the flip moved the forced value"

    term.send(keys.CTRL_S)
    assert term.wait_screen("override saved"), "^S did not reach storage"
    assert term.wait_screen("1 unsaved", present=False), "the row is still owed to flash"

    term.send(keys.CTRL_R)
    assert term.wait_screen("override released"), "^R did not release it"
    assert term.wait_screen("33.5", present=False), "the forced value is still on the row"


def test_tasks_page_reports_program_health(spawn):
    """HEALTH is a program's verdict, and blank on everything else.

    The blank half is the claim worth pinning: `Task::program` is null on a
    driver or a service task, and a page that read health off it anyway would
    fault, not misprint.
    """
    term = boot(spawn)
    goto(term, keys.ALT_7, "TASK")

    # The site's programs head the table, the library's sys_ tasks follow. By
    # name alone sys_alarms would come first, so this is the ordering, not the
    # alphabet.
    def names():
        rows = [l for l in term.view() if l.startswith("  | ")]
        return [n for n in (r.split("|")[1].strip() for r in rows) if n and n != "TASK"]
    assert term.until(lambda: len(names()) > 3), "the table never painted"
    assert not any(n.startswith("sys_") for n in names()), \
        "a library task sits above the site's programs: %s" % names()

    term.send(keys.text("cooling"))
    assert term.wait_screen("[/cooling]"), "the filter did not narrow the table"
    assert term.wait_screen("HEALTHY"), "a control program reported no health"
    # The period left the table for the panel, beside the share it takes.
    assert term.wait_screen("Every ms"), "the panel is missing the declared period"

    term.send(keys.ESC)
    # Wait for the clear before typing: ESC plus a letter IS Alt+letter to any
    # VT parser, so a byte sent inside the command timeout joins the escape.
    assert term.wait_screen("type to filter"), "ESC did not clear the filter"
    term.send(keys.text("sys_console_rx"))
    assert term.wait_screen("[/sys_console_rx]"), "the filter did not narrow the table"
    # Synced on PHASE, the cell AFTER health: the band repaints left to right, so
    # a row whose phase has arrived has already had its health cell written.
    def row():
        return next((l for l in term.view() if "sys_console_rx" in l and "input" in l), None)
    assert term.until(lambda: row() is not None), "the row never finished painting"
    assert "HEALTHY" not in row(), f"a non-program claimed a health: {row()!r}"


def test_network_comes_up(spawn):
    """The DNS -> NTP -> MQTT handshake finishes, per the firmware's own report.

    Asked with `net`, not watched for in the boot log: those lines are transient
    in a 7-row tail that alarm churn scrolls past, so on a busy console they go
    unpainted on a firmware whose network is healthy. `net` prints current state
    on demand, which is a question with an answer.
    """
    term = shell(spawn, quiet_logs=True)
    term.send(b"net" + keys.ENTER)
    # The report's LAST row, so the wait covers the whole of it: at 4800 baud a
    # dozen rows take several frames, and syncing on the first one reads a
    # half-painted answer.
    assert term.wait_screen("MQTT broker"), "`net` did not finish printing"
    report = "\n".join(term.screen.display)
    for want in ("online", "synced", "ready"):
        assert want in report, f"network not up: no '{want}' in\n{report}"


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
    # this exists to catch. Last moved when the three supply watches replaced
    # supply_watch and supply_report, the local input scan arrived, and the
    # rot guard was folded back into the humidity ladder, the two watch
    # programs arrived, irrigation became three programs instead of one, and
    # the sun tracker moved inside a watch program of its own.
    assert term.wait_for("Registered ....... : 43"), \
        "the task table changed size - a registration was added or silently lost"
    assert term.wait_for("0 refused"), "a registration was refused: MAX_TASKS"


def test_a_named_task_answers_for_itself(spawn):
    """`tasks get <name>` is the decisive per-task check (design/scan-cycle-rework.md).

    One from each phase, so a whole phase going missing is caught by name and
    not only by the count above.
    """
    term = shell(spawn, quiet_logs=True)
    for name, phase in (("sys_console_rx", "input"), ("sys_params", "config"),
                        ("sys_health", "service"), ("dc_watch", "control"),
                        ("sys_mbrtu_tx", "output"), ("sys_console_tx", "present")):
        term.send(b"tasks get " + name.encode() + keys.ENTER)
        assert term.wait_for(phase), "no task named %s in %s" % (name, phase)
