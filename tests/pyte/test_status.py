"""Status page screen."""

import keys
from helpers import boot, goto


def test_status_after_delay(spawn, assert_screen):
    """The status page after a refresh, with clock/uptime/depths normalized."""
    term = boot(spawn)
    goto(term, keys.ALT_4)               # STATUS (page index 3)
    # Past one PAGE_HOT_MS tick (1s sim -> 0.1s wall at scale 10) so hot fields
    # have painted at least once.
    term.settle(quiet=0.3, timeout=3.0)
    assert_screen(term, "status_after_delay")
