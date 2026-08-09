"""The kill ring is console-wide: what one page cuts, another pastes.

A per-page clipboard would pass every single-page test in the suite and still
be useless, because the reason to copy a variable name off a table is to type a
command with it. So the claim worth pinning is the crossing itself.
"""

import keys
from helpers import boot, goto


def _badge_name(term):
    """The name in the mode line's Alt+W badge, or None if it is not showing."""
    for row in term.screen.display:
        if "copied " in row:
            return row.split("copied ", 1)[1].split()[0]
    return None


def _copied_name(term):
    """Wait for the badge, then read the name out of it."""
    assert term.wait_screen("copied "), "Alt+W did not report a copy"
    name = _badge_name(term)
    assert name, "the badge left the screen between the wait and the read"
    return name


def test_alt_w_on_monitor_yanks_into_the_shell(spawn):
    """Alt+W copies the selected row's name; ^Y at the prompt pastes it."""
    term = goto(boot(spawn), keys.ALT_6)

    # Whichever row the cursor starts on: the test does not care which, only
    # that the badge names it and the same text arrives at the prompt.
    term.send(keys.ALT_W)
    name = _copied_name(term)

    goto(term, keys.ALT_2)
    term.send(keys.text("get ") + keys.CTRL_Y)
    assert term.wait_screen("get " + name), \
        f"the name copied on MONITOR ({name!r}) did not reach the prompt"


def test_alt_w_copies_the_row_the_cursor_is_on(spawn):
    """Move the selection first and it is the new row that gets copied - the
    badge is read off _selected, not off the top of the table."""
    term = goto(boot(spawn), keys.ALT_6)
    term.send(keys.ALT_W)
    first = _copied_name(term)

    term.send(keys.DOWN)
    term.send(keys.ALT_W)
    assert term.poll(lambda: _badge_name(term) not in (None, first)), \
        f"the badge still names {first!r} after moving the cursor"
