# Field bus resilience: dead devices, wedged devices, and the interleaving

The fix path for the failure behaviour reviewed in `modbus-fault-behaviour`
(F1 to F5), plus the two scheduling defects that review did not reach: outputs
and inputs can starve each other, and a dead device's probe is scheduled ahead of
every live transaction on the bus.

Everything here is `modbus/rtu/`. The TCP corners are untouched.

Phases are ordered so each one is safe to stop after. Phase 0 comes first because
none of the rest is verifiable without it.

**Status.** Done, on `mb-solidify`. Every phase below is landed and the gate is
green; read this as the record of a finished piece of work rather than a
worklist. The one thing it did NOT do is listed under "what this does not do" at
the bottom, and the slave-side failsafe there is a commissioning task that no
firmware change can cover.

One thing phase 0 turned up that the plan did not predict: the simulator paces
its bytes against the HOST clock, so a test stepping a virtual beat sees the port
as permanently busy and exactly one transaction ever leaves. The harness opens
the port at baud 0 to say that byte time is not what it is testing. A virtual
clock in HAL would be the general fix.

It landed on `main` while this branch was in flight (`BOTANIK_SIM_CLOCK=manual`,
with a `_virtual_ns()` the harness drives). It is read once per PROCESS, so
adopting it here is a change to `doctest_main` and to every case in the binary
rather than to one fixture: worth doing as its own piece of work, deliberately
not folded into this one.

---

## What the investigation found

`_pick()` scores every block in one pass and takes the highest. The tiers are a
decade apart so nothing inside one can climb into the next, and the addend inside
a read tier is lateness relative to that block's own period, in permille, capped
at 999999.

| Tier | Score | Meaning |
|---|---|---|
| `T_SAFE` | 4000000 | the field is owed a safe state |
| `T_STARVED` | 3000000 | a read a whole period late |
| `T_WRITE` | 2000000 | a demand that has changed |
| `T_READ` | 1000000 | a reading that is due |

Four facts follow, and the first two are the ones that matter.

**1. Writes have no escalation path, reads do.** A non-starved read can never
outrank a dirty write (`1000000 + 999999 < 2000000`), which is intended. But a
starved read always outranks one (`3000000 + n > 2000000 + 1`), and a dirty write
sits at `T_WRITE` for ever however long it has waited. So a read is bounded at
roughly two periods late, and a **write is not bounded at all**: while any read
is starved, no ordinary write goes out. During a mass outage every dead block is
permanently starved, so output delivery stops for the whole detection window,
which is the moment one most wants outputs driven. The 5 s `MODBUS_REWRITE_MS`
keepalive is starved with it, so a correctly configured slave-side failsafe will
trip. Only `_urgent` safe-state writes get through, because they sit above
`T_STARVED`.

**2. A probe is scheduled ahead of everything.** When a backoff expires the block
is late by the whole backoff interval, so its permille addend is enormous:
30 s late on a 1 s rung scores `3000000 + 30000`, above every live read and every
write on the bus. The probes of N dead devices therefore arrive as one contiguous
burst. Twelve dead devices is 3.6 s of back-to-back response timeouts every
backoff interval, during which nothing but a safe-state write can move. **The
least valuable and most expensive transaction on the bus is scheduled first**, and
raising the backoff cap makes that burst rarer without making it smaller.

**3. A read's retry is deferred by a whole poll period.** `receive_step()` sets
`due_ms = now_ms + b.period_ms` on every completed turn, success or failure
alike, so `MODBUS_RETRIES`' "immediate retry" is immediate only for a write
block, which retries after `MODBUS_WRITE_MIN_MS`. Two consequences: there is no
600 ms contiguous stall when one device dies (good, and it corrects figure 3 of
the review, which drew the two timeouts back to back), and **declaring a device
dead takes two poll periods** (bad). That is 2 s on the 1 s rung, 20 s on the
10 s rung, and two minutes on the 60 s rung, with a 300 ms timeout burned every
period throughout. A device carrying only slow rows is never usefully backed off.

**4. Nothing bounds what the bus spends on devices that are not answering.** A
late dead block and a late live block score identically, so they compete on equal
terms. Twelve sensors on a cut spur need 12 x 2 x 300 ms = 7.2 s of bus to be
declared dead and they simply take it.

---

## Decisions taken

| # | Question | Decision |
|---|---|---|
| D1 | How every block is ranked | **Unified lateness.** One scale for reads and writes: how far a block is through the lateness it is allowed, in permille. The tier ladder goes. |
| D2 | What stays absolute | **The safe state, and only that.** Deadline-driven scheduling degrades badly under overload, everything misses at once, and the one thing that must not degrade is dropping the field to safe. |
| D3 | How the cost of not-answering devices is bounded | **An explicit interleave**: one probe, then at least `MODBUS_PROBE_SPACING_MS` of live traffic, then the *next* dead device. A gate and a cursor in `_pick()`, not a state in the transaction FSM. |
| D4 | Interleave measured in what | **Time, not transactions.** A probe costs a full response timeout and a live two-register read costs 22 ms, so one probe per four transactions is 77 % of the bus. |
| D5 | The deferred retry | **Capped, not removed**: `due_ms = now + min(period_ms, MODBUS_RETRY_DELAY_MS)`. Detection latency becomes a property of the fault rather than of the rung. |
| D6 | Backoff cap | **60 s**, up from 30. A dead device is a work order, not something the firmware recovers from, and the probe stops being disruptive once D3 is in. |
| D7 | A refused write | **Penalised per block**, not per device. The device is alive and its reads must keep their rates; only the block that cannot be delivered backs off. |
| D8 | How long a demand may sit behind reads | **500 ms** (`MODBUS_WRITE_DEADLINE_MS`). The one number here with a plant consequence rather than a bus one, and the one to revisit against a real actuator. |

---

## Phase 0 - the test scaffold

Nothing below is worth landing unverified, and the seams already exist:
`next_block()` is public precisely so the scheduling decision can be pinned
without a wire, and the simulator's `FieldBus` stands a real `RtuSlave` up per
unit over a socketpair.

### 0.1 Fault injection on the simulated field bus [DONE]

Three states per simulated unit, set from the harness: **absent** (drop every
request), **late** (answer after a delay, past the deadline), **refusing**
(answer `EX_ILLEGAL_ADDRESS` to a nominated function code or register). Absent is
the only one the current `FieldBus` can express, and only by not building the
slave at all.

### 0.2 Scheduling tests over `next_block()`

The existing cases pin the tiers. Add the ones that pin the *policy*: a device
crossing into backoff, the probe cadence, recovery and `reassert_outputs()`, and
one case per finding below. These run with no port and no bytes, so they are
cheap enough to be exhaustive.

---

## Phase 1 - the two latches [DONE]

Correctness only. Neither changes a scheduling policy, and both are small enough
to land and verify independently of the ladder work.

### 1.1 The safe-state urgency latches for ever (F1) [DONE]

`_track_control()` spends `_urgent` by sweeping for any remaining dirty write
block, and the sweep does not consult `_due()`. A block on a backed-off device is
dirty and will stay dirty, because `_pick()` will never offer it the bus, so the
sweep always finds one and returns early. Re-enabling control does not clear the
flag either. From the first `control disable` with a dead output device on the
segment, every dirty write on a live device is treated as a safe state for the
life of the boot.

**Fix.** Consult `_due(b, now_ms)` in the clearing sweep, so only blocks that can
actually go out hold the urgency. Clear `_urgent` on the rising edge of control
authority as well, which makes the flag's lifetime bounded by construction rather
than by the sweep finding nothing.

**Test.** Withdraw control with one device backed off, confirm the urgency is
spent once the reachable safe-state writes are out, and that an ordinary dirty
write is then throttled by `MODBUS_WRITE_MIN_MS` again.

### 1.2 A refused write is retried five times a second, for ever (F2) [DONE]

`_answered()` correctly clears the miss count on an exception: the device heard
us. But `_settle()` returns before `clear_dirty()`, so the block stays dirty and
is re-sent every `MODBUS_WRITE_MIN_MS` until someone notices. A register map that
does not match the device, which is the commonest commissioning error on a field
bus, therefore costs about an eighth of the bus and one warning per attempt into
the logger's token bucket.

**Fix.** Per D7, a penalty on the block rather than the device. Add
`Block::refusals` and, on `ERR_EXCEPTION` for a write, push `due_ms` forward by a
delay doubling from `MODBUS_WRITE_MIN_MS` to `MODBUS_REFUSAL_MAX_MS` (60 s).
Cleared by the first write that is accepted. The warning follows the same
edge-triggered rule `_missed()` already uses: logged once on entry, not once per
attempt.

**Test.** A refusing unit; assert the write interval grows and the device's reads
keep their declared rates throughout.

---

## Phase 2 - one clock for every block [DONE]

The tier ladder goes. Every block ranks on a single number: **how far it is
through the lateness it is allowed**, in permille.

```
    eligible   iff  now_ms >= due_ms
    urgency     =   (now_ms - due_ms) * 1000 / deadline_ms
```

Highest urgency wins, ties to the lowest block index, and a block that is not yet
due is not eligible at all. The read/write distinction moves out of the
comparison and into how the two fields are *maintained*:

| Block | `due_ms` | `deadline_ms` |
|---|---|---|
| read | last completion + `period_ms` | `period_ms` - poll any later and the row flaps between valid and stale |
| write, dirty | dirty since + `MODBUS_WRITE_MIN_MS` | `MODBUS_WRITE_DEADLINE_MS` |
| write, clean | `last_ms` + `MODBUS_REWRITE_MS` | `MODBUS_REWRITE_MS` |

`MODBUS_WRITE_MIN_MS` keeps exactly its present meaning: it is when a dirty write
becomes *due*, so a chattering demand still cannot monopolise the bus.

### 2.1 What this deletes

- `T_STARVED`. The starvation floor was a tier jump at "one whole period late";
  it is now just `urgency == 1000` and needs no special case. The reason the
  floor was introduced in the first place - that ranking by absolute
  milliseconds hands the bus to the fast rungs for ever - is already what the
  permille normalisation fixes.
- `T_WRITE` and `T_READ`. There is one scale.
- `T_WRITE_LATE`, which an earlier draft of this plan added above `T_STARVED` to
  stop writes being starved by starved reads. Under one clock a write's urgency
  grows exactly like a read's, so the starvation it was patching cannot happen.
  Finding 1 above is fixed by construction rather than by a tier that outranks
  it.
- The `is_read(b.fc)` branch in `_pick()`.

### 2.2 What stays absolute

Per D2, one pre-empt above the scale and nothing else: a dirty write while
`_urgent` is set. Implemented as a pre-pass returning the first such block, so
the urgency scale never has to reserve headroom for it and cannot be climbed
into by a sufficiently late block.

### 2.3 The behaviour change to accept

A freshly dirty write no longer automatically beats a read that is merely due; it
wins once it is further through its deadline. Against a 1 s rung with a 500 ms
write deadline a demand still reaches the field within 500 ms worst case, which
is tighter than today's typical rather than looser, but it *is* a policy change:
`tests/test_modbus_master.cpp`'s case "an ordinary dirty write goes before a read
that is merely due" encodes the old rule and is replaced, not repaired. Say so in
the commit.

Arithmetic note: the numerator is `uint64_t` milliseconds and the result is
clamped into `uint32_t` before comparison. `deadline_ms` is guarded against zero
where it is derived, in `_slot_of()`, not at every comparison.

**As built.** The derivation lives in `RtuClient::_slot_of()`, which returns the
`(due, deadline)` pair for a block and is the one place the two kinds differ;
`_pick()` compares the same number for every block and no longer mentions
`is_read()`. One subtlety the plan did not: a block exactly at its due time has
urgency 0, so the selection tests `best == NO_BLOCK` as well as `> urgency`, or a
read that has only just come due is never picked at all.

---

## Phase 3 - the probe interleave [DONE]

D3 and D4, and the answer to "twelve devices go down at once". One dead device is
polled, then live traffic for a spacing interval, then the *next* dead device.

### 3.1 The gate and the cursor

A block is a **suspect** if its device has `misses > 0`. Two pieces of state on
the client:

- a **cursor** over the device set, naming the single suspect currently eligible.
  It advances after each probe, so the rotation is a guarantee rather than a
  by-product of which suspect happens to be latest;
- a **gate**, shut for `MODBUS_PROBE_SPACING_MS` after any transaction that
  failed. While it is shut no suspect is eligible at all.

When the gate is open the cursor's block joins the ordinary urgency comparison
like anything else. Being enormously late it normally wins, so a probe does not
have to wait for an idle bus, which matters because a dead device that has come
back must be noticed even while the bus is busy.

Nothing else changes: a probe is an ordinary transaction over the same framer,
the same settle path and the same gap. The interleave is a scheduling rule, so it
lives in `_pick()` and not in the transaction FSM, which is about the wire.

### 3.2 What it guarantees

At most one unanswered transaction per spacing interval, so the share of the bus
spent on devices that are not answering is at most

```
    timeout / (timeout + spacing)   =   300 / 1800   =   17 %
```

and **that figure does not depend on how many devices are down**.

**As built, with one correction the plan got wrong.** The bound is on the STEADY
STATE, not on discovery. A device that has never missed is presumed good, so its
first failure cannot be gated - there is no way to know a device is down without
trying it - and N devices therefore cost N response timeouts once. Only the
retries and the probes that follow are interleaved. That is inherent rather than
a shortcoming of the design, but the plan claimed a flat bound and it is not one.

Measured on six absent devices against a live one, in the doctest harness:

| | before | after |
|---|---|---|
| closest pair of unanswered transactions | 310 ms | 1800 ms |
| probes in a 30 s steady state | 22 | 16, i.e. 16 % of the bus |
| live block polls in that 30 s | 26 | 28, against a nominal 30 |
| all six declared dead after | 13.6 s | 16.3 s |

Discovery is the one thing that got slower, by about 20 %, and that is the trade
being made: the bus stays healthy throughout instead of being taken for seven
seconds. The first device is still declared dead within a couple of seconds; it
is the sixth that waits.

### 3.3 Detection latency independent of the rung

Per D5, on a miss set `due_ms = now_ms + min(period_ms, MODBUS_RETRY_DELAY_MS)`
so the retry no longer waits a whole poll period. A device is then declared dead
about 1.6 s after it goes silent whatever rungs its rows sit on, instead of
taking two minutes for a 60 s block. Safe only because 3.1 lands with it, which
is why they are one phase.

### 3.4 The backoff cap

`MODBUS_BACKOFF_MAX_MS` 30000 to 60000 (D6). One line and a comment, but it
belongs after 3.1: on today's code it would make the probe burst rarer and no
smaller, which is not an improvement.

**Tests.** Twelve devices absent at once: assert that live blocks keep being
picked throughout, that no two failed transactions fall inside one spacing
interval, that the cursor visits every suspect before revisiting one, and that
all twelve reach backoff.

---

## Phase 4 - the device model [DONE]

### 4.1 A per-device response timeout (F3) [DONE]

`MODBUS_TIMEOUT_MS` is global, so a device slower than 300 ms is indistinguishable
from a dead one: it answers after the master has stopped listening, the answer is
flushed as `_stale_rx` by the next transaction, and it sits in backoff for ever
with nothing wrong with it. The model already carries `min_gap_ms` for the
analogous reason on the other side of the answer.

**Fix.** `DeviceModel::timeout_ms`, 0 meaning the bus default, added through the
factories (`meter()`, `relay_board()`, `io_board()`) rather than as a positional
field, for the reason already recorded in `device.h`. Passed to `_framer.arm()`
beside the gap that comes from the same model. Note it also feeds phase 3.2's
arithmetic: the gate's guarantee is stated against whatever timeout the device
actually got.

**Test.** A late unit inside its own declared timeout answers and stays live; the
same unit under the bus default is faulted. The two cases run the SAME site
device table with one entry's model copied and given a deadline, built at run
time from the site's own models - writing a second table out by hand would have
planned the other nineteen devices as generic and tested two things at once.

---

## Phase 5 - diagnosis [DONE]

### 5.1 A line fault is not twenty device faults (F4) [DONE]

A babbling unit, a duplicate address or a second master corrupts whichever
transaction is in flight, and `_missed()` charges it to whichever device was
being polled. Every healthy device is faulted in turn until the segment is
silent. The backoff contains the damage, so this is a diagnostic failure rather
than a bandwidth one: the operator is told that twenty devices failed at once,
which is the one hypothesis that is certainly wrong.

**Fix.** An `alarm_fieldbus_line` row beside `alarm_fieldbus` in
`modbus/rtu/alarms.h`, raised when `_dead` reaches `_plan.device_count()`, with a
remediation note naming termination, stubs and a second master. The distinction
is worth a row of its own because "the segment is gone" and "a sensor is gone"
send different people with different tools.

**As built:** register **998**, not 997 as first written - 997 is the scan
cycle's `alarm_scan_late`. The site test caught the collision, which is what it
is for; the library's 990-999 reserve is now 990-995 platform, 996 field bus, 997
scan, 998 line.

### 5.2 Publish what the framer already knows [DONE]

`timeouts`, `crc_errors`, `bad_frames`, `stray_bytes` and `_stale_rx` are kept
apart precisely because they point at different physical faults, and nothing
reads any of them. Surface the split in the `modbus` shell command, and the two
that an operator away from the console needs as status rows. A bus failing on CRC
errors is a wiring or termination fault; one failing on clean timeouts is a power
or addressing fault.

**As built:** a `Line` row in `modbus` carrying crc / bad frame / timeout / stray
bytes, plus a line naming the segment when every device is quiet, and two status
rows - `sys_fieldbus_crc` (903) and `sys_fieldbus_stray` (904, the framer's stray
bytes plus the client's own `_stale_rx`, since both mean "something talked out of
turn"). Both saturate at 65535 rather than wrapping: a counter that rolls over to
nothing reads as a bus that recovered. They are written once per transaction
rather than only on a health transition, because they move with the frames and
not with the device set.

---

## Phase 6 - documentation [DONE]

Rewrite `modbus/rtu/client.h`'s `cl_dead` section, which describes the backoff as
the whole answer, and replace `_pick()`'s tier table with the one clock and the
two maintenance rules. Correct figure 3 of the review per fact 3 above. Update
`docs/integration/config.md` with the new values, and note beside
`RtuClient::healthy()` that having no consumer is deliberate.

**As built.** `cl_dead` is rewritten and a `cl_rank` section added for the one
clock. `config.md` turned out to have no field-bus section at all - the Modbus
values had never been in the reference - so it gained one covering the line, the
deadlines and the whole failure policy, not only the values this work added.

---

## Config, new and changed

All of these live in `libbotanik/config/modbus.h`, the concern header the module
already includes, and follow its existing pattern without exception: an
`#ifndef`-guarded `BOTANIK_<NAME>` default, then `BOTANIK_CFG(type, NAME)`. So
every one is site-overridable from `sites/<name>/config.h` and settable from the
command line, and none of them is a literal in `client.cpp`.

`MODBUS_BACKOFF_MAX_MS` is already such a value; the change to it is the default
alone, plus a comment saying what the probe now costs and why 60 s is affordable
where it was not before.

| Value | Now | After | Why |
|---|---|---|---|
| `MODBUS_BACKOFF_MAX_MS` | 30000 | **60000** | D6 |
| `MODBUS_PROBE_SPACING_MS` | - | **1500** | live traffic owed between two probes; with the timeout it fixes the bus share at 17 % |
| `MODBUS_WRITE_DEADLINE_MS` | - | **500** | how long a demand may sit behind reads (D8) |
| `MODBUS_RETRY_DELAY_MS` | - | **500** | caps the deferred retry, so detection latency is not the rung's |
| `MODBUS_REFUSAL_MAX_MS` | - | **60000** | ceiling on the per-block refusal penalty |
| `DeviceModel::timeout_ms` | - | **0 = bus default** | a slow device's own deadline |

Three values from an earlier draft of this plan are **not** needed under one
clock and an explicit interleave: `MODBUS_SILENCE_BUDGET_PCT`,
`MODBUS_SILENCE_BURST_MS` and `MODBUS_WRITE_STARVE_MS`.

---

## What this does not do

**It does not make the master survive its own failure.** Every bound above
assumes the master is the thing still working. The slave-side failsafe timer on
each output module is the only mechanism that covers a severed trunk, a wedged
master, or a reset that beats the safe-state write to the wire.
`MODBUS_REWRITE_MS` is already the keepalive that timer watches for, and phase 2
is what stops read pressure starving it. Confirming it is actually configured on
the reference site's RO-4 boards is a commissioning task, not a firmware one.

**It does not withdraw control on a field fault.** `RtuClient::healthy()` still
has no consumer, deliberately: a failed humidity sensor should raise an alarm,
not shut a greenhouse down.

**It does not shorten the response timeout.** 300 ms is already tight against the
budget note's arithmetic. The cost of a dead device is bounded here by scheduling
less of it, not by giving up on it sooner.
