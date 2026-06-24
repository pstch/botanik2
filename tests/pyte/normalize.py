"""Field-level normalization of volatile screen content before comparison.

The firmware paints a few cells whose value changes every run or every tick:
the wall clock, uptime, serial-buffer depths, CPU/stack load, PLC link state,
and log/MARK timestamps. We rewrite each to a fixed token so a golden captures
the *layout and stable text* without pinning a moment in time. Formats mirror:
  - status bar metrics : terminal.cpp _build_metrics  "PLC:%s cpu:%3u%% stk:%3u%% "
  - status hot fields  : pages/status.h draw_hot       clock / uptime / console TX
  - log + MARK stamps  : utils/time.cpp format_ms_timestamp  "DD/MM/YY HH:MM:SS.mmm"
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
    (re.compile(r"\d{4,}d \d{2}:\d{2}:\d{2}"), "<UPTIME>"),                    # uptime DDDDd HH:MM:SS
    (re.compile(r"\b\d{2}:\d{2}:\d{2}\b"), "<TIME>"),                          # status-bar clock HH:MM:SS
    (re.compile(r"cpu:\s*\d+%"), "cpu:<C>"),                                   # CPU load
    (re.compile(r"stk:\s*\d+%"), "stk:<S>"),                                   # stack high-water
    (re.compile(r"PLC:\S+"), "PLC:<P>"),                                       # status-bar PLC state
    (re.compile(r"skipped \d+ beat\(s\)"), "skipped <N> beat(s)"),             # overrun beat count
    # Status page (pages/status.h) volatile fields. Values are right-justified, so
    # each label's \s+ absorbs the variable leading padding; the perf metrics are
    # inherently variable (and dilated-time skews them further), and the network /
    # NTP state settles on a timing-dependent tick.
    (re.compile(r"PLC link\s+\w+"), "PLC link <P>"),                           # UNKNOWN/RUNNING/...
    (re.compile(r"CPU load\s+[\d.]+%\s*\(peak\s+[\d.]+ms\)"), "CPU load <CPU>"),
    (re.compile(r"CPU jitter\s+[\d.]+%\s*\(peak\s+[\d.]+ms\)"), "CPU jitter <JIT>"),
    (re.compile(r"Stack\s+[\d.]+%\s*\(\d+ [KMG]?B free\)"), "Stack <STK>"),
    (re.compile(r"Task period\s+\d+ms \(\d+ RTPS\)"), "Task period <TASK>"),
    (re.compile(r"Console TX\s+\d+/\d+ \(drops: \d+\)"), "Console TX <TX>"),
    (re.compile(r"Network\s+\w+ \(lat [^)]*\)"), "Network <NET>"),             # net status + latency
    (re.compile(r"NTP sync\s+(?:OK \([-\d.]+ s\)|no sync)"), "NTP sync <NTP>"),
    # Site payload values (botanik_sim GreenhouseStatusPage). The mock PLC emulator
    # animates these, and it only spawns where the host permits a realtime thread:
    # on a constrained gate it never starts, so the payload reads all-zero instead.
    # Normalizing the values (not the labels/units) makes the golden match either
    # way - the screen test asserts layout, not the mock's data. 4-field actuators
    # before the 2-field one so the latter can't bite into a Windows/Blowers row.
    (re.compile(r"[\d.]+ V \([\d.]+ Hz\)"), "<PWR>"),                          # Grid AC
    (re.compile(r"[\d.]+ V \(A \S+ B \S+\)"), "<PWR>"),                        # DC bus
    (re.compile(r"[\d.]+ V \(gen [\d.]+V\)"), "<PWR>"),                        # Battery
    (re.compile(r"(UPS\s+:\s+)\w+"), r"\g<1><UPS>"),
    (re.compile(r"[\d.]+ C  RH [\d.]+%"), "<CLIMATE>"),                        # temp/RH zones
    (re.compile(r"(Last rain\s*:\s*)\d+ \w+ \(wind \d+km/h\)"), r"\g<1><RAIN>"),
    (re.compile(r"\d{2}:\d{2} / \d{2}:\d{2}"), "<SUN>"),
    (re.compile(r"\d+ \d+ L / \d+ Lpm"), "<WATER>"),
    (re.compile(r"(Tray\s+:\s+)(?:Tray \d+|N/A)"), r"\g<1><TRAY>"),
    (re.compile(r"(Status\s+:\s+)\S+"), r"\g<1><TRAYST>"),
    (re.compile(r"A\s*\d+% B\s*\d+% C\s*\d+% D\s*\d+%"), "<ACT4>"),            # windows / blowers
    (re.compile(r"A\s*\d+% B\s*\d+%"), "<ACT2>"),                             # extractors
    (re.compile(r"(Humidif\.\s*:\s+)\w+"), r"\g<1><HUMID>"),
    (re.compile(r"(Screen\s+:\s+)[\d.]+%"), r"\g<1><SCREEN>"),
    # Home page firmware-identity block (top-right): version and build commit change
    # with the build (the commit hash every commit), so mask the values and keep the
    # labels/leaders - the test asserts the block's layout, not the build it ran. The
    # site name is deterministic per site, so it stays.
    (re.compile(r"(Version \.+ : )\S+"), r"\g<1><VERSION>"),
    (re.compile(r"(Commit \.+ : )\S+"), r"\g<1><COMMIT>"),
    # Same volatile version in the boot-log banner ("BOOT: Botanik v<ver>"); its
    # "> Running on :" platform line is deterministic, so only the version masks.
    (re.compile(r"(BOOT: Botanik v)\S+"), r"\g<1><VERSION>"),
    # Boot detail line "> console 4800 baud, HW watchdog N ms": the watchdog
    # timeout is dilated by BOTANIK_TIME_SCALE, so its value differs between the
    # assert suite and a faster golden regen. Mask it (the baud is fixed).
    (re.compile(r"(HW watchdog )\d+( ms)"), r"\g<1><WD>\g<2>"),
    # Home page (site Home) live fields: the mock animates temp / humidity / vents
    # and the irrigation tray/stage (which advances on a timing-dependent tick, so
    # the stage shown depends on how far the mock has run by capture time).
    (re.compile(r"(Temp\s+)[\d.]+ C"), r"\g<1><CLIMATE>"),
    (re.compile(r"(Humidity\s+)[\d.]+%"), r"\g<1><CLIMATE>"),
    (re.compile(r"Win [\d.]+%\s+Blow [\d.]+%"), "Win <VENT> Blow <VENT>"),
    (re.compile(r"(Irrigation\s+)(?:Tray \d+ \([A-Z ]+\)|idle)"), r"\g<1><IRR>"),
    # Load-dependent CPU alarms in the ALARMS status bar: a constrained host that
    # can't hold the realtime deadline raises CPU_XRUN/CPU_LOAD/CPU_JITT, which
    # canonical hardware does not. Strip the codes so the bar reads the same either
    # way (their START/END log lines are dropped as noise below).
    (re.compile(r" CPU_\w+"), ""),
]

# Timing-noise log lines: the scheduler logs an overrun whenever a tick misses
# its period, which under time dilation happens at load-dependent moments. The
# same load also trips the CPU_XRUN/LOAD/JITT health alarms, whose START/END
# lines come and go with the host. Their count and placement vary run to run and
# scroll the log-writer pages, so they are dropped entirely (see meaningful_lines)
# rather than asserted on - the tests stay resilient to load instead of pinning it.
#
# (The boot-time privilege warnings - mlockall / SCHED_FIFO / CPU affinity - are
# host-dependent too, but the harness now silences them at the source via
# BOTANIK_NO_RT_WARNING, so boot output is deterministic and they need no drop
# here. The version banner that used to scroll with them is asserted normally.)
_NOISE = re.compile(
    r"scheduler overrun|ALARM_(?:START|END): (?:CPU |Scheduler overrun)"
    # The PLC-link-up log (driven by the sim's PLC emulator thread) and the
    # network bring-up block are logged in a non-deterministic order at boot, so
    # the SHARED_IO line lands on either side of the NETWORK lines run to run.
    # Drop it - the link state is still asserted via the status bar / home widget.
    r"|SHARED_IO: PLC link")


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
