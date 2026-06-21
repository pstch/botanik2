# Built-in self-tests and asserts

The firmware validates its own invariants two ways: once at boot, with the
power-on self-test (POST), and continuously in debug builds, with `BOTANIK_ASSERT`.
Both report through the logger, so a violation is visible over the terminal rather
than silent. (These are the in-firmware checks; the host-run unit and screen suites
are documented under [C++ tests](@ref cpptests) and
[Terminal emulation tests](@ref termtests).)

## Power-on self-test (POST)

The **POST** is a small, fast self-test run once during boot by
`Health::HealthMonitor::post()`. It validates the cheap structural invariants the
rest of the firmware silently assumes, and logs a single verdict line so a boot is
never ambiguous. It is wiring/sanity validation, not a hardware diagnostic.

### When it runs

`BotanikFirmware::setup()` calls `_health.post()` near the end of bring-up, on a
fixed schedule that cannot be reordered:

\dot "setup() bring-up order"
digraph post_order {
    bgcolor="transparent";
    rankdir=LR;
    node [shape=box, style=rounded, fontname="Helvetica", fontsize=10, margin="0.08,0.04"];
    edge [arrowsize=0.7];

    n1 [label="begin UARTs"];
    n2 [label="register\ncore sinks"];
    n3 [label="register_pages()"];
    n4 [label="setup terminal"];
    n5 [label="boot banner"];
    n6 [label="on_setup()\n(site post-wiring)"];
    n7 [label="POST", style="rounded,filled", fillcolor="#e8f0fe"];
    n8 [label="arm HW\nwatchdog"];

    n1 -> n2 -> n3 -> n4 -> n5 -> n6 -> n7 -> n8;
}
\enddot

It runs **after** the log sinks (monitor, shell, logs pages) and the site's
`on_setup()` are registered, so its result is actually logged and reaches every
sink, and **before** the hardware watchdog is armed.

### What it checks

| Check | Why it matters |
|---|---|
| Every platform-health alarm code the firmware asserts (`SYS_RST`, `CPU_LOAD`, `CPU_JITT`, `STACK`, `CPU_XRUN`, `RX_TORN`) resolves in the merged alarm table | A missing code would make `AlarmModel::set_local(code)` silently no-op, so a real condition would never raise. Guards the alarm-namespace wiring (site bits at the low end, platform bits spliced at the high end). |
| The PLC shared region pointers (in / out) are non-null | A null region means the handler can never sync with the coprocessor. |
| Both regions are 32-byte aligned | The cache-maintenance / DMA path requires it; a misaligned region corrupts cross-core transfers. |
| The logger has at least one registered writer | A logger with no sinks swallows everything, including this very report. |

### Result

POST records its verdict in `post_ok()` and logs one line on the `BOOT` topic,
tagged with the site name (`Config::SITE_NAME`):

- success -> `INFO  BOOT: POST ok (site <name>)`
- failure -> `CRITICAL BOOT: POST failed (site <name>)`, preceded by one
  `CRITICAL BOOT: POST: <what> <detail>` line per failed check.

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

