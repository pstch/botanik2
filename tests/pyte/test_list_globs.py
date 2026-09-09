"""`<noun> list` takes a glob, and still takes a bare prefix.

One sim per pattern: a listing is paced, and a second command on the same
screen would race the first one's tail.
"""

import keys
from helpers import shell


# The groups these cases list from. A row name, not the chrome around it.
_GROUPS = ("air_", "tank_")


def _listing(term):
    """The result lines of the last listing, without the chrome."""
    out = []
    for line in term.view():
        s = line.strip()
        if s.startswith(_GROUPS) or s.endswith("variable(s)"):
            out.append(s)
    return out


def test_star_matches_inside_a_name(spawn):
    """The case the wildcard exists for: a group of numbered actuators."""
    term = shell(spawn, quiet_logs=True)
    term.send(b"vars list vent_*?" + keys.ENTER)
    term.settle()
    rows = _listing(term)
    assert rows[-1] == "4 variable(s)", rows
    assert all(r.startswith("vent_pos") for r in rows[:-1]), rows


def test_star_anchors_the_tail(spawn):
    """A leading star is ends-with: nothing else could express it."""
    term = shell(spawn, quiet_logs=True)
    term.send(b"vars list *_tgt?" + keys.ENTER)
    term.settle()
    rows = _listing(term)
    assert rows[-1] == "4 variable(s)", rows
    assert all("_tgt" in r for r in rows[:-1]), rows


def test_a_bare_prefix_still_means_prefix(spawn):
    """What every listing meant before wildcards, unchanged."""
    term = shell(spawn, quiet_logs=True)
    term.send(b"vars list tank_inTotal_L" + keys.ENTER)
    term.settle()
    rows = _listing(term)
    assert rows[-1] == "8 variable(s)", rows
    assert all(r.startswith("tank_inTotal_L") for r in rows[:-1]), rows


def test_question_mark_is_one_character(spawn):
    """vent_drive? takes the two single-digit channels and no wider name."""
    term = shell(spawn, quiet_logs=True)
    term.send(b"vars list vent_drive?" + keys.ENTER)
    term.settle()
    rows = _listing(term)
    assert rows[-1] == "2 variable(s)", rows
