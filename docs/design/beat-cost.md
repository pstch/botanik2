# What the beat costs

Every task on the beat, what it costs, and where the spikes are. The mechanism -
groups, phases, the registry - is @ref dev_scheduler; this page is the budget.

**These are static estimates, not measurements**: instruction counts taken from
Cortex-M7 disassembly of the real code at `-Os`, converted at 480 MHz and
IPC 1.3. They are for sizing and for knowing where to look, not for a datasheet.
Replace them with `TaskCost` readings from the TASKS page as soon as there is
hardware to read.

## The table

Site `gh_mlb41`: 259 variable rows, 29 alarm conditions, 2 control programs.
Beat = `TASK_PERIOD_MS` = 10 ms.

| # | Task | Group | Period | Phase | Typical | Worst |
|---|---|---|---|---|---|---|
| 1 | `console_rx` | fast | 10 ms | input | 1.3 us | 330 us |
| 2 | `monitor_rx` | fast | 10 ms | input | 0.4 us | 530 us |
| 3 | `expire_stale` | bkgnd | 500 ms | input | **18 - 34 us** | 40 us |
| 4 | `params` | slow | 100 ms | config | 0.1 us | **20 - 130 ms** (once) |
| 5 | `health` | slow | 100 ms | service | **14 us** | 55 us / 1.2 ms storm |
| 6 | `control_latch` | fast | 10 ms | service | ~0.05 us | ~0.05 us |
| 7 | `net` | fast | 10 ms | service | 6 - 10 us | **800 - 1900 us** |
| 8 | `scan` dispatch | fast | 10 ms | execute | 0.2 us | 1.3 us |
| 8a | - `supply_watch` | - | 100 ms | execute | **21 us** | **69 us** |
| 8b | - `supply_report` | - | 1000 ms | execute | **21 us** | 22 us |
| 9 | `site` (`on_step`) | fast | 10 ms | execute | ~0 | ~0 |
| 10 | `logger` | fast | 10 ms | present | 1.5 us | 350 - 450 us |
| 11 | `console` | main | 50 ms | present | 10 us | 330 us |
| 12 | `monitor_log` | fast | 10 ms | present | 0.02 us | 367 us |
| 13 | `console_tx` | fast | 10 ms | present | 0.02 us | 100 us |
| 14 | `monitor_tx` | fast | 10 ms | present | 0.02 us | 1400 us (bounded) |

**A fast-only beat is ~11.5 us, or 0.12 % of the period.** The worst *regular*
beat is t = 500 ms, where `console`, `params` and `expire_stale` coincide: ~48 us,
0.5 %. Steady-state headroom is enormous. Every real risk is a spike.

## Where the time actually goes

**Linear name resolution dominates almost everything.** `Bank::find()` is a
`strcmp` scan of all 259 rows, and it sits on five hot paths:

| Call site | Share of that task |
|---|---|
| Control programs - `c.io.var("name")` every activation | **99 %** |
| `HealthMonitor::_assert_platform_alarms` - 159 `strcmp`s for 6 fixed names | **71 %** |
| `MonitorPage::_ref_at()` - an O(255) scan *per drawn row* | 36 k cycles |
| `NetworkStack` - `param_serial()` -> `get_int()` -> `find()` per component per beat | ~1150 cyc/beat |
| Telemetry `_publish_scalars` - full-table walk | 16 us |

Every name shares a prefix (`grid_`, `cli_`, `alarm_`), which is the worst shape
for a linear scan: each `strcmp` matches several bytes before diverging.

This is not the variable model being wrong - addressing rows by name is what
keeps a site from maintaining an enum of indices (@ref dev_vars). It is that the
resolution is meant to happen **once, at bring-up, into a `VarRef`**, and the
declarative tables do exactly that. Hand-written code on the beat does not.
Caching a function-local `static VarRef` - the pattern the control programs
already use for their `Ton`/`Sr`/`Deadband` blocks - is ~40x on the top two rows.

## The spikes, and what bounds them

**`monitor_tx` had no per-activation cap.** `transmit()` drained until the ring
emptied or the driver refused. On the OPTA the monitor is USB CDC, not the UART
its `MONITOR_BAUD` implies (`hal/opta.h`), and with no host attached
`availableForWrite()` always returns 1 - so it drained all 4095 bytes in one
activation. With a host attached `USBCDC::send()` *blocks* per byte, putting tens
of milliseconds inside one beat. Now bounded by `SERIAL_TX_PER_STEP`.

The console UART was never exposed to this: its `availableForWrite()` reflects a
real hardware FIFO, and `present()` self-caps the TX ring at `_tx_cold_depth`
(61 bytes at 4800 baud) anyway.

**Bring-up does 423 flash reads, and they no longer land in a beat.**
`Params::on_setup()` runs `load_params()` (164 keys) and `load_overrides()` (259
- every row, not just the parameters). At 50 - 300 us per Mbed TDBStore read that
is 20 - 130 ms, the largest single stall in the firmware.

It used to happen on the first `PHASE_CONFIG` dispatch, because init was driven
lazily from a component's first `step()` - so it sat inside the first beat and
read as a boot overrun against a 10 ms budget. `Registry::setup()` now runs every
registered task's init from `BotanikFirmware::setup()`, before the loop exists.
The 423 reads are unchanged; what changed is that they are no longer measured
against a beat that had not started.

The count is still worth reducing: `load_overrides()` probes a storage key for
every row in the table, not just the ones that can hold an override.

**Telemetry sends one TCP packet per topic.** 38 - 80 topics x ~20 us once per
30 s. MQTT 3.1.1 has no multi-topic PUBLISH, so the fix is either pacing (a
cursor and a per-activation cap, the idiom the log rows and the terminal budget
already use) or coalescing several self-delimiting PUBLISH packets into one
`tcp_send()`.

**Alarm transitions cost 10 - 40 us each**, in `vsnprintf` plus an MQTT publish.
A storm that trips many conditions at once is the only realistic path to a beat
overrun in the health path.

## Known gap: offsets stagger within a group, not across

`Registry::add()` derives a task's offset from its rank *within its group*, so
group-mates never share a beat (@ref dev_scheduler). Across groups they still
can: at t = 500 ms, `console` (main), `params` (slow) and `expire_stale`
(background) all come due together. Harmless at 48 us; a global rank would fix it
if peak per beat ever became the constraint.

## What would move these by more than 2x

- **Build type.** The default is `DEBUG` / `-O0`, which is 2 - 4x worse than the
  `-Os` these assume. The Arduino deploy path uses `-Os`.
- **Mbed's per-byte `PlatformMutex`** in the serial driver, assumed at 400 - 800
  cycles. It is ~97 % of all serial cost, and it is an assumption.
- **lwIP socket call cost**, assumed 3 - 17 us for a non-blocking `recv`/`send`.
  The single largest unknown in the network figures.
- **`newlib` vs `newlib-nano`** for `memcmp`/`strcmp`/`snprintf`. Byte-wise
  versus word-optimized is a 2 - 4x swing on the console diff and on every name
  lookup.
- **D-cache residency.** The descriptor table is 259 x 72 B = 18.6 KB against a
  16 KB D-cache, so `expire_stale` (500 ms rate, always cold) and the telemetry
  sweep both pay line fills the warm-cache figures ignore.

Measure the first three with a DWT cycle counter before trusting any of this.

## Per-program CPU

The accounting unit is the **task**, not the group: "the slow group cost 3 ms"
does not say which of its five members to look at. Roll up to the group for the
summary.

`Tasks::TaskCost` already holds exactly this (`us_last`, `us_max`, `us_total`,
`runs`, `us_mean()`); what is missing is a `Plc::Program` carrying one, measured
with two `HAL::Clock::uptime_us()` reads around `step()`. The program already owns
its timer, so the cost belongs beside it rather than in a parallel array. Report a
windowed peak and a mean - an all-time max never decays, so one outlier pins it
for the life of the boot.
