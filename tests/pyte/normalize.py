"""Field-level normalization of volatile screen content before comparison.

The firmware paints a few cells whose value changes every run or every tick:
the wall clock, uptime, serial-buffer depths, CPU/stack load, PLC link state,
and log/MARK timestamps. We rewrite each to a fixed token so a golden captures
the *layout and stable text* without pinning a moment in time. Formats mirror:
  - status bar metrics : terminal.cpp _build_metrics  "PLC:%s cpu:%3u%% stk:%3u%% "
  - status hot fields  : pages/status.h draw_hot       clock / uptime / "TX .. RX .."
  - log + MARK stamps  : utils/time.cpp format_ms_timestamp  "DD/MM/YY HH:MM:SS.mmm"
"""

import re

# Order matters: the millisecond log timestamp is a superset of the clock, so it
# must be matched and replaced first.
_SUBS = [
    (re.compile(r"\d{2}/\d{2}/\d{2} \d{2}:\d{2}:\d{2}\.\d{3}"), "<TS>"),       # log / MARK stamp
    (re.compile(r"\d{2}/\d{2}/\d{2} \d{2}:\d{2}:\d{2}"), "<CLOCK>"),           # status wall clock
    (re.compile(r"\d{4,}d \d{2}:\d{2}:\d{2}"), "<UPTIME>"),                    # uptime DDDDd HH:MM:SS
    (re.compile(r"TX\s+\d+/\d+\s+RX\s+\d+/\d+"), "<TXRX>"),                    # serial buffer depths
    (re.compile(r"cpu:\s*\d+%"), "cpu:<C>"),                                   # CPU load
    (re.compile(r"stk:\s*\d+%"), "stk:<S>"),                                   # stack high-water
    (re.compile(r"PLC:\S+"), "PLC:<P>"),                                       # status-bar PLC state
    (re.compile(r"skipped \d+ beat\(s\)"), "skipped <N> beat(s)"),             # overrun beat count
    # Status page (pages/status.h) volatile fields: the PLC link state settles on
    # a timing-dependent tick, and the scheduler perf metrics are inherently
    # variable (and dilated-time skews them further).
    (re.compile(r"PLC link\s+\w+"), "PLC link <P>"),                           # UNKNOWN/RUNNING/...
    (re.compile(r"Overruns\s+\d+"), "Overruns <OVR>"),                         # overrun counter
    (re.compile(r"CPU load\s+[\d.]+% \(peak [\d.]+ms\)"), "CPU load <CPU>"),
    (re.compile(r"Jitter\s+[\d.]+ms \(peak [\d.]+ms\)"), "Jitter <JIT>"),
    (re.compile(r"Stack\s+[\d.]+% \(\d+ B\)"), "Stack <STK>"),
]

# Timing-noise log lines: the scheduler logs an overrun whenever a tick misses
# its period, which under time dilation happens at load-dependent moments. Their
# count and placement vary run to run and scroll the log-writer pages, so they
# are dropped entirely (see meaningful_lines) rather than asserted on - the tests
# stay resilient to overruns instead of pretending they never happen.
_NOISE = re.compile(r"scheduler overrun")


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
