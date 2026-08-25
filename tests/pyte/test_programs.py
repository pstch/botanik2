"""`programs status`: the tally, and who is in the states worth naming.

Own sim: `control safe` puts the whole plant into its parked states, which
nothing after it would want to inherit.
"""

import keys
from helpers import shell


def test_status_tallies_and_names(spawn):
    """A tally per health, then the members of the ones that get named."""
    term = shell(spawn, quiet_logs=True)
    term.send(b"programs status" + keys.ENTER)
    assert term.wait_for("HEALTHY"), "no tally line"

    # Early in the boot the field rows are still arriving, so some laws are
    # short of an input. Whatever the split, the header carries the total and
    # the DEGRADED members are named under it.
    assert term.wait_for("programs: "), "the header does not carry a total"
    assert term.wait_for("  DEGRADED "), "the DEGRADED members are not named"


def test_status_names_the_one_still_travelling(spawn):
    """A safe request parks the plant, and PARKING is who has not arrived."""
    term = shell(spawn, quiet_logs=True)
    term.send(b"control safe" + keys.ENTER)
    assert term.wait_for("control: parking"), "the request was not taken"

    # The windows drive their rams to the seated position and say `parking()`
    # until the last one lands; everything else is where a reboot needs it on
    # the activation it was asked.
    term.send(b"programs status" + keys.ENTER)
    assert term.wait_for("PARKING   windows"), "the travelling program is not named"
    assert term.wait_for("INHIBITED"), "the arrived ones are not counted"
