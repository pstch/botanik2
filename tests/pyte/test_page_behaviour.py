"""Whole-firmware behaviour a unit test cannot reach: the lifecycle guard, the
network state machine, and the pages over a real task table.

Named rows are the library's own, so any full-profile site passes. What a site's
table decides is that site's to test, beside it.

One file, because a CTest entry costs an interpreter startup. Own sim per test.
"""

import keys
from helpers import boot, goto, shell


def test_a_bare_lifecycle_verb_refuses(spawn):
    """The guard on the real firmware, where the health monitor IS wired.

    The unit test has none, so it can only prove the guard passes; only here can
    a bare `halt` be shown not to stop the plant. Deliberately the verb that
    would take this console with it.
    """
    term = shell(spawn, quiet_logs=True)
    term.send(b"halt" + keys.ENTER)
    assert term.wait_screen("halt confirm"), \
        "halt did not say what to type\n%s" % term.snapshot()

    # The console is still here, which is the actual assertion: `halt` stops the
    # cycle, so a screen that answers at all is a plant that was not parked.
    term.send(b"version" + keys.ENTER)
    assert term.wait_screen(") site "), \
        "the shell stopped answering after a bare halt\n%s" % term.snapshot()


def test_a_wildcard_filter_narrows_a_table(spawn):
    """A `*`/`?` pattern narrows a table where a substring cannot.

    On MONITOR, where the names an operator knows come in groups:
    `sol_from???_min` takes the sunset count and not the sunrise one, which no
    substring of those names selects.
    """
    term = boot(spawn)
    goto(term, keys.ALT_6, "VARIABLE")

    def names():
        rows = [l for l in term.view() if l.startswith("  | ")]
        cells = [r.split("|") for r in rows]
        return [c[2].strip() for c in cells
                if len(c) > 2 and c[2].strip() and c[2].strip() != "VARIABLE"]

    def narrowed():
        return names() == ["sol_fromSet_min"]

    term.send(keys.text("sol_from???_min"))
    assert term.wait_screen("[/sol_from???_min]"), "the filter did not reach the legend"
    # Waited on rather than asserted once: the legend is PRIO_CHROME and goes out
    # a frame ahead of the band it describes.
    assert term.until(narrowed), "the table did not narrow to the pattern: %s" % names()


def test_tasks_page_filters_by_health(spawn):
    """LEFT/RIGHT narrow the table to one HEALTH code.

    The view selects on the same verdict the column prints, so the badge and
    the column cannot disagree. HEALTHY is the one code a booted unit is
    certain to have rows under; the empty views are asserted through the badge
    rather than through a count, since which of the other two is populated is a
    property of the plant.
    """
    term = boot(spawn)
    goto(term, keys.ALT_7, "TASK")

    # Tolerant of a half-painted row: the band goes out left to right at 4800
    # baud, so a line can be on screen before its HEALTH cell is.
    def cells(col):
        out = []
        for l in term.view():
            if not l.startswith("  | "):
                continue
            parts = l.split("|")
            if len(parts) <= col or parts[1].strip() in ("", "TASK"):
                continue
            out.append(parts[col].strip())
        return out

    assert term.until(lambda: len([n for n in cells(1) if n]) > 3), \
        "the table never painted"
    assert term.wait_screen("[ALL]"), "the view badge is missing"

    term.send(keys.RIGHT)
    assert term.wait_screen("[HEALTHY]"), "RIGHT did not step the view"
    # Every row the view keeps says the code the badge does, and there is at
    # least one: the console's own tasks are healthy on a unit that booted.
    assert term.until(lambda: cells(3) and set(cells(3)) == {"HEALTHY"}), \
        "the HEALTHY view kept a row that is not: %s" % cells(3)

    term.send(keys.LEFT)
    assert term.wait_screen("[ALL]"), "LEFT did not step back"


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
