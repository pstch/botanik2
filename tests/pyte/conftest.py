"""pytest fixtures driving botanik_sim over a PTY, read back through pyte.

The sim is the real firmware, speaking ANSI over its console serial at a
throttled 4800 baud. It gets a PTY rather than a pipe (RawSerial's TIOCOUTQ only
answers on a tty), and pyte replays the bytes into an 80x24 screen to assert on.

Time is the harness's, not the host's: the sim runs one beat per command over a
control pipe (BOTANIK_SIM_CLOCK=manual), so a wait is bounded in beats and the
same run gives the same screen. BOTANIK_PYTE_MANUAL=0 falls back to the
wall-clock path below. See README.md.
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

# Wall-clock fallback (BOTANIK_PYTE_MANUAL=0). Every wait is bounded twice and
# must never depend on either firing: IDLE gives up once the screen has stopped
# moving, CAP gives up regardless (a live log tail repaints forever, so IDLE
# alone never fires on a firmware whose boot never completes). Both on
# time.monotonic(), which does not count time the box spent suspended. IDLE is a
# heuristic and cannot tell a stuck sim from a starved one, so it stays generous:
# tightening it to 3 s false-failed four runs in six against a good sim.
#
# Manual clock (BOTANIK_PYTE_MANUAL=1): the sim advances one beat per command
# instead of following the host clock, so a wait is bounded in BEATS and the
# same run always produces the same screen. The budgets below then bound only
# the ack round-trip, which is a liveness check rather than a race.
MANUAL = os.environ.get("BOTANIK_PYTE_MANUAL", "1") != "0"
ACK = b"\033]777;botanik-ack\007"
BEATS_PER_STEP = int(os.environ.get("BOTANIK_PYTE_BEAT_STEP", "20"))
MAX_BEATS = int(os.environ.get("BOTANIK_PYTE_MAX_BEATS", "4000"))

WAIT_IDLE_S = float(os.environ.get("BOTANIK_PYTE_WAIT_IDLE", "15.0"))
WAIT_CAP_S = float(os.environ.get("BOTANIK_PYTE_WAIT_CAP", "30.0"))
SETTLE_IDLE_S = float(os.environ.get("BOTANIK_PYTE_SETTLE_IDLE", "0.2"))
SETTLE_CAP_S = float(os.environ.get("BOTANIK_PYTE_SETTLE_CAP", "5.0"))


def pytest_addoption(parser):
    parser.addoption("--sim", action="store", default=None,
                     help="path to the botanik_sim binary (else $BOTANIK_SIM)")
    parser.addoption("--update", action="store_true", default=False,
                     help="regenerate golden screens instead of asserting against them")


class Term:
    """A running botanik_sim plus a pyte screen fed from its PTY."""

    def __init__(self, binary, time_scale=50, watch=(), store=None, manual=MANUAL):
        env = dict(os.environ)
        self.manual = manual
        self._ctl_w = None
        self._raw = b""                       # undecoded tail, for the ack split
        self.beats = 0                        # beats run, for the bounded waits
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

        # Manual clock: the sim runs a beat only when told to, over a control
        # pipe it inherits as fd 3. Nothing else about the run changes.
        # The fd keeps its number in the child: close_fds runs AFTER preexec_fn,
        # so a dup2 to a fixed 3 would be closed again unless 3 were itself in
        # pass_fds. Naming the real number is one fewer thing to get wrong.
        pass_fds = ()
        if manual:
            ctl_r, self._ctl_w = os.pipe()
            env["BOTANIK_SIM_CLOCK"] = "manual"
            env["BOTANIK_SIM_CONTROL_FD"] = str(ctl_r)
            pass_fds = (ctl_r,)

        self.master, slave = pty.openpty()
        self.proc = subprocess.Popen(
            [binary], stdin=slave, stdout=slave, stderr=subprocess.DEVNULL,
            env=env, close_fds=True, pass_fds=pass_fds)
        os.close(slave)
        if manual:
            os.close(ctl_r)
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
        if manual:
            self._await_ack()                 # the sim announcing it is ready

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

    def run(self, beats):
        """Advance the sim by `beats` beats and absorb everything it emits.

        Returns when the sim acknowledges, which it writes to the console AFTER
        the last beat - so the ack arriving means the output is already in hand.
        No polling and no quiescence heuristic: the wait ends on a message, not
        on a guess about whether more is coming.
        """
        os.write(self._ctl_w, b"RUN %d\n" % beats)
        self._await_ack(beats)
        return True

    def _await_ack(self, beats=0):
        """Absorb output until the sim's next ack. Raises if it never comes."""
        deadline = time.monotonic() + WAIT_CAP_S   # liveness only: a dead sim
        while time.monotonic() < deadline:
            r, _, _ = select.select([self.master], [], [], 0.05)
            if not r:
                continue
            try:
                data = os.read(self.master, 65536)
            except OSError:
                break
            if not data:
                continue
            self._raw += data
            if ACK in self._raw:
                head, _, tail = self._raw.partition(ACK)
                self.stream.feed(head.decode("latin-1"))
                self._raw = tail
                self._view = None
                self._latch()
                self.beats += beats
                return True
        raise AssertionError(
            f"sim never acknowledged {beats} beats within {WAIT_CAP_S}s "
            f"(after {self.beats} beats) - it is wedged, not slow")

    def until(self, done, max_beats=MAX_BEATS):
        """Run beats in steps until `done()`, or `max_beats` have passed.

        The manual-clock replacement for poll(): the bound is simulated beats,
        so it is the same on a loaded box as on an idle one, and a failure costs
        the beats rather than 15 s of waiting to see if anything else arrives.
        """
        if done():
            return True
        for _ in range(0, max_beats, BEATS_PER_STEP):
            self.run(BEATS_PER_STEP)
            if done():
                return True
        return False

    def poll(self, done, idle_s=WAIT_IDLE_S, cap_s=WAIT_CAP_S):
        """Pump until `done()`, the screen is still for `idle_s`, or `cap_s` passes.

        The one wait in the harness: settle(), wait_for(), wait_screen() and the
        golden compare are this loop with a different predicate. Both bounds are
        real - see the note on the budgets. Returns done().

        Under the manual clock there is nothing to wait FOR: the sim advances only
        when asked, so the wait becomes until(), bounded in beats. Dispatching
        here rather than at each call site covers the helpers that reach for
        poll() directly.
        """
        if self.manual:
            return self.until(done)
        prev = self.view()
        now = time.monotonic()
        idle_since, deadline = now, now + cap_s
        while not done():
            self._pump(0.02)
            cur = self.view()
            now = time.monotonic()
            if cur != prev:
                prev, idle_since = cur, now
            if now - idle_since >= idle_s or now >= deadline:
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
        if self.manual:
            # Quiescence in BEATS, not seconds: run steps until the screen holds
            # still across two of them. A fixed number of beats is not enough -
            # it captured a golden before the page had painted at all.
            last = self.view()
            stable = 0
            for _ in range(0, MAX_BEATS, BEATS_PER_STEP):
                self.run(BEATS_PER_STEP)
                cur = self.view()
                stable = stable + 1 if cur == last else 0
                last = cur
                if stable >= 2:
                    return
            return
        changed_at = None
        last = self.view()

        def still():
            nonlocal last, changed_at
            now = self.view()
            if now != last:
                last, changed_at = now, time.monotonic()
            return changed_at is not None and time.monotonic() - changed_at >= quiet

        self.poll(still, SETTLE_IDLE_S, SETTLE_CAP_S)

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
        done = lambda: self._seen >= want                     # noqa: E731
        return self.until(done) if self.manual else self.poll(done)

    def wait_screen(self, needle, present=True):
        """Pump until `needle` is (or with present=False, is no longer) on screen."""
        done = lambda: (needle in "\n".join(self.screen.display)) is present  # noqa: E731
        return self.until(done) if self.manual else self.poll(done)

    # -- inspection ----------------------------------------------------------

    def snapshot(self):
        """Normalized screen as a single string (one row per line, trailing \\n)."""
        return "\n".join(self.view()) + "\n"

    def close(self):
        if self._ctl_w is not None:
            try:
                os.close(self._ctl_w)
            except OSError:
                pass
            self._ctl_w = None
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
        matched = (term.until(lambda: got() == want) if term.manual
                   else term.poll(lambda: got() == want))
        if matched:
            return
        diff = "".join(difflib.unified_diff(
            path.read_text().splitlines(keepends=True),
            term.snapshot().splitlines(keepends=True),
            fromfile=f"{name} (expected)", tofile=f"{name} (actual)"))
        pytest.fail(f"screen never matched '{name}' (unchanged for {WAIT_IDLE_S}s):"
                    f"\n{diff}")

    return _check
