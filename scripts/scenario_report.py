#!/usr/bin/env python3
"""What the control system did, out of the suite that watched it do it.

Runs the SCENARIO cases with a trace enabled (tests/scenario_trace.h), and turns
what they recorded into a document: the weather each one held, what it asserted,
and the plant's rows at each moment it stopped to look.

Nothing here decides what happened - the numbers are the ones the assertions
were made against, so the document cannot claim a behaviour the suite is not
holding the firmware to. A case that fails writes no report.

Markdown always; a PDF as well when reportlab imports, through the same
renderer scripts/vars_report.py uses. Neither output is part of the build.
"""

import argparse
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vars_csv
import vars_report
from vars_extract import FILES

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The rows the trace carries, in the order the document shows them, grouped the
# way an operator reads them: what it measured, what it decided, what it moved,
# and what was allowed to overrule all three.
GROUPS = [
    ("What it measured", [
        "cli_temp_in", "cli_temp_ext", "cli_temp_cs", "cli_rh_hi",
        "cli_dpMargin_degC", "cli_targetTemp_degC", "cli_phase",
    ]),
    ("What it decided", [
        "cli_coolDem", "cli_stratDem", "cli_humidDem", "cli_rotVentDem",
        "cli_windowDem",
    ]),
    ("What it moved", [
        "cli_window1_pos", "cli_screen", "cli_screenMoving", "cli_intr1",
        "cli_fanSpeed", "cli_skirtDuty", "cli_skirtValve",
    ]),
    ("What was allowed", [
        "cli_intakeCoolOk", "cli_groundHeatOk", "cli_intakeDryOk",
        "cli_rainLockout", "cli_windLockout", "sys_supply_lockout",
        "alarm_rot", "alarm_too_hot", "alarm_too_cold", "alarm_sns_ceiling",
    ]),
]


def descriptions(site):
    """Each row's own description, from the tables, so the document names a row
    the way the PARAMS page does rather than by its identifier alone."""
    out = {}
    table_dir = os.path.join(ROOT, "src", "sites", site, "tables")
    for fname in FILES:
        path = os.path.join(table_dir, fname)
        if not os.path.exists(path):
            continue
        for r in vars_csv.read(path):
            if r.get("name") and r.get("desc"):
                out[r["name"]] = r["desc"]
    return out


def run_trace(binary, cases):
    """The suite, with the trace on, into a file it then reads back."""
    fd, path = tempfile.mkstemp(prefix="botanik-scenarios-", suffix=".txt")
    os.close(fd)
    env = dict(os.environ, BOTANIK_SCENARIO_TRACE=path)
    p = subprocess.run([binary, "-tc=" + cases], env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    text = open(path, encoding="utf-8").read()
    os.unlink(path)
    if p.returncode != 0:
        sys.stderr.write(p.stdout.decode("utf-8", "replace"))
        raise SystemExit("the scenarios did not pass: no report to write")
    return text


def parse(text):
    """The trace, as a list of scenarios. Notes buffer until the moment they
    were made at, because a case says what it is asserting and only then looks
    at the plant."""
    scenarios, pending = [], []
    for line in text.split("\n"):
        if not line:
            continue
        tag, _, rest = line.partition("|")
        if tag == "S":
            name, _, about = rest.partition("|")
            scenarios.append({"name": name, "about": about,
                              "weather": [], "moments": []})
            pending = []
        elif not scenarios:
            continue
        elif tag == "W":
            k, _, v = rest.partition("|")
            scenarios[-1]["weather"].append((k, v))
        elif tag == "N":
            pending.append(rest)
        elif tag == "M":
            scenarios[-1]["moments"].append(
                {"name": rest, "notes": pending, "rows": []})
            pending = []
        elif tag == "V" and scenarios[-1]["moments"]:
            name, _, tail = rest.partition("|")
            value, _, quality = tail.partition("|")
            scenarios[-1]["moments"][-1]["rows"].append((name, value, quality))
    return scenarios


def cell(value, quality):
    """A value, and what the plant thought of it. Only ever shown when there is
    something to say - a good reading says nothing - and it is the REASON that
    is worth the column width, since "invalid" is already implied by there
    being a note at all."""
    why = quality.partition(":")[2].strip() or quality
    return value + (f" ({why})" if quality else "")


def sentence(text):
    """A case writes its moments in running prose; a heading starts upright."""
    return text[:1].upper() + text[1:]


def scenario_doc(s, desc):
    """One scenario: what it held, what it asserted, and what the plant read."""
    out = [f"## {s['name']}", "", s["about"], "",
           "**The weather it held.** " +
           ", ".join(f"{k} {v}" for k, v in s["weather"]) + ".", ""]

    if not s["moments"]:
        return out

    # Numbered only when there is more than one: a single moment has nothing
    # to be told apart from, and the table below then heads its one column
    # "value" rather than "1".
    many = len(s["moments"]) > 1
    out += ["### What happened", ""]
    for i, m in enumerate(s["moments"], 1):
        lead = f"{i}. " if many else ""
        out += [f"**{lead}{sentence(m['name'])}**", ""]
        out += [f"- {n}" for n in m["notes"]] + [""]

    # One column per moment, NUMBERED against the list above: a scenario is
    # about what MOVED, and a row read at three points in one line is where
    # that shows - which a header carrying three sentences would not.
    head = ["what it is", "row"] + (
        [str(i + 1) for i in range(len(s["moments"]))] if many else ["value"])
    at = [{n: (v, q) for n, v, q in m["rows"]} for m in s["moments"]]
    for title, names in GROUPS:
        body = []
        for n in names:
            if not any(n in a for a in at):
                continue
            body.append([desc.get(n, n), f"`{n}`"] +
                        [cell(*a[n]) if n in a else "-" for a in at])
        if body:
            out += [f"### {title}", ""] + vars_report.md_table(head, body)
    return out


def document(site, scenarios):
    desc = descriptions(site)
    out = [f"# {site}: the control system, through ten kinds of weather", "",
           "GENERATED by `scripts/scenario_report.py`, from the trace the "
           "scenario cases in `tests/test_site_scenarios.cpp` write while they "
           "run. Every number below is one an assertion was made against.", "",
           "## What is being driven", "",
           "The whole site, in the order the firmware registers it: the "
           "instruments, then the ladders that read their rows, then the "
           "actuators that read the demands. One `Vars::Bank` behind all of "
           "them, stepped at the site's own period, exactly as the firmware "
           "steps it. No program holds a reference to another - what one "
           "decides reaches the next through the table, so what a scenario "
           "changes is the WEATHER and everything after that is the control "
           "system's own doing.", "",
           "Two programs are deliberately absent. `sun_watch` needs a fix and a "
           "wall clock, and `day_phase` would then follow whichever hour the "
           "suite happened to run at, so the phase is an input here and the two "
           "have cases of their own.", "",
           "A field row that stops being refreshed goes stale, so each case "
           "holds its weather every beat: that is what makes a probe DYING a "
           "thing a scenario can do simply by stopping.", "",
           f"{len(scenarios)} scenarios.", ""]
    out += vars_report.md_table(
        ["scenario", "what it asks"],
        [[s["name"], s["about"]] for s in scenarios])

    for s in scenarios:
        out += [vars_report.PAGE_BREAK, ""] + scenario_doc(s, desc)
    out += [vars_report.PAGE_BREAK, ""] + CLOSING
    return "\n".join(out) + "\n"


# The one part of the document that is not derived from the trace: what the set
# as a whole does and does not reach. Kept here rather than in a scenario,
# because it is a statement about the SUITE and no single case can make it.
CLOSING = [
    "## What the set reaches", "",
    "Taken together the ten cases exercise every tier of the arbitration the "
    "plant actually has, in the order the plant resolves it:", "",
    "- **The safe state**, raised by the supply lockout and released with it. "
    "Every program is asked to park through the same machinery a halt uses, "
    "and each one drives its OWN actuators to the position its table row "
    "declares - the fabric out, the fans at speed 1, the skirt shut.",
    "- **The weather interlocks**, which own the roof outright: rain shuts it "
    "while the cooling ladder is still asking for it wide open, and the "
    "lockout outlives the shower that raised it. The gust interlock is the "
    "same path and the same row, tested where a case can state a wind speed.",
    "- **The permissions**, which are not interlocks but gates on a demand: "
    "the intake gates on the air outside being worth drawing, the screen on "
    "the phase of the day, the skirt on the air being dry enough for a wet "
    "pad to cool rather than only wet.",
    "- **The ladders themselves**, both halves of each: cooling and ground "
    "heat, humidify and dehumidify, and the staging that turns one demand "
    "into a roof position, three fan speeds and a valve duty.", "",
    "Three failure modes are covered beside them: an instrument dying (the "
    "plant carries on from the level that still answers, and says it is "
    "working on a reading it does not have), a loop held against its limit for "
    "ten minutes (which costs no recovery time it did not need), and a reading "
    "parked exactly on a threshold with noise either side of it (which moves "
    "nothing at all).", "",
    "## What it does not reach", "",
    "- **The sun.** `sun_watch` needs a fix and a wall clock; the day phase is "
    "an input here and the two programs have cases of their own.",
    "- **Irrigation.** The programs run in every scenario - they are part of "
    "the same plant and would show if they misbehaved - but no case here "
    "drives a cycle: irrigation answers a clock and a tray, not the weather, "
    "and it is covered where that is what a case can state.",
    "- **The field bus.** The rows are published and read directly, so what is "
    "under test is the control system and not the Modbus driver that fills "
    "those rows on hardware.", "",
]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--site", default="gh_mlb41")
    ap.add_argument("--binary", default=os.path.join(ROOT, "build", "tests",
                                                     "botanik_tests"))
    ap.add_argument("--cases", default="scenario*")
    ap.add_argument("--out", help="where to write (default: docs/generated/)")
    ap.add_argument("--no-pdf", action="store_true")
    a = ap.parse_args()

    if not os.path.exists(a.binary):
        raise SystemExit(f"{a.binary}: build it first (make -C build botanik_tests)")

    scenarios = parse(run_trace(a.binary, a.cases))
    if not scenarios:
        raise SystemExit("the suite wrote no trace: is scenario_trace.h wired in?")

    md = document(a.site, scenarios)
    base = a.out or os.path.join(ROOT, "docs", "generated",
                                 f"{a.site}_scenarios")
    os.makedirs(os.path.dirname(base), exist_ok=True)
    open(base + ".md", "w", encoding="utf-8").write(md)
    print(f"  {os.path.relpath(base + '.md', ROOT)}: written")
    title = f"{a.site}: the control system, through ten kinds of weather"
    if not a.no_pdf and vars_report.to_pdf(md, base + ".pdf", title):
        print(f"  {os.path.relpath(base + '.pdf', ROOT)}: written")


if __name__ == "__main__":
    main()
