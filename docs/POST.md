# Built-in self-tests and asserts

The firmware validates its own invariants two ways: once at boot, with the
power-on self-test (POST), and continuously in debug builds, with `BOTANIK_ASSERT`.
Both report through the logger, so a violation is visible over the terminal rather
than silent. (These are the in-firmware checks; the host-run unit and screen suites
are documented under [C++ tests](@ref cpptests) and
[Terminal emulation tests](@ref termtests).)

## Power-on self-test (POST)

The **POST** is a small, fast self-test `Health::HealthMonitor` runs once on the
firmware's first beat. It validates the cheap structural invariants the rest of the
firmware silently assumes and logs a single verdict line, so a boot is never ambiguous.
It is wiring/sanity validation, not a hardware diagnostic.

### When it runs

The HealthMonitor runs POST on the **first** `step()` it is given - the first `loop()`
beat, right after `setup()` finishes. The shared-region pointers it checks are handed
to it at construction; `setup()` no longer calls it:

```
setup(): UARTs, core sinks, register_pages()
   │
   ▼
setup(): terminal, boot banner, on_setup()
   │
   ▼
setup(): arm HW watchdog
   │
   ▼
loop() beat 1: health.step()
   │
   ▼
POST
```

By the first beat every log sink and the site's `on_setup()` sinks are registered, so
the verdict reaches all of them. Deferring it out of `setup()` keeps the monitor in
charge of its own boot check and runs it once the loop is actually turning.

### What it checks

| Check | Why it matters |
|---|---|
| Every platform-health alarm code the firmware asserts (`SYS_RST`, `CPU_LOAD`, `CPU_JITT`, `STACK`, `CPU_XRUN`) resolves in the merged alarm table | A missing code would make `AlarmModel::set_local(code)` silently no-op, so a real condition would never raise. Guards the alarm-namespace wiring (site bits at the low end, platform bits spliced at the high end). |
| The PLC shared region pointers (in / out) are non-null | A null region means the handler can never sync with the coprocessor. |
| Both regions are 32-byte aligned | The cache-maintenance / DMA path requires it; a misaligned region corrupts cross-core transfers. |
| The logger has at least one registered writer | A logger with no sinks swallows everything, including this very report. |

### Result

POST records its verdict in `post_ok()` and logs one line on the `HEALTH` topic,
tagged with the site name (`Config::SITE_NAME`):

- success -> `INFO  HEALTH: POST ok (site <name>)`
- failure -> `CRITICAL HEALTH: POST failed (site <name>)`, preceded by one
  `CRITICAL HEALTH: POST: <what> <detail>` line per failed check.

A failure is **loud but not fatal**: POST does *not* withhold the watchdog or halt
the boot. The firmware keeps running so the operator can read the diagnostics over
the terminal and the link can still come up; the criticals make the cause
unmistakable in the log rather than leaving a subtly broken unit running silently.

## Assertions (BOTANIK_ASSERT)

Where POST is a one-shot boot check, `BOTANIK_ASSERT(expr)` guards invariants at the
point they must hold. Its mechanics (release compile-out, the debugger-gated trap, the
abort-on-simulator) are documented at @ref utils/assert.h "utils/assert.h".

