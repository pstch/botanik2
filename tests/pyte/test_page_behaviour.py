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

    # Every library task a full build registers. Raise it deliberately when
    # adding one; a DROP is the bug this exists to catch.
    assert term.wait_for("Registered ....... : 23"), \
        "the task table changed size - a registration was added or silently lost"
    assert term.wait_for("0 refused"), "a registration was refused: MAX_TASKS"


def test_a_named_task_answers_for_itself(spawn):
    """`tasks get <name>` is the decisive per-task check (design/scan-cycle-rework.md).

    One from each phase, so a whole phase going missing is caught by name and
    not only by the count above.
    """
    term = shell(spawn, quiet_logs=True)
    for name, phase in (("console_in", "input"), ("params", "config"),
                        ("health", "service"), ("supply_watch", "control"),
                        ("modbus_rtu_tx", "output"), ("console_tx", "present")):
        term.send(b"tasks get " + name.encode() + keys.ENTER)
        assert term.wait_for(phase), "no task named %s in %s" % (name, phase)
