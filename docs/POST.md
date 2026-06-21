# Built-in self-tests and asserts

The firmware validates its own invariants two ways: once at boot, with the
power-on self-test (POST), and continuously in debug builds, with `BOTANIK_ASSERT`.
Both report through the logger, so a violation is visible over the terminal rather
than silent. (These are the in-firmware checks; the host-run unit and screen suites
are documented under [C++ tests](@ref cpptests) and
[Terminal emulation tests](@ref termtests).)

## Power-on self-test (POST)

The **POST** is a small, fast self-test run once on the firmware's first beat by
`Health::HealthMonitor`. It validates the cheap structural invariants the rest of
the firmware silently assumes, and logs a single verdict line so a boot is never
ambiguous. It is wiring/sanity validation, not a hardware diagnostic.

### When it runs

The HealthMonitor runs POST itself, on the **first** `step()` it is given - the
first `loop()` beat, right after `setup()` finishes. The shared-region pointers it
checks are handed to the monitor at construction; `setup()` no longer calls it:

\dot "POST on the first health step"
digraph post_order {
    bgcolor="transparent";
    rankdir=TB; nodesep=0.35; ranksep=0.5;
    node [shape=box, style=rounded, fontname="Helvetica", fontsize=10, margin="0.08,0.04"];
    edge [arrowsize=0.7];

    n1 [label="setup():\nUARTs, core sinks,\nregister_pages()"];
    n2 [label="setup():\nterminal, boot banner,\non_setup()"];
    n3 [label="setup():\narm HW watchdog"];
    n4 [label="loop() beat 1:\nhealth.step()"];
    n5 [label="POST", style="rounded,filled", fillcolor="#e8f0fe"];

    n1 -> n2 -> n3 -> n4 -> n5;
}
\enddot

By the first beat every log sink (monitor, shell, logs pages) and the site's
`on_setup()` sinks are already registered, so the verdict is actually logged and
reaches every sink. Deferring it out of `setup()` keeps the monitor in charge of
its own boot check (no caller has to remember to run it) and runs it once the loop
is actually turning.

### What it checks

| Check | Why it matters |
|---|---|
| Every platform-health alarm code the firmware asserts (`SYS_RST`, `CPU_LOAD`, `CPU_JITT`, `STACK`, `CPU_XRUN`, `RX_TORN`) resolves in the merged alarm table | A missing code would make `AlarmModel::set_local(code)` silently no-op, so a real condition would never raise. Guards the alarm-namespace wiring (site bits at the low end, platform bits spliced at the high end). |
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
the boot. The firmware continues to run so the operator can read the diagnostics
over the terminal and the link can still come up; the criticals make the cause
unmistakable in the log rather than leaving a subtly broken unit running silently.

## Assertions (BOTANIK_ASSERT)

Where POST is a one-shot boot check, `BOTANIK_ASSERT(expr)` (in
`utils/assert.h`) guards invariants at the point they must hold. It is a
development aid: in **release** builds (DEBUG undefined) it compiles out entirely,
so it costs nothing on a shipped unit.

In **debug** builds a failed assertion logs a `CRITICAL` entry on the `ASSERT`
topic - through the logger registered via `set_assert_logger()`, so the failure
reaches the same sinks as everything else - and then:

- traps into an attached debugger, gated on `HAL::under_debugger()` (a bare
  breakpoint with no debugger HardFaults the OPTA, so the gate matters);
- otherwise `abort()`s on the simulator, for a loud failure in a headless or CI run;
- otherwise (hardware, no debugger) it has already logged, and continues.

The split is deliberate: asserts catch programmer errors loudly during development,
while POST catches wiring and integration errors at every boot, including on a
release unit in the field.

