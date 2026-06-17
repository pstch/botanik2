"""Small helpers shared across the page/reconcile suites."""

import keys


def boot(spawn, **kwargs):
    """Launch a sim, wait for the boot screen to settle, return the Term."""
    term = spawn(**kwargs)
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
