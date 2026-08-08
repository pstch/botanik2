"""pytest fixtures driving botanik_sim over a PTY, read back through pyte.

The sim is the real firmware, speaking ANSI over its console serial at a
throttled 4800 baud. It gets a PTY rather than a pipe (RawSerial's TIOCOUTQ only
answers on a tty), and pyte replays the bytes into an 80x24 screen to assert on.
Speed comes from BOTANIK_TIME_SCALE, never from a higher baud. See README.md.
"""

import difflib
import os
import pty
import select
import subprocess
import time
from pathlib import Path

import pyte
import pytest

from normalize import (normalize_lines, meaningful_lines, strip_noise,
                       drop_log_lines)

COLS, ROWS = 80, 24                       # matches AnsiTerminal(..., 80, 24, ...) in main.cpp
GOLDEN_DIR = Path(__file__).parent / "golden"

# A wait ends once the screen has STOPPED MOVING for one of these, in host
# seconds. They are deliberately generous: the suite is CPU-bound, and a box
# running a build alongside it has been measured taking 7x longer per test, so a
# budget tight enough to be a timing assertion is a budget that fails correct
# runs. None of these is ever reached by a passing test - they only bound how
# long a genuinely stuck wait takes to report.
#
# This USED to be denominated in the sim's own clock, read off the status bar, on
# the theory that a budget measured in the observed thing scales with load by
# itself. It does not, and the whole apparatus was wall time in disguise: the
# displayed clock is steady_clock * BOTANIK_TIME_SCALE (hal/simulator.h), so it
# races ahead at 50x whether or not the starved firmware executed a single beat.
# 90 sim-seconds was 1.8 s of wall time, always, and that is what kept failing a
# different screen test on every loaded run. Host seconds, honestly labelled, are
# the same rule without the ceremony - and without a clock that steps backwards
# on an epoch re-latch, or resets under a test's feet on a reboot.
#
# Measured on time.monotonic(), never time.time(). Suspend the box mid-suite and
# the wall clock leaps forward on resume, ending every wait in flight on the spot
# (one soak run here reported 19.9 hours). CLOCK_MONOTONIC does not count time
# spent suspended, so the waits simply carry on where they left off.
WAIT_IDLE_S = float(os.environ.get("BOTANIK_PYTE_WAIT_IDLE", "15.0"))
MATCH_IDLE_S = float(os.environ.get("BOTANIK_PYTE_MATCH_IDLE", "4.0"))
SETTLE_IDLE_S = float(os.environ.get("BOTANIK_PYTE_SETTLE_IDLE", "0.2"))


def pytest_addoption(parser):
    parser.addoption("--sim", action="store", default=None,
                     help="path to the botanik_sim binary (else $BOTANIK_SIM)")
    parser.addoption("--update", action="store_true", default=False,
                     help="regenerate golden screens instead of asserting against them")


class Term:
    """A running botanik_sim plus a pyte screen fed from its PTY."""

    def __init__(self, binary, time_scale=50, watch=(), store=None):
        env = dict(os.environ)
        # Scale 50: the greenhouse lives 50x faster, so every byte/event ratio
        # matches a real 4800-baud run and only wall time is compressed. Green
        # far higher too (verified at 500). PYTE_TIME_SCALE overrides per run.
        env["BOTANIK_TIME_SCALE"] = str(int(os.environ.get("PYTE_TIME_SCALE", time_scale)))
        env["BOTANIK_NO_RT_WARNING"] = "1"    # host-dependent privilege warnings
        env["BOTANIK_NO_RT"] = "1"            # ...and the pinning itself: ctest runs sims in parallel
        env["BOTANIK_NO_LOAD_ALARMS"] = "1"   # CPU_* fire on host timing, moving the alarm bar
        env["BOTANIK_NO_MBCHECK"] = "1"       # its traffic moves the log tail a golden pins
        if store is not None:
            # Private config store, or the sims share ~/.botanik_config with each
            # other and with the developer's own (test_params commits an edit).
            env["BOTANIK_CONFIG_STORE"] = str(store)

        self.master, slave = pty.openpty()
        self.proc = subprocess.Popen(
            [binary], stdin=slave, stdout=slave, stderr=subprocess.DEVNULL,
            env=env, close_fds=True)
        os.close(slave)
        os.set_blocking(self.master, False)

        # latin-1, not pyte.ByteStream: its UTF-8 decoder can resync-drop a byte
        # and a lost ESC leaves a literal "[2;1H" on the screen.
        self.screen = pyte.Screen(COLS, ROWS)
        self.stream = pyte.Stream(self.screen)

        # Markers latched as they go by, so wait_for() can answer for a line that
        # has already scrolled out of the 7-row log tail. Registered here to cover
        # the bytes that land before the first wait_for().
        self._watch = set(watch)
        self._seen = set()
        self._view = None                     # normalized screen, see view()

    # -- raw I/O -------------------------------------------------------------

    def _pump(self, timeout):
        """Feed whatever the firmware has emitted into the screen."""
        r, _, _ = select.select([self.master], [], [], timeout)
        if not r:
            return
        try:
            data = os.read(self.master, 65536)
        except OSError:
            return
        if data:
            self.stream.feed(data.decode("latin-1"))
            self._view = None                 # the screen moved: drop the cache
            self._latch()

    def _latch(self):
        """Record any watched marker currently on screen."""
        pending = self._watch - self._seen
        if pending:
            blob = "\n".join(self.screen.display)
            self._seen |= set(n for n in pending if n in blob)

    def view(self):
        """The normalized screen, as a tuple of rows.

        Cached until the next byte arrives. Normalizing a full screen runs ~45
        regexes over 24 rows and measured 1 ms, and every wait wants the same
        answer two or three times per poll - the loop's own movement check plus
        whatever its predicate looks at.
        """
        if self._view is None:
            self._view = tuple(normalize_lines(self.screen.display))
        return self._view

    def send(self, data):
        """Write raw input bytes to the firmware (str is UTF-8 encoded)."""
        os.write(self.master, data.encode("utf-8") if isinstance(data, str) else data)

    # -- synchronization -----------------------------------------------------

    def poll(self, done, idle_s=WAIT_IDLE_S):
        """Pump until `done()`, or until the screen has been still for `idle_s`.

        The one wait in the harness: settle(), wait_for(), wait_screen() and the
        golden compare are this loop with a different predicate.
        """
        prev = self.view()
        idle_since = time.monotonic()
        while not done():
            self._pump(0.02)
            cur = self.view()
            if cur != prev:
                prev, idle_since = cur, time.monotonic()
            elif time.monotonic() - idle_since >= idle_s:
                return done()
        return True

    def settle(self, quiet=0.02):
        """Pump until the normalized screen has held still for `quiet` seconds.

        The screen never goes byte-quiet - the clock and the metrics keep
        trickling - but those cells normalize to fixed tokens, so a finished
        repaint reads as a still screen. At least one change must be observed
        first, or a slow repaint would let the caller assert on the pre-action
        screen; an action that paints nothing therefore costs SETTLE_IDLE_S.

        The blunt instrument, and now used only to capture a golden: a quiet
        window has to wait out the whole 4800-baud dribble, where a content wait
        stops at the first frame that says yes. Measured at ~200 ms against
        ~20 ms, and trading the suite's 29 settles for wait_for/wait_screen took
        it from ~10 s to ~4 s. Name what you are waiting for, and this stays out
        of the assert path.
        """
        changed_at = None
        last = self.view()

        def still():
            nonlocal last, changed_at
            now = self.view()
            if now != last:
                last, changed_at = now, time.monotonic()
            return changed_at is not None and time.monotonic() - changed_at >= quiet

        self.poll(still, SETTLE_IDLE_S)

    def wait_for(self, needles):
        """Pump until every string in `needles` HAS BEEN on screen.

        Latched, not current (see _latch): a boot line that scrolled out of the
        log tail still counts, and two lines that never shared the tail both do.
        The flip side is that a marker seen earlier answers instantly - for "is
        it on screen NOW", or for asserting one has LEFT it, use wait_screen().
        """
        want = {needles} if isinstance(needles, str) else set(needles)
        self._watch |= want
        self._latch()
        return self.poll(lambda: self._seen >= want)

    def wait_screen(self, needle, present=True):
        """Pump until `needle` is (or with present=False, is no longer) on screen."""
        return self.poll(
            lambda: (needle in "\n".join(self.screen.display)) is present)

    # -- inspection ----------------------------------------------------------

    def snapshot(self):
        """Normalized screen as a single string (one row per line, trailing \\n)."""
        return "\n".join(self.view()) + "\n"

    def close(self):
        self.proc.terminate()
        try:
            self.proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=2)
        os.close(self.master)


def _sim_binary(config):
    """Where the sim is, or a skip/fail explaining why there is nothing to run."""
    sim = config.getoption("--sim") or os.environ.get("BOTANIK_SIM")
    if not sim:
        pytest.skip("no simulator binary: pass --sim or set BOTANIK_SIM")
    if not Path(sim).exists():
        # A path that was NAMED and is absent means the sim was not built, which
        # is a broken run and not an absent capability. Skipping here once had
        # the whole suite report green having asserted nothing.
        pytest.fail(f"simulator binary not built: {sim}\n"
                    f"build it with: make -C <build> pyte-sim")
    return sim


def _spawner(sim, store_dir):
    """The spawn factory plus its teardown, shared by both fixture scopes."""
    instances = []

    def _spawn(**kwargs):
        kwargs.setdefault("store", store_dir / f"config{len(instances)}")
        term = Term(sim, **kwargs)
        instances.append(term)
        return term

    return _spawn, instances


@pytest.fixture
def spawn(request, tmp_path):
    """Factory that launches botanik_sim instances and tears them down."""
    _spawn, instances = _spawner(_sim_binary(request.config), tmp_path)
    yield _spawn
    for term in instances:
        term.close()


@pytest.fixture(scope="module")
def spawn_module(request, tmp_path_factory):
    """`spawn`, but one sim shared by every test in the module.

    The boot handshake is the expensive part of a screen test. Only for a file
    whose tests LOOK rather than mutate: an edit, a reset or a persisted
    override would carry into the next test.
    """
    _spawn, instances = _spawner(_sim_binary(request.config),
                                 tmp_path_factory.mktemp("shared"))
    yield _spawn
    for term in instances:
        term.close()


@pytest.fixture
def assert_screen(request):
    """Compare a Term's screen against a committed golden.

    Polls until the screen matches rather than snapshotting once: under dilation
    a render can span ticks, so a transient partial frame must not fail a correct
    screen. With --update the golden is rewritten from a settled snapshot, so
    authoring is: write the test, `make pytest-update`, eyeball, commit.
    """
    update = request.config.getoption("--update")

    def _check(term, name, rows=None):
        # rows=(start, stop) compares only that band, for a page whose other rows
        # hold something the golden has no business pinning (a live log tail).
        # The golden still stores the whole screen, for review.
        path = GOLDEN_DIR / f"{name}.txt"
        clip = (lambda lines: lines[rows[0]:rows[1]]) if rows else (lambda lines: lines)
        if update:
            term.settle(quiet=0.4)        # extra-stable capture for the golden
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(term.snapshot())
            return
        if not path.exists():
            pytest.fail(f"missing golden '{name}'; run `make pytest-update` to author it")
        # Compared on meaningful_lines with log lines dropped: volatile cells are
        # masked, and a page's live tail shows whatever the greenhouse logged at
        # that instant, which is a function of the sim's age. Assert log content
        # with wait_for() instead, as test_logs does.
        want = drop_log_lines(meaningful_lines(clip(path.read_text().split("\n"))))
        got = lambda: drop_log_lines(strip_noise(clip(term.view())))
        if term.poll(lambda: got() == want, MATCH_IDLE_S):
            return
        diff = "".join(difflib.unified_diff(
            path.read_text().splitlines(keepends=True),
            term.snapshot().splitlines(keepends=True),
            fromfile=f"{name} (expected)", tofile=f"{name} (actual)"))
        pytest.fail(f"screen never matched '{name}' (unchanged for {MATCH_IDLE_S}s):"
                    f"\n{diff}")

    return _check
