"""pytest fixtures driving botanik_sim over a PTY, read back through pyte.

The sim is the real firmware, speaking ANSI over its console serial at a
throttled 4800 baud. It gets a PTY rather than a pipe (RawSerial's TIOCOUTQ only
answers on a tty), and pyte replays the bytes into an 80x25 screen to assert on.

Time is the harness's, not the host's: the sim runs one beat per command over a
control pipe (BOTANIK_SIM_CLOCK=manual), so every wait is bounded in BEATS and
the same run gives the same screen whatever else the box is doing. See README.md.
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

COLS, ROWS = 80, 25                       # Config::CONSOLE_COLS/ROWS, the SCREEN_MIN_* floor
GOLDEN_DIR = Path(__file__).parent / "golden"

# The sim advances one beat per command, so a wait is bounded in beats: the same
# on a loaded box as on an idle one. ACK_TIMEOUT_S bounds only the round-trip,
# and firing it means the sim is wedged rather than slow.
ACK = b"\033]777;botanik-ack\007"
BEATS_PER_STEP = int(os.environ.get("BOTANIK_PYTE_BEAT_STEP", "20"))
MAX_BEATS = int(os.environ.get("BOTANIK_PYTE_MAX_BEATS", "4000"))
ACK_TIMEOUT_S = float(os.environ.get("BOTANIK_PYTE_ACK_TIMEOUT", "30.0"))


def pytest_addoption(parser):
    parser.addoption("--sim", action="store", default=None,
                     help="path to the botanik_sim binary (else $BOTANIK_SIM)")
    parser.addoption("--update", action="store_true", default=False,
                     help="regenerate golden screens instead of asserting against them")


class Term:
    """A running botanik_sim plus a pyte screen fed from its PTY."""

    def __init__(self, binary, watch=(), store=None, screen=None, watchdog=False):
        env = dict(os.environ)
        self._ctl_w = None
        self._raw = b""                       # undecoded tail, for the ack split
        self.beats = 0                        # beats run, for the bounded waits
        env["BOTANIK_NO_RT_WARNING"] = "1"    # host-dependent privilege warnings
        env["BOTANIK_NO_RT"] = "1"            # ...and the pinning itself: ctest runs sims in parallel
        env["BOTANIK_NO_LOAD_ALARMS"] = "1"   # CPU_* fire on host timing, moving the alarm bar
        env["BOTANIK_NO_MBCHECK"] = "1"       # its traffic moves the log tail a golden pins
        # Off by default: the clock is manual here, so a case that stops running
        # beats would look wedged and be re-executed under its own PTY. The one
        # case that asserts the watchdog WORKS passes watchdog=True.
        env["BOTANIK_WATCHDOG"] = "0" if watchdog is False else "1"
        if watchdog not in (True, False):
            env["BOTANIK_WATCHDOG_MS"] = str(watchdog)   # a shorter window
        if store is not None:
            # Private config store, or the sims share ~/.botanik_config with each
            # other and with the developer's own (test_params commits an edit).
            env["BOTANIK_CONFIG_STORE"] = str(store)

        # The sim runs a beat only when told to, over a control pipe. The fd
        # keeps its number in the child: close_fds runs AFTER preexec_fn, so a
        # dup2 to a fixed 3 would be closed again unless 3 were itself in
        # pass_fds. Naming the real number is one fewer thing to get wrong.
        ctl_r, self._ctl_w = os.pipe()
        env["BOTANIK_SIM_CLOCK"] = "manual"
        env["BOTANIK_SIM_CONTROL_FD"] = str(ctl_r)

        self.master, slave = pty.openpty()
        self.proc = subprocess.Popen(
            [binary], stdin=slave, stdout=slave, stderr=subprocess.DEVNULL,
            env=env, close_fds=True, pass_fds=(ctl_r,))
        os.close(slave)
        os.close(ctl_r)
        os.set_blocking(self.master, False)

        # latin-1, not pyte.ByteStream: its UTF-8 decoder can resync-drop a byte
        # and a lost ESC leaves a literal "[2;1H" on the screen.
        # `screen` widens the emulator for a test that resizes the console past
        # the sim's own boot geometry; the firmware still draws to term_cols.
        cols, rows = screen if screen is not None else (COLS, ROWS)
        self.screen = pyte.Screen(cols, rows)
        self.stream = pyte.Stream(self.screen)

        # Markers latched as they go by, so wait_for() can answer for a line that
        # has already scrolled out of the 7-row log tail. Registered here to cover
        # the bytes that land before the first wait_for().
        self._watch = set(watch)
        self._seen = set()
        self._view = None                     # normalized screen, see view()
        self._await_ack()                     # the sim announcing it is ready

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
        answer two or three times per wait - the loop's own movement check plus
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
        deadline = time.monotonic() + ACK_TIMEOUT_S   # liveness only: a dead sim
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
            f"sim never acknowledged {beats} beats within {ACK_TIMEOUT_S}s "
            f"(after {self.beats} beats) - it is wedged, not slow")

    def until(self, done, max_beats=MAX_BEATS):
        """Run beats in steps until `done()`, or `max_beats` have passed.

        The one wait in the harness: the bound is simulated beats,
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

    def settle(self):
        """Run beats until the normalized screen holds still across two steps.

        The blunt instrument, used to capture a golden: a content wait stops at
        the first frame that says yes, where this has to wait out the whole
        4800-baud dribble. Name what you are waiting for and this stays out of
        the assert path.
        """
        last = self.view()
        stable = 0
        for _ in range(0, MAX_BEATS, BEATS_PER_STEP):
            self.run(BEATS_PER_STEP)
            cur = self.view()
            stable = stable + 1 if cur == last else 0
            last = cur
            if stable >= 2:
                return

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
        return self.until(done)

    def wait_screen(self, needle, present=True):
        """Pump until `needle` is (or with present=False, is no longer) on screen."""
        done = lambda: (needle in "\n".join(self.screen.display)) is present  # noqa: E731
        return self.until(done)

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

    Polls until the screen matches rather than snapshotting once:
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
            term.settle()                 # a still screen, so the capture is stable
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
        if term.until(lambda: got() == want):
            return
        diff = "".join(difflib.unified_diff(
            path.read_text().splitlines(keepends=True),
            term.snapshot().splitlines(keepends=True),
            fromfile=f"{name} (expected)", tofile=f"{name} (actual)"))
        pytest.fail(f"screen never matched '{name}' after {MAX_BEATS} beats:"
                    f"\n{diff}")

    return _check
