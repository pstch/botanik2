"""Field-level normalization of volatile screen content before comparison.

The firmware paints a few cells whose value changes every run or every tick:
the wall clock, uptime, CPU/stack load, the live site readings, and log/MARK
timestamps. We rewrite each to a fixed token so an expected screen pins the
*layout and stable text* without pinning a moment in time. Formats mirror:
  - status bar metrics : status_bar.cpp build_metrics  "cpu:%3u%% stk:%3u%% "
  - home fields        : pages/home.h                  label + live value
  - log + MARK stamps  : utils/time.cpp format_ms_timestamp  "DD/MM/YY HH:MM:SS.mmm"

Only add a mask for a cell some test actually asserts on. Masks for screens no
test visits are invisible dead weight: this file once carried 25 of them for the
StatusPage, which is a building block no site registers.
"""

import re

# Order matters: the millisecond log timestamp is a superset of the clock, so it
# must be matched and replaced first.
_SUBS = [
    (re.compile(r"\d{2}/\d{2}/\d{2} \d{2}:\d{2}:\d{2}\.\d{3}"), "<TS>"),       # log / MARK stamp
    (re.compile(r"\d{2}/\d{2}/\d{2} \d{2}:\d{2}:\d{2}"), "<CLOCK>"),           # status wall clock
    # Alarm LAST EVENT stamp "YY/MM/DD HH:MM" (no seconds). Must follow the two
    # with-seconds patterns above: its minute prefix would otherwise bite the head
    # off a full HH:MM:SS stamp before they match.
    (re.compile(r"\d{2}/\d{2}/\d{2} \d{2}:\d{2}\b"), "<ALARM_TS>"),            # alarm LAST EVENT
    (re.compile(r"\d{3,}d \d{2}:\d{2}:\d{2}"), "<UPTIME>"),                    # uptime DDDd HH:MM:SS
    (re.compile(r"\b\d{2}:\d{2}:\d{2}\b"), "<TIME>"),                          # status-bar clock HH:MM:SS
    (re.compile(r"cpu:\s*\d+%"), "cpu:<C>"),                                   # CPU load
    (re.compile(r"stk:\s*\d+%"), "stk:<S>"),                                   # stack high-water
    # Home page firmware-identity block (top-right): version and build commit change
    # with the build (the commit hash every commit), so mask the values and keep the
    # labels/leaders - the test asserts the block's layout, not the build it ran. The
    # site name is deterministic per site, so it stays.
    (re.compile(r"(Version \.+ : )\S+"), r"\g<1><VERSION>"),
    (re.compile(r"(Commit \.+ : )\S+"), r"\g<1><COMMIT>"),
    # Boot detail line "> console 4800 baud, HW watchdog N ms": the watchdog
    # timeout is dilated by BOTANIK_TIME_SCALE, so its value differs between the
    # assert suite and a faster golden regen. Mask it (the baud is fixed).
    (re.compile(r"(HW watchdog )\d+( ms)"), r"\g<1><WD>\g<2>"),
    # (The home page's own site readings are masked after this list - see
    # _HOME_FIELDS: they are generated from the site's home-field table.)
    # The NTP-sync / MQTT-connect log lines carry the resolved socket address
    # (IP:port); the sim's loopback host part is PID-derived, so strip the
    # parenthesised address back to the bare message the golden pins.
    (re.compile(r"(NTP synced) \(\d+\.\d+\.\d+\.\d+:\d+\)"), r"\g<1>"),
    (re.compile(r"(MQTT connected) \(\d+\.\d+\.\d+\.\d+:\d+\)"), r"\g<1>"),
    # The persistent alarm bar (bottom row " ALARMS : CODE CODE ...") shows the
    # live unmasked-alarm set, which the fast-living greenhouse cycles constantly
    # (SYS_RST clears, site conditions trip in and out) - so which codes are shown
    # is timing-dependent, not layout. Mask the whole code list to a fixed token so
    # every page test asserts the bar's presence/position, not its momentary set.
    # Subsumes the old CPU_* strip; the dedicated alarms page asserts the table, not
    # the bar, so it is unaffected. (The bar's ALARM_START/END log lines are dropped
    # as noise below.)
    (re.compile(r"^( ALARMS :).*$"), r"\g<1><BAR>"),
    # Alarms page footer "... N active": how many conditions are currently raised
    # is live state the fast greenhouse cycles, so mask the count (the "SPACE:
    # active" mode hint has no leading digit and is left alone). "N listed" is the
    # fixed catalog size and stays.
    (re.compile(r"\d+ active"), "<NA> active"),
]

# Home page site readings (the site's declarative home-field table: label + live
# value, three columns; column 1 is two cells shorter - uptime + a spacer sit
# above it). The mock animates them, and on a constrained gate
# the emulator may not run so they read zero - either way only the value moves,
# so mask it and keep the label. Mirrors src/sites/gh_mlb41/status.h
# g_home_fields[]: (label, unit) in table order, so a renamed field shows up here
# as an unmasked - and therefore flaky - value.
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
    """Replace a home-field value with a token padded to the field it replaced.

    The pattern absorbs the value's trailing pad up to the next column, so a
    variable-length f32 (21.5 vs 21.1739) can't shift that column; padding the
    token back to the same width keeps the golden pinning where the next column
    starts.
    """
    return m.group(1) + "<HF>".ljust(len(m.group(2)))


_SUBS += [(re.compile("(" + re.escape(label) + r"\s+)(-?[\d.]+" + unit + r"\s*)"),
           _mask_home_value)
          for label, unit in _HOME_FIELDS]

# Timing-noise log lines. The fast greenhouse trips site conditions (temp, PS
# volts, storm, network) in and out constantly, so alarm transitions flood the
# log tail run to run - their count and placement vary with the host and scroll
# the log-writer pages. Nothing asserts one, so drop them all (see
# meaningful_lines); alarm state is asserted via the alarms page instead.
#
# (The boot-time privilege warnings - mlockall / SCHED_FIFO / CPU affinity - are
# host-dependent too, but the harness silences them at the source via
# BOTANIK_NO_RT_WARNING, so boot output is deterministic and they need no drop.)
_NOISE = re.compile(r"ALARM_(?:START|END|FLAP):")


def normalize_lines(lines):
    """Apply the volatile-field substitutions and strip trailing padding."""
    out = []
    for line in lines:
        for rx, repl in _SUBS:
            line = rx.sub(repl, line)
        out.append(line.rstrip())
    return out


def meaningful_lines(lines):
    """The screen reduced to a comparison that ignores scheduler overruns.

    Normalizes volatile fields, drops timing-noise lines, and drops blank lines
    so a comparison is by the sequence of meaningful rows - not their absolute
    position. An overrun line appearing (or not), and the scroll it causes, then
    can't change the result.
    """
    return [ln for ln in normalize_lines(lines) if ln and not _NOISE.search(ln)]


# A normalized log line: normalize_lines() has already collapsed the stamp to
# <TS>, so every log entry starts with it and nothing else does.
_LOG_LINE = re.compile(r"^<TS> ")


def drop_log_lines(lines):
    """Drop log entries from a normalized screen.

    A page's live log tail shows whatever the greenhouse logged at that instant,
    which is a function of how far the sim has aged rather than of what a test
    exercises - so a golden that pins one only holds while the sim reaches the
    assertion at the same age. Assert log content with wait_for() instead (see
    test_logs.py), and let the goldens speak for the widget rows.
    """
    return [ln for ln in lines if not _LOG_LINE.match(ln)]
