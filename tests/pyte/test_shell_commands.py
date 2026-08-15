"""Shell built-ins, driven through the real console - nothing else covers dispatch.

The commands are noun-verb: `params` edits the persisted settings, `overrides`
forces any variable for maintenance, `vars` reads the whole table. Editing and
committing are separate verbs (`set` then `save`), which is what these assert.

Own sim per test: the write commands all leave state, and a saved override would
outlive the test.
"""

import keys
from helpers import shell


def _run(term, line):
    term.send(line + keys.ENTER)


def test_get_and_set(spawn):
    term = shell(spawn, quiet_logs=True)
    _run(term, b"help tables")
    assert term.wait_for(("params", "overrides", "vars", "alarms")), \
        "help tables did not list the table commands"

    _run(term, b"vars get ntp_srv1")
    assert term.wait_for("ntp_srv1"), "vars get <name> found nothing"

    _run(term, b"params set ntp_poll_sec 900")
    assert term.wait_for("ntp_poll_sec = 900"), "params set did not apply"
    # Applied is not saved: the reply has to say so, since nothing else does.
    assert term.wait_for("unsaved"), "params set did not flag the edit as unsaved"
    _run(term, b"params get ntp_poll_sec")
    assert term.wait_for("900"), "params set did not round-trip through get"

    _run(term, b"params set nosuchthing 1")
    assert term.wait_for("unknown parameter"), "unknown name not reported"


def test_params_save_commits_the_edit(spawn):
    """The second half of an edit: live is not durable until `params save`."""
    term = shell(spawn, quiet_logs=True)
    _run(term, b"params set ntp_poll_sec 900")
    assert term.wait_for("unsaved"), "the edit did not report as unsaved"

    _run(term, b"params save ntp_poll_sec")
    assert term.wait_for("ntp_poll_sec saved"), "params save did not commit the row"

    # Nothing left owed, and the command says so rather than silently doing
    # nothing - "did that work?" is the question a silent reply leaves.
    _run(term, b"params save all")
    assert term.wait_for("nothing to save"), "params save did not report an empty commit"


def test_params_set_refuses_a_non_parameter(spawn):
    term = shell(spawn, quiet_logs=True)
    _run(term, b"params set grid_L1_V 10")
    assert term.wait_for("not a parameter"), "params set accepted a sensor"
    assert term.wait_for("overrides set"), "params set did not point at overrides"


def test_override(spawn):
    term = shell(spawn, quiet_logs=True)
    _run(term, b"overrides list")
    assert term.wait_for("none active"), "fresh boot reported an override"

    _run(term, b"overrides set grid_L1_V 123.5")
    assert term.wait_for("forced to"), "overrides set did not take"
    # Both halves on rows of their own: what it is held at, and what it reads
    # underneath - "has the sensor come back?" is the question behind releasing
    # an override.
    _run(term, b"vars get grid_L1_V")
    assert term.wait_for(("Override value", "123.5", "Raw value")), \
        "vars get did not show the override over the live reading"

    _run(term, b"overrides list")
    assert term.wait_for("override(s) active"), "overrides list missing"

    # Forcing a variable to read as a dead sensor is a separate job from forcing
    # its value, and both have to be sayable.
    _run(term, b"overrides set grid_L2_V 99 invalid")
    assert term.wait_for("reading as invalid"), "validity was not honoured"

    _run(term, b"overrides clear grid_L1_V")
    assert term.wait_for("override cleared"), "override was not released"

    # `overrides clear` releases the rest without walking the list, which is how
    # a maintainer hands the machine back.
    _run(term, b"overrides clear all")
    assert term.wait_for("cleared"), "overrides clear did not report"
    _run(term, b"overrides list")
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
    _run(term, b"control status")                # bring-up hands control over
    assert term.wait_for("control: enabled"), "control not enabled after boot"

    for mode, reported in (("inhibit", "inhibited"),
                           ("disable", "disabled"),
                           ("enable",  "enabled")):
        _run(term, b"control " + mode.encode())
        _run(term, b"control status")
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

    _run(term, b"overrides set cli_screen 1")
    assert term.wait_for("forced to"), "an output could not be forced by hand"
    _run(term, b"vars get cli_screen")
    assert term.wait_screen("Override value"), "the forced output did not stick"

    _run(term, b"control disable")
    assert term.wait_for("ignored"), "control disable did not say overrides are ignored"
    _run(term, b"vars get cli_screen")
    assert term.wait_screen("Override value"), \
        "disable cleared the override instead of muting it"


def test_list_filters_by_prefix(spawn):
    """Variables are named by domain, so a prefix is the subsystem view."""
    term = shell(spawn, quiet_logs=True)
    _run(term, b"vars list grid_L1_")
    assert term.wait_for("grid_L1_V"), "prefix list missed a matching variable"

    _run(term, b"vars list zzz_nothing")
    assert term.wait_for("nothing matches"), "a prefix matching nothing said otherwise"

    _run(term, b"vars get")                      # `get` is strictly one variable
    assert term.wait_for("usage: vars get"), "bare vars get did not show usage"


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
    _run(term, b"tasks stats")
    # The four markers belong to the summary and to no per-task row, which is
    # what asserts this is not a listing. The table's absence cannot be checked
    # directly: it would have scrolled off a 24-row screen either way.
    assert term.wait_for(("Registered", "Missed", "Peaks add to", "TASKS page")), \
        "tasks did not render the beat summary"

    _run(term, b"tasks get expire_stale")
    assert term.wait_for(("input (bkgnd", "Activations", "missed", "Cost")), \
        "tasks get <name> did not detail the task"

    _run(term, b"tasks get nosuchtask")
    assert term.wait_for("no task named"), "an unknown task name said otherwise"

    # A listing exists too, prefix-filtered, for finding the name to detail.
    _run(term, b"tasks list site_")
    assert term.wait_for(("site_", "task(s)")), "tasks list did not filter by prefix"


def test_params_clear_reverts_to_the_default(spawn):
    """`params clear` is the way back: live value AND stored copy."""
    term = shell(spawn, quiet_logs=True)
    _run(term, b"params set ntp_poll_sec 900")
    assert term.wait_for("ntp_poll_sec = 900"), "params set did not apply"
    _run(term, b"params save ntp_poll_sec")
    assert term.wait_for("ntp_poll_sec saved"), "params save did not commit"

    _run(term, b"params clear ntp_poll_sec")
    assert term.wait_for("cleared to its default"), "params clear did not report"

    _run(term, b"params get ntp_poll_sec")
    assert term.wait_for("3600"), \
        "params clear did not put the factory default back (BOTANIK_NTP_POLL_SEC)"

    # The mark went with the value: nothing is owed to storage after a clear.
    _run(term, b"params save all")
    assert term.wait_for("nothing to save"), "params clear left the row marked unsaved"

    _run(term, b"params clear nosuchthing")
    assert term.wait_for("unknown parameter"), "unknown name not reported"


def test_programs_list_and_gate(spawn):
    """A program can be taken out of the scan by hand, and says so until saved."""
    term = shell(spawn, quiet_logs=True)
    _run(term, b"programs list")
    assert term.wait_for("program(s)"), "programs list printed no roll-call"
    assert term.wait_for("enabled"), "programs list did not show a state column"

    _run(term, b"programs disable nosuchprogram")
    assert term.wait_for("unknown program"), "unknown program not reported"


def test_programs_disable_is_live_then_saved(spawn):
    term = shell(spawn, quiet_logs=True)
    # Take the first program out by name, whatever the site calls it.
    _run(term, b"programs disable all")
    assert term.wait_for("disabled"), "programs disable all did not report"
    assert term.wait_for("unsaved"), "disable did not flag the change as unsaved"

    _run(term, b"programs list")
    assert term.wait_for("DISABLED"), "programs list did not show the new state"

    _run(term, b"programs save all")
    assert term.wait_for("saved"), "programs save did not commit"

    _run(term, b"programs save all")
    assert term.wait_for("nothing to save"), "programs save did not report an empty commit"

    _run(term, b"programs enable all")
    assert term.wait_for("enabled"), "programs enable did not report"


def test_a_bare_verb_never_means_all(spawn):
    """Applying to every row is only ever spelled `all`: a bare verb is a typo."""
    term = shell(spawn, quiet_logs=True)
    for line in (b"params save", b"params clear", b"overrides save",
                 b"overrides clear", b"programs save", b"programs disable"):
        _run(term, line)
        assert term.wait_for("usage:"), f"{line!r} did not refuse a missing target"


def test_help_is_a_summary_of_groups(spawn):
    """`help` names the GROUPS only - the command list ran past a 25-row screen
    once the tables grew verbs of their own."""
    term = shell(spawn, quiet_logs=True)
    _run(term, b"help")
    assert term.wait_for(("Main commands", "Topics", "system", "status", "tables")), \
        "help did not show the main commands and the topics"
    # Neither the commands nor their verbs belong in the summary.
    assert not term.wait_for("the editable settings"), \
        "help listed a group's commands instead of summarising"
    assert not term.wait_for("set <name> <value>"), \
        "help printed a verb list"

    _run(term, b"help tables")
    assert term.wait_for(("params ...", "overrides ...", "vars ...")), \
        "help <topic> did not list the topic, marked for their verbs"

    _run(term, b"help nope")
    assert term.wait_for(("no topic", "Topics")), \
        "an unknown topic did not say so and fall back to the help page"


def test_a_noun_answers_its_own_usage(spawn):
    """Bare, or with `help`: same answer, so the detail is one word away."""
    term = shell(spawn, quiet_logs=True)
    for noun, marker in ((b"overrides", "overrides clear <name>|all"),
                         (b"params",    "params clear <name>|all"),
                         (b"programs",  "programs disable <name>|all"),
                         (b"vars",      "vars get <name>"),
                         (b"alarms",    "alarms ack <code|name|all>"),
                         (b"tasks",     "tasks get <name>"),
                         (b"modbus",    "modbus devices"),
                         (b"logs",      "logs status"),
                         (b"control",   "control inhibit")):
        _run(term, noun)
        assert term.wait_for(("usage:", marker)), f"bare {noun!r} did not print its usage"
        _run(term, noun + b" help")
        assert term.wait_for(("usage:", marker)), f"{noun!r} help did not print its usage"


def test_report_rows_never_wrap(spawn):
    """A wrapped row breaks the label column for every row under it."""
    term = shell(spawn, quiet_logs=True)
    for cmd in (b"tasks stats", b"stats"):
        _run(term, cmd)
        term.wait_for("botanik>")
        for row in term.screen.display:
            # The wrap continuation marker the shell page emits mid-output.
            assert not row.startswith("> "), f"{cmd!r} wrapped a row: {row!r}"


def test_stats_reports_the_watchdog(spawn):
    """A withheld kick is invisible until the unit reboots, so the loop's
    distance from its own reset belongs in `stats`."""
    term = shell(spawn, quiet_logs=True)
    _run(term, b"stats")
    assert term.wait_for(("Watchdog", "last kick")), \
        "stats did not report the watchdog"


def test_log_writes_an_operator_entry(spawn):
    """The shift's own note, beside what the firmware recorded itself."""
    term = shell(spawn)
    _run(term, b"log notice valve 3 serviced")
    assert term.wait_for(("USER", "valve 3 serviced")), \
        "log did not write the entry on its own topic"

    # A level is required: a bare `log` must not swallow the text as one.
    _run(term, b"log")
    assert term.wait_for("usage:"), "bare log did not print its usage"
    _run(term, b"log nosuchlevel hello")
    assert term.wait_for("usage:"), "an unknown level was not refused"


def test_reboot_announces_itself_in_the_log(spawn):
    """The reply IS the log entry: a unit that reset is worth accounting for,
    and CRITICAL reaches the screen even with `logs off`."""
    term = shell(spawn, quiet_logs=True)
    _run(term, b"reboot")
    assert term.wait_for(("HEALTH", "reboot requested", "kick withheld")), \
        "reboot did not log its own announcement"
    # One acknowledgement, not two: the printed line would say it again.
    assert not term.wait_for("resetting shortly"), \
        "reboot printed a line beside the log entry"


def test_sink_log_floor_is_a_live_parameter(spawn):
    """A sink's floor is a parameter, and it lands on the next entry.

    The SHELL page's own floor, moved from the SHELL page - which is what makes
    this worth a test: there is no serial to bump, so a `params set` must reach
    the writer through the CONFIG pass before the next `log` command is
    formatted.
    """
    # Logs ON: `logs off` would silence this sink whatever its floor says, which
    # is the other half of the point - the two are different mechanisms.
    term = shell(spawn)
    _run(term, b"params set console_shell_level error")
    assert term.wait_for("console_shell_level = error"), "the floor did not take"

    # Below the floor: the entry is forwarded by the logger and refused by this
    # sink, so it must not reach the screen. Asserted on the RENDERED entry
    # ("USER: ...") rather than the marker alone - the shell echoes the command
    # that produced it, so the bare word is on screen either way.
    _run(term, b"log info below-the-floor")
    _run(term, b"log error above-the-floor")
    assert term.wait_for("USER: above-the-floor"), "an entry at the floor was dropped"
    # The one above arrived after it, so the one below has had its chance.
    assert term.wait_screen("USER: below-the-floor", present=False), \
        "an entry below the sink's floor was shown anyway"
