#!/usr/bin/env python3
"""Textual screenshots of the console, for the manual.

Drives the real botanik_sim over a PTY through the pyte harness the screen
tests use, and writes the rendered 80x24 screen of each console page, in tab
order, into docs/generated/screens/, for the user manual.

    python3 scripts/screens.py [path/to/botanik_sim]

Not a build step and nothing checks its output.

A site repository imports it for screens of its own, through three functions and
nothing else: session() boots a sim, warm() runs it, capture() writes a screen.
gh_mlb41's scripts/scenario_screens.py is one.

The plant is run for WARMUP beats first, so the numbers on screen are those of
a unit that has been up a while rather than one three seconds old.
"""

import os
import re
import shutil
import sys
from contextlib import contextmanager
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tests', 'pyte'))

import keys                                          # noqa: E402
from harness import Term                             # noqa: E402

# Tab order, as register_pages() adds them. The label is what the status bar
# renders for the ACTIVE tab, so it is what a page switch syncs on.
#
# SHELL and NOTES are empty until something is typed at them, and a screenshot
# of an empty prompt shows the chrome rather than the page, so those two are
# given something to hold. Change it here.
DEMO = {'shell': [b'net\r'],
        'notes': [b'roof ram 2 sticky on the last 20% - checked 08/09']}

PAGES = [('home',    keys.ALT_1, '1:HOME'),
         ('shell',   keys.ALT_2, '2:SHELL'),
         ('logs',    keys.ALT_3, '3:LOGS'),
         ('alarms',  keys.ALT_4, '4:ALARMS'),
         ('params',  keys.ALT_5, '5:PARAMS'),
         ('monitor', keys.ALT_6, '6:MONITOR'),
         ('tasks',   keys.ALT_7, '7:TASKS'),
         ('system',  keys.ALT_8, '8:SYSTEM'),
         ('notes',   keys.ALT_9, '9:NOTES')]

OUT = os.path.join(ROOT, 'docs', 'generated', 'screens')

WARMUP = int(os.environ.get('BOTANIK_SCREEN_WARMUP', '6000'))    # beats


# One RUN command must be acknowledged inside the harness's ACK_TIMEOUT_S, and
# the harness itself never asks for more than BEATS_PER_STEP at a time. Warming
# up in chunks stays inside what it is built for.
BEAT_CHUNK = 10000


@contextmanager
def session(sim):
    """A botanik_sim under the pyte harness, its console up, with a store of its
    own; closed and cleaned up on leaving the block. Yields the harness Term."""
    tmp = tempfile.mkdtemp(prefix='botanik-screens.')
    # BOTANIK_CONFIG_STORE is a FILE inside a directory of its own. Handed the
    # directory, every persist write fails and the plant raises FLASH_FLT.
    term = Term(sim, store=os.path.join(tmp, 'config'))
    try:
        if not term.wait_screen(PAGES[0][2]):
            sys.exit('the console never came up:\n' + term.snapshot())
        yield term
    finally:
        term.close()
        shutil.rmtree(tmp, ignore_errors=True)


def warm(term, beats):
    """Run @p beats of the plant, in chunks the harness can acknowledge."""
    for done in range(0, beats, BEAT_CHUNK):
        term.run(min(BEAT_CHUNK, beats - done))


# Two cells move for reasons that are not the firmware's: the git hash is
# stamped in at build time, and TPS is measured off host timing. Left alone they
# would rewrite a committed screen on every commit, so a reader learns to ignore
# the diff. Everything else is deterministic under the virtual clock.
MASKS = [(re.compile(r'(Commit\s*\.+\s*: ).*$'),        r'\1<COMMIT>'),
         (re.compile(r'(Task period\s+\d+ ms \()\d+( TPS\))'), r'\1<N>\2')]


def capture(term, path):
    """Write the screen to @p path: the raw grid, masked, right-trimmed, with the
    trailing blank rows dropped. Returns the rows written."""
    rows = []
    for r in term.screen.display:
        for pat, sub in MASKS:
            r = pat.sub(sub, r)
        rows.append(r.rstrip())
    while rows and not rows[-1]:
        rows.pop()
    open(path, 'w').write('\n'.join(rows) + '\n')
    return len(rows)


def pages(sim):
    """One screen per console page, in tab order."""
    os.makedirs(OUT, exist_ok=True)
    with session(sim) as term:
        warm(term, WARMUP)
        for name, key, label in PAGES:
            term.send(key)
            if not term.wait_screen(label):
                sys.exit('%s never rendered:\n%s' % (name, term.snapshot()))
            for typed in DEMO.get(name, ()):
                term.send(typed)
                term.settle()
            term.settle()
            # The raw grid, not view(): the masks that keep a test stable would
            # put <HF> where the manual wants a reading.
            n = capture(term, os.path.join(OUT, name + '.txt'))
            print('  %-8s %2d rows' % (name, n))


def main():
    sim = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'botanik_sim')
    if not os.path.exists(sim):
        sys.exit('no simulator at %s - `make -C build` first' % sim)
    pages(sim)


if __name__ == '__main__':
    main()
