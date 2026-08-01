"""pytest fixtures driving botanik_sim over a PTY and reading its screen via pyte.

The simulator is the real firmware build: it speaks ANSI over its console serial
(stdin/stdout), throttled to a UART baud. We bind that console to a PTY (a pipe
would not do - RawSerial::availableForWrite() uses ioctl(TIOCOUTQ), which only
answers on a tty, so a pipe would bypass the baud throttle and back-pressure we
want to exercise). pyte replays the byte stream into an 80x24 screen we assert on.

Speed comes from BOTANIK_TIME_SCALE (see hal/simulator.h): simulated time runs N
times faster than wall-clock, so every byte/event ratio matches a real run but
the test finishes N times sooner. We do NOT raise the baud.
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

from normalize import normalize_lines, meaningful_lines, drop_log_lines

COLS, ROWS = 80, 24                       # matches AnsiTerminal(..., 80, 24, ...) in main.cpp
GOLDEN_DIR = Path(__file__).parent / "golden"


def pytest_addoption(parser):
    parser.addoption("--sim", action="store", default=None,
                     help="path to the botanik_sim binary (else $BOTANIK_SIM)")
    parser.addoption("--update", action="store_true", default=False,
                     help="regenerate golden screens instead of asserting against them")


class Term:
    """A running botanik_sim plus a pyte screen fed from its PTY."""

    # Default scale 50 - the greenhouse lives 50x faster, so time-bound waits
    # (the network boot handshake, refresh cycles) finish in a fraction of the wall
    # time. The assertions are written to tolerate a fast-drifting sim (volatile
    # state masked in normalize.py, live log tails not pinned - see the resilience
    # notes in helpers.py / test_*.py); the suite stays green at far higher scales
    # too (verified 19/19 at scale 500). Override per-run with PYTE_TIME_SCALE=N
    # (env, honored through pytest/ctest/make alike). Use a release/no-sanitizer
    # sim; a debug+ASAN build is much slower per tick. BOTANIK_NO_XRUN_WARNING (set
    # below) silences the load-dependent overrun logs.
    def __init__(self, binary, time_scale=50, watch=(), store=None):
        time_scale = int(os.environ.get("PYTE_TIME_SCALE", time_scale))  # tunable knob
        env = dict(os.environ)
        env["BOTANIK_TIME_SCALE"] = str(time_scale)
        # Silence the boot-time privilege warnings (mlockall / SCHED_FIFO / CPU
        # affinity): which a host denies varies, so they're host-dependent boot
        # noise. Suppressing at the source keeps the rendered screen deterministic.
        env["BOTANIK_NO_RT_WARNING"] = "1"
        # Hold the CPU_* load alarms cleared: they fire at host-timing-dependent
        # moments, so without this the alarm set (and thus the alarms-page screen and
        # the persistent alarm bar) varies run to run. See health.h.
        env["BOTANIK_NO_LOAD_ALARMS"] = "1"
        # Private config store. HAL::Storage otherwise persists to ~/.botanik_config,
        # which the tests would share with each other (four sims run in parallel) and
        # with the developer's own sim: test_params commits an edit, so a suite run
        # left ntp_poll_s=120 behind for every later run and every desktop launch.
        # storage.cpp documents $BOTANIK_CONFIG_STORE as exactly this override.
        if store is not None:
            env["BOTANIK_CONFIG_STORE"] = str(store)

        self.master, slave = pty.openpty()
        self.proc = subprocess.Popen(
            [binary], stdin=slave, stdout=slave, stderr=subprocess.DEVNULL,
            env=env, close_fds=True)
        os.close(slave)
        os.set_blocking(self.master, False)

        # Feed via a plain Stream with latin-1 decoding (every byte maps 1:1 and
        # never errors). pyte.ByteStream's UTF-8 decoder can resync-drop a byte on
        # a stray non-UTF-8 byte, which corrupts an escape sequence (e.g. a lost
        # ESC leaves a literal "[2;1H" on the screen).
        self.screen = pyte.Screen(COLS, ROWS)
        self.stream = pyte.Stream(self.screen)

        # Markers to latch as they go by. The screen is a window, not a log: a boot
        # line lands in the 7-row log tail and scrolls out of it a moment later, so
        # "has this happened?" cannot be answered by looking at the current screen.
        # Every pump records which watched markers were visible, and wait_for()
        # answers from that record. Register them at construction to cover the
        # bytes that arrive before the first wait_for() call.
        self._watch = set(watch)
        self._seen = set()

    # -- raw I/O -------------------------------------------------------------

    def _pump(self, timeout):
        """Feed any bytes the firmware has emitted into the screen. Returns count."""
        r, _, _ = select.select([self.master], [], [], timeout)
        if not r:
            return 0
        try:
            data = os.read(self.master, 65536)
        except OSError:
            return 0
        if data:
            self.stream.feed(data.decode("latin-1"))
            self._latch()
        return len(data)

    def _latch(self):
        """Record any watched marker currently on screen (see __init__)."""
        pending = self._watch - self._seen
        if not pending:
            return
        blob = "\n".join(self.screen.display)
        self._seen |= set(n for n in pending if n in blob)

    def send(self, data):
        """Write raw input bytes to the firmware (str is UTF-8 encoded)."""
        if isinstance(data, str):
            data = data.encode("utf-8")
        os.write(self.master, data)

    # -- synchronization -----------------------------------------------------

    def settle(self, quiet=0.05, timeout=5.0):
        """Drain output until the *normalized* screen has been stable for `quiet`s.

        The screen never goes byte-quiet: periodic refreshes (uptime/clock/cpu)
        keep trickling. But those only rewrite volatile cells, which normalize to
        fixed tokens - so once the input-driven repaint is done, the normalized
        screen is stable even as raw bytes keep arriving.

        We require observing at least one change before counting the quiet window,
        so a snapshot can never capture the pre-action screen when the firmware is
        slow to process the just-sent input (e.g. a page switch still in flight).
        Every settle in the suite follows a screen-changing action; if one somehow
        produced no change, the timeout bounds the wait.
        """
        deadline = time.time() + timeout
        prev = tuple(normalize_lines(self.screen.display))
        last_change = time.time()
        changed = False
        while True:
            self._pump(0.02)
            cur = tuple(normalize_lines(self.screen.display))
            now = time.time()
            if cur != prev:
                prev, last_change, changed = cur, now, True
            if (changed and now - last_change >= quiet) or now >= deadline:
                return

    def wait_for(self, needles, timeout=6.0):
        """Pump until every string in `needles` has been seen on screen.

        Content-based sync for events settle() can miss: an async sequence (the
        DNS->NTP->MQTT boot handshake, which retries) emits in bursts with gaps,
        so the quiet window can return mid-sequence. Waiting for its final markers
        is deterministic where a quiet-window guess is not. Returns True once all
        are present, False on timeout (caller proceeds either way).

        "Seen", not "on screen right now": the markers are latched as they arrive
        (see _latch), so two log lines that never share the visible tail - or one
        that scrolled past during the preceding settle() - still satisfy the wait.
        Requiring them simultaneously made this call burn its full timeout on
        every test, and the sim ages ~50x wall-clock while it waits.
        """
        if isinstance(needles, str):
            needles = (needles,)
        self._watch |= set(needles)
        self._latch()
        deadline = time.time() + timeout
        while True:
            if self._seen >= set(needles):
                return True
            if time.time() >= deadline:
                return False
            self._pump(0.02)

    # -- inspection ----------------------------------------------------------

    def snapshot(self):
        """Normalized screen as a single string (one row per line, trailing \\n)."""
        return "\n".join(normalize_lines(self.screen.display)) + "\n"

    def close(self):
        self.proc.terminate()
        try:
            self.proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=2)
        os.close(self.master)


@pytest.fixture
def spawn(request, tmp_path):
    """Factory that launches botanik_sim instances and tears them down."""
    sim = request.config.getoption("--sim") or os.environ.get("BOTANIK_SIM")
    if not sim:
        pytest.skip("no simulator binary: pass --sim or set BOTANIK_SIM")
    if not Path(sim).exists():
        pytest.skip(f"simulator binary not found: {sim}")

    instances = []

    def _spawn(**kwargs):
        # One config store per sim (see Term): pytest's tmp_path is per-test, and
        # the index keeps two sims in one test from sharing one.
        kwargs.setdefault("store", tmp_path / f"config{len(instances)}")
        term = Term(sim, **kwargs)
        instances.append(term)
        return term

    yield _spawn

    for term in instances:
        term.close()


@pytest.fixture
def assert_screen(request):
    """Compare a Term's screen against a committed golden fixture.

    Assert mode *polls until the live screen matches the golden* (or a timeout),
    rather than snapshotting once. Under time dilation a render can span several
    ticks with gaps, so a single post-settle snapshot occasionally catches a
    partial frame; polling makes a transient frame harmless - we simply keep
    reading until the correct state appears. A genuine mismatch still fails (on
    timeout) with a readable full-screen diff.

    With --update the golden is (re)written from a settled snapshot, so authoring
    a new test is: write it, run `make pytest-update`, eyeball the golden, commit.
    """
    update = request.config.getoption("--update")

    def _check(term, name, timeout=8.0, rows=None):
        # rows=(start, stop): compare only that half-open row band, both sides
        # sliced before meaningful_lines. Use it when the rest of the screen holds
        # legitimately-drifting content the test isn't about - e.g. a page's live
        # log tail, which at high BOTANIK_TIME_SCALE churns and scrolls the pinned
        # lines away. The golden still stores the full screen for review.
        path = GOLDEN_DIR / f"{name}.txt"
        clip = (lambda lines: lines[rows[0]:rows[1]]) if rows else (lambda lines: lines)
        if update:
            term.settle(quiet=0.4)        # extra-stable capture for the golden
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(term.snapshot())
            return
        if not path.exists():
            pytest.fail(f"missing golden '{name}'; run `make pytest-update` to author it")
        # Golden stores the full screen (readable for review); the compare is on
        # meaningful_lines (volatile fields normalized, overrun/blank noise
        # dropped) so an incidental scroll or stray overrun can't fail a correct
        # screen. Log lines drop out too: every page with a live tail shows
        # whatever the fast greenhouse logged at that instant, which is a
        # function of how far the sim has aged, not of what the test exercises.
        # Pinning them made the goldens depend on the sim being in a particular
        # steady state - assert log content with wait_for(), as test_logs does.
        want = drop_log_lines(meaningful_lines(clip(path.read_text().split("\n"))))
        deadline = time.time() + timeout
        while True:
            if drop_log_lines(meaningful_lines(clip(term.screen.display))) == want:
                return
            if time.time() >= deadline:
                break
            term._pump(0.02)
        diff = "".join(difflib.unified_diff(
            path.read_text().splitlines(keepends=True),
            term.snapshot().splitlines(keepends=True),
            fromfile=f"{name} (expected)", tofile=f"{name} (actual)"))
        pytest.fail(f"screen never matched '{name}' within {timeout}s:\n{diff}")

    return _check
