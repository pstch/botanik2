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

**The thresholds are parameters, not constants.** What counts as too much load,
jitter or stack usage is a property of the installation, and the number an
engineer wants to move is the one a unit is nuisance-alarming on at 3am, over a
terminal, with no toolchain. `Config::ALARM_*_PCT` are the factory defaults
behind the rows.

`HealthMonitor` reads each one live off its bound row every beat rather than
caching it on a commit serial: it is one indexed load against a comparison that
was already happening, and there is nothing to re-initialise when the operator
moves it. A build that binds nothing reads the bank's inert sink, and `_pct()`
falls back to the `Config` value.

## Power-on self-test (POST)

The **POST** is a small, fast self-test `Health::HealthMonitor` runs once on the
firmware's first beat. It validates the cheap structural invariants the rest of the
firmware silently assumes and logs a single verdict line, so a boot is never ambiguous.
It is wiring/sanity validation, not a hardware diagnostic.

### When it runs

POST is the HealthMonitor's one-time task init, run from `tasks.setup()` near the end
of `setup()` - after every sink is registered, and before the first beat:

@image html bringup.drawio.svg "POST runs from tasks.setup(), the highlighted step: after every sink exists, and still before the first beat."

The same bring-up order is walked through in @ref dev_architecture.

Two constraints pin it there. Every log sink - including the ones a site registers in
`on_setup()` - must already exist, or the verdict goes nowhere. And it must be out of
the first beat: the parameter load beside it is hundreds of storage reads, which read
as a boot overrun against a 10 ms budget.

### What it checks

Two cheap structural invariants - it is wiring validation, not a hardware
diagnostic.

| Check | Why it matters |
|---|---|
| The alarm model fits: `declared_count()` is not above `count()` | The table is generated whole and the generator refuses one that does not fit, so this catches a build whose `ALARM_SLOTS` is below what the site declares. The tail would otherwise be silently dropped. |
| The logger has at least one registered writer | A logger with no sinks swallows everything, including this very report. |

### Result

POST records its verdict in `post_ok()` and logs one line on the `HEALTH` topic,
tagged with the site name (`Config::SITE_NAME`):

- success -> `INFO  HEALTH: POST ok (site <name>)`
- failure -> `CRITICAL HEALTH: POST failed (site <name>)`, preceded by one
  `CRITICAL HEALTH: POST: <what>` line per failed check, with `<detail>` on a
  second line (see the width rule in `logs/format.h`).

A failure is **loud but not fatal**: POST does *not* withhold the watchdog or halt
the boot. The firmware keeps running so the operator can read the diagnostics over
the terminal; the criticals make the cause unmistakable in the log rather than
leaving a subtly broken unit running silently.

## Assertions (BOTANIK_ASSERT)

Where POST is a one-shot boot check, `BOTANIK_ASSERT(expr)` guards invariants at the
point they must hold. Its mechanics (release compile-out, the debugger-gated trap, the
abort-on-simulator) are documented at @ref utils/assert.h "utils/assert.h".

