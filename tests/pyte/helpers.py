"""Small helpers shared across the page/reconcile suites."""

import keys


# Final markers of the async network boot handshake (DNS -> NTP -> MQTT). They
# land in the boot log shown on the Home page; the sequence retries on a missed
# NTP reply, so it can finish well after settle()'s quiet window. Waiting for both
# guarantees the chatter is done before any page switch - otherwise a late line
# lands in the Shell page's live log tail and breaks its (blank-tail) golden.
NET_BOOT_DONE = ("MQTT connected", "NTP synced")


def boot(spawn, **kwargs):
    """Launch a sim, wait for boot (incl. the network handshake) to finish."""
    term = spawn(**kwargs)
    term.settle()
    term.wait_for(NET_BOOT_DONE)   # drain the boot log before any page switch
    term.settle()
    return term


def goto(term, page_key):
    """Switch page and wait for the redraw to settle."""
    term.send(page_key)
    term.settle()
    return term


def shell(spawn, **kwargs):
    """Boot and switch to the SHELL page, ready for editor input."""
    term = boot(spawn, **kwargs)
    goto(term, keys.ALT_2)
    return term
