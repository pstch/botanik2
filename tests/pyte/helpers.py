"""Small helpers shared across the page/reconcile suites."""

import keys


# Final markers of the async network boot handshake (DNS -> NTP -> MQTT). They
# land in the boot log shown on the Home page; the sequence retries on a missed
# NTP reply, so it can finish well after settle()'s quiet window. Waiting for both
# guarantees the chatter is done before any page switch, so a test acts on a
# firmware that has finished booting rather than one still mid-handshake.
NET_BOOT_DONE = ("MQTT connected", "NTP synced")


def boot(spawn, **kwargs):
    """Launch a sim, wait for boot (incl. the network handshake) to finish."""
    kwargs.setdefault("watch", NET_BOOT_DONE)  # latch the markers from the first byte
    term = spawn(**kwargs)
    # The async net log can trail the quiet window, so wait it out. Assert it:
    # a marker the sim can no longer produce would otherwise burn wait_for()'s
    # full timeout in every single test and still pass (that is exactly what an
    # unreachable mock broker did - see the mqtt_broker default).
    assert term.wait_for(NET_BOOT_DONE), \
        f"network boot never completed; saw {sorted(term._seen)}"
    return term


def goto(term, page_key, marker=None):
    """Switch page and wait for the redraw.

    `marker` syncs on content instead of settle()'s quiet window. Prefer it for
    a page whose content never goes quiet (MONITOR animates every reading): the
    normalized screen there keeps changing, so settle() finds no quiet window
    and always burns its full timeout.
    """
    term.send(page_key)
    if marker is not None:
        assert term.wait_for(marker), f"page did not render marker {marker!r}"
    else:
        term.settle()
    return term


def shell(spawn, **kwargs):
    """Boot and switch to the SHELL page, ready for editor input."""
    term = boot(spawn, **kwargs)
    goto(term, keys.ALT_2)
    return term
