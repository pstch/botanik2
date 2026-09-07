#!/usr/bin/env python3
"""A textual screenshot of each console page, for the user manual.

Drives the real botanik_sim over a PTY through the pyte harness the screen
tests use, switches to each page in turn and writes the rendered 80x24 screen
to docs/user/pages/screens/.

Not a build step and nothing checks its output: it writes a starting point,
which is then edited by hand like any other page. Values come from the mock
plant and differ from run to run.

    python3 scripts/screens.py [path/to/botanik_sim]

The plant is run for WARMUP beats first, so the numbers on screen are those of
a unit that has been up a while rather than one three seconds old.
"""

import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tests', 'pyte'))

import keys                                          # noqa: E402
from conftest import Term                            # noqa: E402

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

OUT = os.path.join(ROOT, 'docs', 'user', 'pages', 'screens')

WARMUP = int(os.environ.get('BOTANIK_SCREEN_WARMUP', '6000'))    # beats


def main():
    sim = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'botanik_sim')
    if not os.path.exists(sim):
        sys.exit('no simulator at %s - `make -C build` first' % sim)

    store = tempfile.mkdtemp(prefix='botanik-screens.')
    os.makedirs(OUT, exist_ok=True)
    term = Term(sim, store=store)
    try:
        if not term.wait_screen(PAGES[0][2]):
            sys.exit('the console never came up:\n' + term.snapshot())
        term.run(WARMUP)
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
            rows = [r.rstrip() for r in term.screen.display]
            while rows and not rows[-1]:
                rows.pop()
            path = os.path.join(OUT, name + '.txt')
            open(path, 'w').write('\n'.join(rows) + '\n')
            print('  %-8s %2d rows' % (name, len(rows)))
    finally:
        term.close()
        shutil.rmtree(store, ignore_errors=True)


if __name__ == '__main__':
    main()
