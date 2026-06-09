# HAL Platform-Abstraction Plan

Status: **in progress** — Workstreams A & B done; C/D pending.

This plan closes three HAL-boundary leaks and adds two missing platform
primitives (`reset()` and a hardware watchdog) to the HAL. The throughline is the
project rule from `CLAUDE.md`: *all platform-specific behaviour goes through
`hal/platform.h`; no other module reaches for raw `<time.h>`, `<unistd.h>`,
`<csignal>`, or vendor calls directly.*

Guiding split:
- **Genuine platform operations** (sleep, breakpoint, reset, watchdog) belong
  *in* HAL, declared once in `platform.h` and implemented in each backend
  (`simulator.h`, `opta.h`).
- **Deterministic algorithms that libc merely happens to provide** (calendar
  breakdown) do **not** belong in HAL — they go in `utils/` implemented
  platform-free, so the dependency disappears entirely instead of being spread
  across both backends.

**OPTA caveat (applies to every workstream):** the OPTA target cannot be built
or run in the dev environment here. All `opta.h` edits are compile-reasoned only
and must be confirmed on a real `arm-none-eabi` cross-build. Exact Mbed symbol
names (`mbed_reset`, `mbed::Watchdog`, `__BKPT`) should be verified against the
pinned Mbed OS headers under `includes/`.

---

## Workstream A — `HAL::sleep_ms()` (scheduler leak)  ✅ done

**Implemented:** `HAL::sleep_ms(uint32_t)` declared in `platform.h` and defined in
both backends (`usleep(ms*1000)` on sim, `rtos::ThisThread::sleep_for` on OPTA).
The scheduler's `#ifdef TARGET_SIMULATOR` usleep block is gone, replaced by
`HAL::sleep_ms((_next_tick_ms - now) / 2)`. **Note vs. plan:** the sim-only
include block could *not* be dropped wholesale — `<sched.h>`, `<sys/mman.h>`,
`<cerrno>`, `<cstring>` are still needed by `make_process_realtime`; only the now
unused `<unistd.h>` was removed. Validated: SIMULATOR builds clean and a 3 s
headless run sits at ~2 % CPU (yielding, not busy-spinning).

**Leak:** `scheduler/scheduler.cpp` includes `<unistd.h>` and calls `usleep()`
inside a `#ifdef TARGET_SIMULATOR` block (scheduler.cpp:62-68). This is more than
tidiness: the OPTA path has *no yield at all*, so on hardware the scheduler
busy-spins the entire sub-tick gap. There is already a TODO at scheduler.cpp:64
asking for exactly this.

**Changes:**
- `hal/platform.h` — declare `inline void sleep_ms(uint32_t ms);` under
  *Timing & clocks*.
- `hal/simulator.h` — `usleep(ms * 1000);` (the `<unistd.h>` include is
  legitimate *here*).
- `hal/opta.h` — `rtos::ThisThread::sleep_for(std::chrono::milliseconds(ms));`.
- `scheduler/scheduler.cpp` — drop `#include <unistd.h>`, delete the `#ifdef`
  block, replace with `HAL::sleep_ms((_next_tick_ms - now) / 2);`. The existing
  `now < _next_tick_ms` guard already bounds the argument.

**Decision — granularity:** the current sim sleeps in microseconds
(`remaining_ms * 500` µs ≈ half the remaining ms). A ms-granularity `sleep_ms`
collapses a 1 ms remainder to `0` — which the existing `if (delay > 500)` guard
already does today, so behaviour is effectively preserved. **Chosen: ms
granularity**, matching the TODO and keeping the HAL surface minimal. If sub-ms
jitter absorption is later found to matter, promote to `sleep_us`.

**Validation:** build SIMULATOR, run, confirm the scheduler still yields between
ticks (CPU not pegged).

---

## Workstream B — `HAL::debug_break()` (shell leak)  ✅ done

**Implemented:** `HAL::debug_break()` declared in `platform.h` (new *Debug*
group) and defined per backend (`raise(SIGTRAP)` on sim, `__BKPT(0)` on OPTA).
`shell/shell.cpp`'s `breakpoint` command and its `#ifdef`/`<csignal>` are gone,
replaced by a single `HAL::debug_break()`. Sim behaviour is byte-identical to the
old inline code. Validated: SIMULATOR builds clean, and `debug_break()` traps
under gdb (`Program received signal SIGTRAP`).

**Leak:** `shell/shell.cpp` includes `<csignal>` and encodes the platform
breakpoint mechanism inline via `#ifdef` (shell.cpp:44-48: `raise(SIGTRAP)` on
sim, `__BKPT(0)` on OPTA).

**Changes:**
- `hal/platform.h` — declare `inline void debug_break();`.
- `hal/simulator.h` — `raise(SIGTRAP);` (move `<csignal>` here).
- `hal/opta.h` — `__BKPT(0);`.
- `shell/shell.cpp` — drop `#include <csignal>`, replace the `#ifdef` block with
  a single `HAL::debug_break();`.

**Validation:** build SIMULATOR, run the `breakpoint` shell command under a
debugger, confirm it traps.

---

## Workstream C — de-libc `utils/time` (calendar leak)

**Leak:** `utils/time.h:12` and `utils/time.cpp:9` include `<ctime>`;
`cal_time_ms` calls `localtime_r` and the header exposes a `time_t` return type
(`unix_time_ms`). This is the only *unguarded*, genuinely cross-platform leak.

**Findings that simplify this:**
- `unix_time_ms` has **zero callers** anywhere in the tree. It is the only thing
  forcing `time_t` into the public header.
- The only used entry points are `format_ms_timestamp` (from `logs/format.h:60`
  and `pages/logs.cpp:92`) and the `cal_time_ms` / `absolute_ms` it sits on.
- `localtime_r` honours `$TZ`, but the header comment promises a **UTC**
  timestamp — so today the simulator output is wrong in any non-UTC timezone.
  This is a latent bug the rewrite also fixes.

**Changes:**
- `utils/time.cpp` — replace the `localtime_r`/`struct tm` breakdown in
  `cal_time_ms` with a pure civil-from-days integer algorithm (the standard
  Howard-Hinnant `civil_from_days`: days → Y/M/D, then `%` for H/M/S). UTC by
  construction. Drop `#include <ctime>`.
- `utils/time.h` — drop `#include <ctime>`; **delete `unix_time_ms`** (dead, and
  the sole reason `time_t` is in the header). If symmetry is preferred over
  deletion, change its return type to `uint64_t` instead.

**Test tie-in:** this turns `utils/time` into pure, host-testable logic — a prime
candidate for the first C++ unit tests (a couple of known-epoch → expected-string
cases over `format_ms_timestamp`).

**Validation:** build SIMULATOR; spot-check a rendered log timestamp against a
known epoch, including a deliberately non-UTC `$TZ` to confirm output stays UTC.

---

## Workstream D — `HAL::reset()` and HAL hardware watchdog

These are new HAL capabilities (not leak fixes). They are grouped because the
simulator watchdog *depends on* `reset()`: when the simulated timer lapses it
must reboot the process. **Implement `reset()` first, then the watchdog.**

### D.1 — `HAL::reset()`

- `hal/platform.h` — declare `inline void reset();` (no return; does not come
  back).
- `hal/opta.h` — call `mbed_reset();` (verify the exact symbol against the
  pinned Mbed headers; `NVIC_SystemReset()` is the fallback if `mbed_reset` is
  not exposed).
- `hal/simulator.h` — stop the emulator thread (and the watchdog monitor thread
  from D.2, if running), then re-exec the process image:
  `execve("/proc/self/exe", saved_argv, environ);`. `execve` replaces the whole
  process atomically, giving a clean "reboot" with the original arguments.

**Required plumbing (sim):**
1. **Capture `argv`.** `argv` is not stashed anywhere global today
   (`src/botanik_sim/main.cpp`). Add a HAL init hook — e.g.
   `HAL::set_reboot_args(int argc, char** argv)` called at the top of `main()`
   — that stores the pointer for `reset()` to pass to `execve`. (`environ` from
   `<unistd.h>` covers the env array.)
2. **Make the emulator thread stoppable.** It is currently `pthread_create` +
   `pthread_detach` with the `pthread_t` discarded (`plcio/emulator.h:197-199`),
   so it cannot be signalled. Options:
   - (a) Retain the `pthread_t` and add a cooperative `std::atomic<bool> _running`
     flag the thread loop checks, plus a `stop_emulator_thread()` that clears it
     and `pthread_join`s. **Preferred** — clean shutdown, releases the PLC shared
     memory before re-exec.
   - (b) Rely on `execve` destroying all threads implicitly and skip the explicit
     kill. Simpler, but leaves the detached thread touching shared memory until
     the image is replaced — rejected per the requirement to kill it first.

**Open question:** where should `reset()` live relative to the emulator? The
emulator thread lifecycle is owned by `plcio/emulator.h`, but `reset()` is HAL.
Cleanest is a small HAL-internal hook/callback that the emulator registers its
`stop` function into, so HAL does not gain a compile dependency on plcio. Decide
during implementation.

**Validation:** trigger `reset()` from a shell command on SIMULATOR; confirm the
process re-execs (same args, fresh uptime, emulator thread restarted, no leaked
shared-memory FDs).

### D.2 — HAL hardware watchdog

Interface (free functions, matching the existing `HAL` style):
- `hal/platform.h` —
  - `inline void watchdog_start(uint32_t timeout_ms);`
  - `inline void watchdog_kick();`
  - (optional) `inline void watchdog_stop();`

**Note:** this is the *hardware* watchdog — distinct from the existing 30 s PLC
software watchdog in `plcio/sync.h`, which guards comms liveness, not the whole
firmware. Keep the names unambiguous.

- `hal/opta.h` — wrap the Mbed OS singleton:
  - `watchdog_start` → `mbed::Watchdog::get_instance().start(timeout_ms);`
    (respect the hardware max-timeout ceiling; clamp or assert).
  - `watchdog_kick` → `mbed::Watchdog::get_instance().kick();`.
- `hal/simulator.h` — simulate the hardware watchdog with an independent monitor
  thread (independence is the whole point: it must fire even if the main loop
  hangs):
  - `watchdog_start(timeout_ms)` records the timeout, sets
    `std::atomic<uint64_t> _last_kick = HAL::uptime_ms()`, and spawns the monitor
    thread once.
  - Monitor loop: `sleep_ms(small interval)`, then if
    `uptime_ms() - _last_kick > timeout_ms`, call `HAL::reset()`.
  - `watchdog_kick()` stores `_last_kick = HAL::uptime_ms()`.
  - `reset()` (D.1) must stop this monitor thread before re-exec.

**Caller wiring (out of scope for this plan, but the obvious follow-up):** the
scheduler/main loop should `watchdog_start()` at boot and `watchdog_kick()` once
per healthy tick, so a hung loop reboots the board.

**Validation (SIMULATOR):** start the watchdog, kick it on a healthy loop and
confirm no reset; then deliberately stall the kick and confirm the monitor thread
triggers `reset()` (process re-execs) after `timeout_ms`.

---

## Suggested order & overall validation

1. **A** and **B** — independent, mechanical, ~15 min each. Build SIMULATOR after
   each.
2. **C** — the only one with real logic; build + timestamp spot-check (incl.
   non-UTC `$TZ`).
3. **D.1** (`reset()` + argv/emulator plumbing), then **D.2** (watchdog atop it).

After every workstream: `make -C build` (SIMULATOR). The OPTA backend edits are
compile-reasoned only here and must be validated on a real cross-build before
they ship.
