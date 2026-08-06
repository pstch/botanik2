# Health, POST and asserts {#dev_health}

`HealthMonitor` owns the alarm model, asserts the platform-health alarms from the
scheduler metrics and the storage fault count, runs the power-on self-test, and
produces the watchdog verdict.

The firmware validates its own invariants two ways: once at boot, with the power-on
self-test (POST), and continuously in debug builds, with `BOTANIK_ASSERT`. Both
report through the logger, so a violation is visible over the terminal rather than
silent. (These are the in-firmware checks; the host-run suites are under
[C++ tests](@ref cpptests) and [terminal emulation tests](@ref termtests).)

## The watchdog verdict

`watchdog_ok()` is simply "the loop is alive". With no coprocessor link to lose,
nothing else justifies letting the hardware watchdog fire; control-side fault
escalation belongs to the scan cycle, not here. What the loop does with the verdict -
withdraw control, drop outputs to safe state, withhold the kick - is in
@ref dev_architecture.

## Platform alarms

`alarms/platform.h` defines the generic set - `SYS_RST`, `CPU_LOAD`, `CPU_JITT`,
`STACK`, `CPU_XRUN`, `FLASH_FLT` - asserted each beat from the scheduler metrics and
the storage fault counter. They are appended **after** the site's conditions, so site
alarms always fill from index 0. See @ref user_alarms for what each means
operationally.

## Power-on self-test (POST)

The **POST** is a small, fast self-test `Health::HealthMonitor` runs once on the
firmware's first beat. It validates the cheap structural invariants the rest of the
firmware silently assumes and logs a single verdict line, so a boot is never ambiguous.
It is wiring/sanity validation, not a hardware diagnostic.

### When it runs

POST is the HealthMonitor's one-time task init, run from `tasks.setup()` near the end
of `setup()` - after every sink is registered, and before the first beat:

```
setup(): UARTs, core sinks, register_pages()
   │
   ▼
setup(): terminal, boot banner, on_setup()
   │
   ▼
setup(): tasks.setup()  ──▶  each task's one-time init:
   │                         announce(), POST, the parameter load
   ▼
setup(): arm HW watchdog, enable control
   │
   ▼
loop() beat 1
```

Two constraints pin it there. Every log sink - including the ones a site registers in
`on_setup()` - must already exist, or the verdict goes nowhere. And it must be out of
the first beat: the parameter load beside it is hundreds of storage reads, which read
as a boot overrun against a 10 ms budget (@ref dev_tasks).

### What it checks

Two cheap structural invariants - it is wiring validation, not a hardware
diagnostic.

| Check | Why it matters |
|---|---|
| Every alarm row the firmware raises (`alarm_system_reset`, `alarm_cpu_load`, `alarm_cpu_jitter`, `alarm_stack`, `alarm_cpu_xrun`, `alarm_flash_error`) resolves in the variable table | The monitor binds these six names once at bring-up. One that resolves to nothing lands on the bank's inert sink, so the fault would be raised into a variable nobody reads. In practice it means a site left `BOTANIK_PLATFORM_ALARM_VARS` out of its `g_vars[]` - which is why POST reports the missing rows by name. |
| The logger has at least one registered writer | A logger with no sinks swallows everything, including this very report. |

### Result

POST records its verdict in `post_ok()` and logs one line on the `HEALTH` topic,
tagged with the site name (`Config::SITE_NAME`):

- success -> `INFO  HEALTH: POST ok (site <name>)`
- failure -> `CRITICAL HEALTH: POST failed (site <name>)`, preceded by one
  `CRITICAL HEALTH: POST: <what> <detail>` line per failed check.

A failure is **loud but not fatal**: POST does *not* withhold the watchdog or halt
the boot. The firmware keeps running so the operator can read the diagnostics over
the terminal; the criticals make the cause unmistakable in the log rather than
leaving a subtly broken unit running silently.

## Assertions (BOTANIK_ASSERT)

Where POST is a one-shot boot check, `BOTANIK_ASSERT(expr)` guards invariants at the
point they must hold. Its mechanics (release compile-out, the debugger-gated trap, the
abort-on-simulator) are documented at @ref utils/assert.h "utils/assert.h".

