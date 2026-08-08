"""Masking of volatile screen cells, so a comparison pins layout and not a moment.

The firmware paints cells that change every run or every tick - the clock,
uptime, load, the live readings, log stamps. Each is rewritten to a fixed token.

Two rules for adding one:
  - Only mask a cell some test asserts on. This file once carried 25 masks for
    the StatusPage, which no site registers.
  - Inside a bordered table, a mask must be WIDTH-PRESERVING, or the row changes
    width and stops matching the golden's.
"""

import re

# Order matters: each stamp pattern is a prefix of the one before it, so the
# longest must match first.
_SUBS = [
    (re.compile(r"\d{2}/\d{2}/\d{2} \d{2}:\d{2}:\d{2}\.\d{3}"), "<TS>"),   # log / MARK stamp
    (re.compile(r"\d{2}/\d{2}/\d{2} \d{2}:\d{2}:\d{2}"), "<CLOCK>"),       # status page wall clock
    # ALARMS' LAST EVENT stamp, padded: it sits in a table cell, and an alarms row
    # was changing width the moment its alarm first fired.
    (re.compile(r"\d{2}/\d{2}/\d{2} \d{2}:\d{2}\b"),
     lambda m: "<ALARM_TS>".ljust(len(m.group(0)))),
    (re.compile(r"\d{3,}d \d{2}:\d{2}:\d{2}"), "<UPTIME>"),                # uptime DDDd HH:MM:SS
    (re.compile(r"\b\d{2}:\d{2}:\d{2}\b"), "<TIME>"),                      # status bar clock
    (re.compile(r"cpu:\s*\d+%"), "cpu:<C>"),
    (re.compile(r"stk:\s*\d+%"), "stk:<S>"),

    # Home's identity block: version and commit change with every build, the site
    # name does not. The watchdog timeout is dilated by BOTANIK_TIME_SCALE, so it
    # differs between the assert suite and a slower golden regen.
    (re.compile(r"(Version \.+ : )\S+"), r"\g<1><VERSION>"),
    (re.compile(r"(Commit \.+ : )\S+"), r"\g<1><COMMIT>"),
    (re.compile(r"(HW watchdog )\d+( ms)"), r"\g<1><WD>\g<2>"),

    # The NTP/MQTT log lines carry a resolved address whose host part is
    # PID-derived on the sim's loopback.
    (re.compile(r"(NTP synced) \(\d+\.\d+\.\d+\.\d+:\d+\)"), r"\g<1>"),
    (re.compile(r"(MQTT connected) \(\d+\.\d+\.\d+\.\d+:\d+\)"), r"\g<1>"),

    # The alarm bar's code list is the live unmasked set, which the fast
    # greenhouse cycles: mask it whole, so every page test asserts the bar's
    # presence and position rather than its momentary contents.
    (re.compile(r"^( ALARMS :).*$"), r"\g<1><BAR>"),

    # A table's window counter "first-last / total": the total is the size of the
    # site's variable table, so one added row would rewrite three pages' goldens.
    (re.compile(r"\d+-\d+ / \d+"), lambda m: "<N>".ljust(len(m.group(0)))),
    (re.compile(r"\d+ active"), "<NA> active"),          # alarms footer count
    (re.compile(r"(Quality\s+: )\S+"), r"\g<1><Q>"),     # MONITOR detail panel
    # TASKS detail panel: all three are host timing, not firmware layout.
    (re.compile(r"(Last us\s+: ).*$"), r"\g<1><T>"),
    (re.compile(r"(Runs\s+: ).*$"), r"\g<1><N>"),
    (re.compile(r"(Of period\s+: ).*$"), r"\g<1><D>"),
]

# Home page site readings, from the site's home-field table (label + live value,
# three columns). The mock animates them, and on a constrained host the emulator
# may not run at all, so they read zero: mask the value, keep the label. Mirrors
# src/sites/gh_mlb41/status.h g_home_fields[] in table order, so a renamed field
# shows up here as an unmasked - and therefore flaky - value.
_HOME_FIELDS = (
    ("Line V", " V"), ("Line-line", " V"), ("Line Hz", " Hz"), ("Current", " A"),
    ("Power", " W"), ("Pwr factor", ""),
    ("L1 V", " V"), ("L2 V", " V"), ("L3 V", " V"),
    ("L1 A", " A"), ("L2 A", " A"), ("L3 A", " A"),
    ("Neutral A", " A"), ("THD L-N", " %"),
    ("ATS V", " V"), ("ATS A", " A"), ("ATS W", " W"), ("ATS Hz", " Hz"),
    ("Supply Hz", " Hz"), ("Lockout", ""), ("Screen", ""), ("Skirt", ""),
)


def _mask_home_value(m):
    """Token padded to the field it replaced, so a variable-length f32 (21.5 vs
    21.1739) cannot shift the column after it."""
    return m.group(1) + "<HF>".ljust(len(m.group(2)))


_SUBS += [(re.compile("(" + re.escape(label) + r"\s+)(-?[\d.]+" + unit + r"\s*)"),
           _mask_home_value)
          for label, unit in _HOME_FIELDS]

# Alarm transitions flood the log tail at speed and nothing asserts one; alarm
# state is asserted on the alarms page instead.
_NOISE = re.compile(r"ALARM_(?:START|END|FLAP):")

# Live cells inside a bordered table, masked by the table's geometry because they
# cannot be matched by content:
#
#   MONITOR  | SCOPE | VARIABLE | VALUE | UNIT | OVERRIDE |            (5 cells)
#   ALARMS   | CODE  | VARIABLE | ON | LAST EVENT |                    (4 cells)
#   TASKS    | TASK  | GROUP | EVERY ms | PHASE | MEAN us | PEAK us |  (6 cells)
#
# MONITOR's VALUE is live on all ~72 rows, so a per-name regex does not
# generalise; ALARMS' ON/OFF flips and its LAST EVENT is blank until the alarm
# first trips; TASKS' last two are microseconds measured on the host. The rest of
# each table IS the layout under test.
_TABLE_ROW = re.compile(r"^\s*\|(?:[^|]*\|){4,6}\s*$")
# A header row has a data row's shape, and for ALARMS the same text too (its
# third column is headed "ON"), so it is spotted by its first cell.
_TABLE_HEADERS = ("SCOPE", "CODE", "TASK", "PARAMETER")


def _mask_table_cell(line):
    """Mask a table row's live cells in place, preserving their width.

    The three tables are told apart by cell count: ALARMS four, MONITOR five,
    TASKS six.
    """
    if not _TABLE_ROW.match(line):
        return line
    cells = line.split("|")            # leading indent + n cells + trailing ""
    if len(cells) < 6 or cells[1].strip() in _TABLE_HEADERS:
        return line
    for i in {6: (3, 4), 7: (3,), 8: (5, 6)}.get(len(cells), ()):
        cells[i] = " <V>".ljust(len(cells[i]))[:len(cells[i])]
    return "|".join(cells)


def normalize_lines(lines):
    """Apply the substitutions and strip trailing padding."""
    out = []
    for line in lines:
        for rx, repl in _SUBS:
            line = rx.sub(repl, line)
        out.append(_mask_table_cell(line).rstrip())
    return out


def strip_noise(view):
    """Drop blank and timing-noise rows from ALREADY-normalized lines.

    Comparing the SEQUENCE of meaningful rows rather than their absolute
    positions means a stray scheduler-overrun line, and the scroll it causes,
    cannot fail a correct screen.

    Separate from meaningful_lines so a caller holding Term.view() does not pay
    to normalize a screen twice; normalization is a per-line map, so clipping a
    band before or after it gives the same rows.
    """
    return [ln for ln in view if ln and not _NOISE.search(ln)]


def meaningful_lines(lines):
    """strip_noise() over raw screen rows (or golden text read off disk)."""
    return strip_noise(normalize_lines(lines))


# normalize_lines has already collapsed the stamp, so every log entry starts with
# <TS> and nothing else does. A long entry wraps into indented "> " rows that
# carry no stamp but belong to it; no page legend starts a row with ">".
_LOG_LINE = re.compile(r"^(?:<TS> |\s+> )")


def drop_log_lines(lines):
    """Drop log entries from a normalized screen.

    A page's live tail shows whatever the greenhouse logged at that instant,
    which is a function of the sim's age and not of what a test exercises. Assert
    log content with wait_for() instead (see test_logs.py).
    """
    return [ln for ln in lines if not _LOG_LINE.match(ln)]
