"""`<noun> list` takes a glob, and still takes a bare prefix.

One sim per pattern: a listing is paced, and a second command on the same
screen would race the first one's tail. Patterns name the library's rows, and
the claims are properties of the match, so a site's own rows cannot move them.
"""

import re
from fnmatch import fnmatchcase

import keys
from helpers import shell

_ROW = re.compile(r"^([a-z][A-Za-z0-9_]*) .*\([PASIOQ]\)")
_COUNT = re.compile(r"^(\d+) variable\(s\)$")


def _listing(term, pattern):
    """The names the listing printed, checked against its own count line."""
    term.send(b"vars list " + pattern.encode() + keys.ENTER)
    term.settle()
    names, count = [], None
    for line in term.view():
        s = line.strip()
        m = _ROW.match(s)
        if m:
            names.append(m.group(1))
        m = _COUNT.match(s)
        if m:
            count = int(m.group(1))
    assert count == len(names), (count, names)
    return names


def test_star_matches_inside_a_name(spawn):
    """The case the wildcard exists for: a group named around its middle."""
    names = _listing(shell(spawn, quiet_logs=True), "sys_*_pct")
    assert all(fnmatchcase(n, "sys_*_pct") for n in names), names
    assert {"sys_cpu_load_pct", "sys_jitter_pct", "sys_stack_pct"} <= set(names), names


def test_star_anchors_the_tail(spawn):
    """A leading star is ends-with: nothing else could express it."""
    names = _listing(shell(spawn, quiet_logs=True), "*down_sec")
    assert names and all(n.endswith("down_sec") for n in names), names
    assert "sys_shutdown_sec" in names, names


def test_a_bare_prefix_still_means_prefix(spawn):
    """What every listing meant before wildcards, unchanged."""
    names = _listing(shell(spawn, quiet_logs=True), "sys_s")
    assert all(n.startswith("sys_s") for n in names), names
    assert {"sys_stack_pct", "sys_startup_sec", "sys_shutdown_sec"} <= set(names), names


def test_question_mark_is_one_character(spawn):
    """mqtt_???_sec takes the telemetry period and not the two-letter keepalive."""
    names = _listing(shell(spawn, quiet_logs=True), "mqtt_???_sec")
    assert names == ["mqtt_tlm_sec"], names
