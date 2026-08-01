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

The HealthMonitor runs POST on the **first** `step()` it is given - the first `loop()`
beat, right after `setup()` finishes, not from `setup()` itself:

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

Two cheap structural invariants - it is wiring validation, not a hardware
diagnostic.

| Check | Why it matters |
|---|---|
| Every platform-health alarm code the firmware asserts (`SYS_RST`, `CPU_LOAD`, `CPU_JITT`, `STACK`, `CPU_XRUN`) resolves in the merged alarm table | A missing code would make `AlarmModel::set_local(code)` silently no-op, so a real condition would never raise. Guards the alarm-namespace wiring: site conditions at the low end, the platform set appended after. |
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

