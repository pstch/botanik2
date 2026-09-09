#!/usr/bin/env python3
"""Textual screenshots of the console, for the manual.

Drives the real botanik_sim over a PTY through the pyte harness the screen
tests use, and writes the rendered 80x24 screen out.

    python3 scripts/screens.py              each console page, in tab order
                                            -> docs/user/pages/screens/
    python3 scripts/screens.py --scenarios  HOME under each simulated day
                                            -> docs/demo/screens/

Not a build step and nothing checks its output: it writes a starting point,
which is then edited by hand like any other page. Values come from the mock
plant and differ from run to run.

Either form takes a path to the simulator as its last argument.

The plant is run for WARMUP beats first, so the numbers on screen are those of
a unit that has been up a while rather than one three seconds old.
"""

import calendar
import os
import re
import shutil
import sys
import tempfile
import time

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

OUT = os.path.join(ROOT, 'docs', 'user', 'screens')
SCN_OUT = os.path.join(ROOT, 'docs', 'demo', 'screens')
TABLE = os.path.join(HERE, 'scenarios.tsv')

WARMUP = int(os.environ.get('BOTANIK_SCREEN_WARMUP', '6000'))    # beats
# Ten simulated minutes, which is what carries `probes` and `supply` from the
# hour they start at to the event that hour exists for.
SCN_WARMUP = int(os.environ.get('BOTANIK_SCENARIO_WARMUP', '60000'))

# The default day, as sim_scenario.sh has it; both read scenarios.tsv.
SCN_DAY = os.environ.get('BOTANIK_SIM_DAY', '2026-07-01')


def scenarios():
    """(name, start hour) from scenarios.tsv, minus `cycle`.

    `cycle` is a mode rather than a day - it runs a different one of the others
    each time round - so there is no one screen that shows it."""
    out = []
    for line in open(TABLE):
        if line.startswith('#') or not line.strip():
            continue
        name, start = line.split('\t')[:2]
        if name != 'cycle':
            out.append((name, start.strip()))
    return out


# One RUN command must be acknowledged inside the harness's ACK_TIMEOUT_S, and
# the harness itself never asks for more than BEATS_PER_STEP at a time. Warming
# up in chunks stays inside what it is built for.
BEAT_CHUNK = 10000


def warm(term, beats):
    for done in range(0, beats, BEAT_CHUNK):
        term.run(min(BEAT_CHUNK, beats - done))


def store_for(tmp):
    """BOTANIK_CONFIG_STORE is a FILE inside a directory of its own. Handed the
    directory, every persist write fails and the plant raises FLASH_FLT."""
    return os.path.join(tmp, 'config')


# Two cells move for reasons that are not the firmware's: the git hash is
# stamped in at build time, and TPS is measured off host timing. Left alone they
# would rewrite a committed screen on every commit, so a reader learns to ignore
# the diff. Everything else is deterministic under the virtual clock.
MASKS = [(re.compile(r'(Commit\s*\.+\s*: ).*$'),        r'\1<COMMIT>'),
         (re.compile(r'(Task period\s+\d+ ms \()\d+( TPS\))'), r'\1<N>\2')]


def capture(term, path):
    """The raw grid, right-trimmed, with the trailing blank rows dropped."""
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
    tmp = tempfile.mkdtemp(prefix='botanik-screens.')
    os.makedirs(OUT, exist_ok=True)
    term = Term(sim, store=store_for(tmp))
    try:
        if not term.wait_screen(PAGES[0][2]):
            sys.exit('the console never came up:\n' + term.snapshot())
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
    finally:
        term.close()
        shutil.rmtree(tmp, ignore_errors=True)


def scenario_homes(sim):
    """HOME under each simulated day, started at the hour that day is about.

    One sim per scenario: the day is chosen once, from the environment, and the
    epoch decides what the sun and the mock weather are doing."""
    os.makedirs(SCN_OUT, exist_ok=True)
    for name, start in scenarios():
        epoch = int(calendar.timegm(time.strptime(
            '%s %s' % (SCN_DAY, start), '%Y-%m-%d %H:%M')))
        env = dict(os.environ)
        os.environ['BOTANIK_SIM_SCENARIO'] = name
        os.environ['BOTANIK_SIM_EPOCH'] = str(epoch)
        try:
            # Sims run back to back here, and the mock network binds a
            # PID-derived 127/8 address: one can still meet the lingering
            # sockets of the one that just exited, come up, and then never
            # answer. Rare, and a second attempt gets a different PID.
            for attempt in (1, 2):
                tmp = tempfile.mkdtemp(prefix='botanik-screens.')
                term = Term(sim, store=store_for(tmp))
                try:
                    if not term.wait_screen(PAGES[0][2]):
                        sys.exit('%s never came up:\n%s' % (name, term.snapshot()))
                    warm(term, SCN_WARMUP)
                    term.settle()
                    n = capture(term, os.path.join(SCN_OUT, name + '.txt'))
                    print('  %-8s %s+10min  %2d rows' % (name, start, n))
                    break
                except AssertionError:
                    if attempt == 2:
                        raise
                    print('  %-8s wedged, retrying' % name)
                    time.sleep(2)
                finally:
                    term.close()
                    shutil.rmtree(tmp, ignore_errors=True)
        finally:
            os.environ.clear()
            os.environ.update(env)


def main():
    argv = [a for a in sys.argv[1:] if a != '--scenarios']
    sim = argv[0] if argv else os.path.join(ROOT, 'build', 'botanik_sim')
    if not os.path.exists(sim):
        sys.exit('no simulator at %s - `make -C build` first' % sim)
    if '--scenarios' in sys.argv:
        scenario_homes(sim)
    else:
        pages(sim)


if __name__ == '__main__':
    main()
