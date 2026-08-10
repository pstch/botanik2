"""Shell built-ins, driven through the real console - nothing else covers dispatch.

`set` edits a parameter; `override` is the other half, forcing any variable for
maintenance.

Own sim per test: `set` and `override` both write, and a persisted override would
outlive the test.
"""

import keys
from helpers import shell


def _run(term, line):
    term.send(line + keys.ENTER)


def test_get_and_set(spawn):
    term = shell(spawn, quiet_logs=True)
    _run(term, b"help")
    assert term.wait_for(("read a variable", "list variables", "edit a parameter")), \
        "help did not list get/list/set"

    _run(term, b"get ntp_srv1")
    assert term.wait_for("ntp_srv1"), "get <name> found nothing"

    _run(term, b"set ntp_poll_sec 900")
    assert term.wait_for("ntp_poll_sec = 900"), "set did not apply"
    _run(term, b"get ntp_poll_sec")
    assert term.wait_for("900"), "set did not round-trip through get"

    _run(term, b"set nosuchthing 1")
    assert term.wait_for("unknown variable"), "unknown name not reported"


def test_set_refuses_a_non_parameter(spawn):
    term = shell(spawn, quiet_logs=True)
    _run(term, b"set grid_L1_V 10")
    assert term.wait_for("not a parameter"), "set accepted a sensor"
    assert term.wait_for("override"), "set did not point at override"


def test_override(spawn):
    term = shell(spawn, quiet_logs=True)
    _run(term, b"override list")
    assert term.wait_for("none active"), "fresh boot reported an override"

    _run(term, b"override grid_L1_V 123.5")
    assert term.wait_for("forced to"), "override did not take"
    # Both halves on rows of their own: what it is held at, and what it reads
    # underneath - "has the sensor come back?" is the question behind releasing
    # an override.
    _run(term, b"get grid_L1_V")
    assert term.wait_for(("Override value", "123.5", "Raw value")), \
        "get did not show the override over the live reading"

    _run(term, b"override list")
    assert term.wait_for("override(s) active"), "override list missing"

    # Forcing a variable to read as a dead sensor is a separate job from forcing
    # its value, and both have to be sayable.
    _run(term, b"override grid_L2_V 99 invalid")
    assert term.wait_for("reading as invalid"), "validity was not honoured"

    _run(term, b"override grid_L1_V off")
    assert term.wait_for("override off"), "override was not released"

    # `override clear` releases the rest without walking the list, which is how a
    # maintainer hands the machine back.
    _run(term, b"override clear")
    assert term.wait_for("cleared"), "override clear did not report"
    _run(term, b"override list")
    assert term.wait_for("none active"), "override still listed after clear"


def test_control(spawn):
    """All three modes report, and each one STICKS.

    The report is checked with wait_screen, not wait_for: the mode word also
    appears in the reply to the command that SET it, and a latched wait_for would
    match that and pass without the state having stuck at all. The reply spells
    it upper-case and the report lower-case, which is what makes them tellable
    apart on one screen.
    """
    term = shell(spawn, quiet_logs=True)
    _run(term, b"control")                       # bring-up hands control over
    assert term.wait_for("control: enabled"), "control not enabled after boot"

    for mode, reported in (("inhibit", "inhibited"),
                           ("disable", "disabled"),
                           ("enable",  "enabled")):
        _run(term, b"control " + mode.encode())
        _run(term, b"control")
        assert term.wait_screen("control: " + reported), \
            "control %s did not stick (wanted %r on screen)" % (mode, reported)


def test_control_inhibit_leaves_an_actuator_commandable(spawn):
    """`control inhibit` stops the control PROGRAMS, not you: with the scan
    stopped an override is the only thing that can move an actuator, which is the
    whole reason to type it before working on the plant.

    `control disable` is the stricter mode and stops that too - but it MUTES the
    override rather than clearing it, so the forced value is still on the row and
    comes back when control does. Only the SYSTEM withholding control erases one.
    """
    term = shell(spawn, quiet_logs=True)
    _run(term, b"control inhibit")
    assert term.wait_for("by hand"), "control inhibit did not mention overrides"

    _run(term, b"override cli_screen 1")
    assert term.wait_for("forced to"), "an output could not be forced by hand"
    _run(term, b"get cli_screen")
    assert term.wait_screen("Override value"), "the forced output did not stick"

    _run(term, b"control disable")
    assert term.wait_for("ignored"), "control disable did not say overrides are ignored"
    _run(term, b"get cli_screen")
    assert term.wait_screen("Override value"), \
        "disable cleared the override instead of muting it"


def test_list_filters_by_prefix(spawn):
    """Variables are named by domain, so a prefix is the subsystem view."""
    term = shell(spawn, quiet_logs=True)
    _run(term, b"list grid_L1_")
    assert term.wait_for("grid_L1_V"), "prefix list missed a matching variable"

    _run(term, b"list zzz_nothing")
    assert term.wait_for("nothing matches"), "a prefix matching nothing said otherwise"

    _run(term, b"get")                           # `get` is strictly one variable
    assert term.wait_for("usage: get"), "bare get did not show usage"


def test_features(spawn):
    """What the binary IS - an absent subsystem is otherwise indistinguishable
    from a broken one over the terminal.

    The reference site enables every flag in the roster, so the off partition is
    empty by construction; asserting it prints "(none)" is what still proves both
    partitions render.
    """
    term = shell(spawn, quiet_logs=True)
    _run(term, b"features")
    assert term.wait_for(("console", "mqtt", "sun_tracker",
                          "modbus_rtu_client", "modbus_tcp_server")), \
        "features did not list the site's enabled subsystems"
    assert term.wait_for("off : (none)"), "the off partition did not render"


def test_tasks(spawn):
    """The beat's totals - `stats` gives its CPU with no attribution.

    A summary in `stats`' shape, not a listing: the TASKS page is the table, and
    22 rows over a throttled 4800-baud console is an index of a page. Wants
    `quiet_logs`, since alarm churn competes with the reply for that console.

    Presence, not order - `wait_for` is a latched set. `test_tasks.cpp` pins the
    dispatch order, and the missed count it reports.
    """
    term = shell(spawn, quiet_logs=True)
    _run(term, b"tasks")
    # The four markers belong to the summary and to no per-task row, which is
    # what asserts this is not a listing. The table's absence cannot be checked
    # directly: it would have scrolled off a 24-row screen either way.
    assert term.wait_for(("Registered", "Missed", "Peaks add to", "TASKS page")), \
        "tasks did not render the beat summary"

    _run(term, b"tasks expire_stale")
    assert term.wait_for(("input (bkgnd", "Activations", "missed", "Cost")), \
        "tasks <name> did not detail the task"

    _run(term, b"tasks nosuchtask")
    assert term.wait_for("no task named"), "an unknown task name said otherwise"
