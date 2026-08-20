"""Masking of volatile screen cells, so a comparison pins layout and not a moment.

Scoped to the pages the suite still renders - HOME (the one golden), SHELL,
NOTES, PARAMS. Masks for ALARMS/TASKS/MONITOR tables went with the tests that
looked at them; add one back only when a test asserts on that cell, and make it
WIDTH-PRESERVING if it sits inside a bordered table.
"""

import re

# Order matters: the stamp patterns are prefixes of one another, longest first.
_SUBS = [
    (re.compile(r"\d{2}/\d{2}/\d{2} \d{2}:\d{2}:\d{2}\.\d{3}"), "<TS>"),   # log stamp
    (re.compile(r"\d{3,}d \d{2}:\d{2}:\d{2}"), "<UPTIME>"),                # DDDd HH:MM:SS
    (re.compile(r"\b\d{2}:\d{2}:\d{2}\b"), "<TIME>"),                      # status bar clock
    (re.compile(r"cpu:\s*\d+%"), "cpu:<C>"),
    (re.compile(r"stk:\s*\d+%"), "stk:<S>"),

    # Home's identity block: version and commit move with every build.
    (re.compile(r"(Version \.+ : )\S+"), r"\g<1><VERSION>"),
    (re.compile(r"(Commit \.+ : )\S+"), r"\g<1><COMMIT>"),

    # Resolved addresses in the NTP/MQTT log lines are PID-derived on loopback.
    # Only fires when CAPTURING a golden - the asserts clip the log band away.
    (re.compile(r"(NTP synced) \(\d+\.\d+\.\d+\.\d+:\d+\)"), r"\g<1>"),
    (re.compile(r"(MQTT connected) \(\d+\.\d+\.\d+\.\d+:\d+\)"), r"\g<1>"),

    # The alarm bar's code list is the live unmasked set, which the fast
    # greenhouse cycles: mask it whole, so a test asserts the bar's presence and
    # position rather than its momentary contents.
    (re.compile(r"^( ALARMS :).*$"), r"\g<1><BAR>"),
]

# Home page site readings (label + live value). The mock animates them, and on a
# constrained host it may not run at all, so they read zero: mask the value, keep
# the label. Mirrors the site's home-field table in order, so a renamed field
# shows up here as an unmasked - and therefore flaky - value.
_HOME_FIELDS = (
    ("Interior", " degC"), ("Exterior", " degC"), ("Humidity", ""),
    ("Dew margin", " degC"), ("Target", " degC"),
    ("Cool dem", ""), ("Strat dem", ""), ("Humid dem", ""), ("Windows", ""),
    ("Intractor", ""), ("Fan speed", ""), ("Screen", ""), ("Skirt", ""),
    ("Tray", ""), ("Since rain", " min"), ("Wind", " mps"),
    ("Local V", " V"), ("Local A", " A"), ("Grid A", " A"), ("Lockout", ""),
)


def _mask_home_value(m):
    """Token padded to the field it replaced, so a variable-length f32 (21.5 vs
    21.1739) cannot shift the column after it."""
    return m.group(1) + "<HF>".ljust(len(m.group(2)))


_SUBS += [(re.compile("(" + re.escape(label) + r"\s+)(-?[\d.]+" + unit + r"\s*)"),
           _mask_home_value)
          for label, unit in _HOME_FIELDS]

# Alarm transitions flood the log tail at speed and nothing asserts one.
_NOISE = re.compile(r"ALARM_(?:START|END|FLAP):")


def normalize_lines(lines):
    """Apply the substitutions and strip trailing padding."""
    out = []
    for line in lines:
        for rx, repl in _SUBS:
            line = rx.sub(repl, line)
        out.append(line.rstrip())
    return out


def strip_noise(view):
    """Drop blank and timing-noise rows from ALREADY-normalized lines.

    Comparing the SEQUENCE of meaningful rows rather than their absolute
    positions means a stray scheduler-overrun line cannot fail a correct screen.
    Separate from meaningful_lines so a caller holding Term.view() does not
    normalize twice.
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
    """Drop log entries from a normalized screen: a page's live tail shows
    whatever the greenhouse logged, not what a test exercises."""
    return [ln for ln in lines if not _LOG_LINE.match(ln)]
