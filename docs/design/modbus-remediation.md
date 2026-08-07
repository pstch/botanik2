# Modbus remediation plan

The fix path for every finding in `modbus-review.md`. Four design decisions were
taken by the maintainer and are recorded in **Decisions** below; the rest follow
from the code.

Phases are ordered so that each one is safe to stop after. Every phase ends with
a build and the doctest suite; `make check` is the maintainer's gate.

**Status.** Phase 0 is done, along with the correctness fixes marked below and
the whole of the site register map. The rest is tracked as tasks 1-17; each
heading below carries its task number, so the plan explains *why* and the task
list says *what is left*. Nothing here is stale by the time you read it: if a
heading says DONE, it is committed on `modbus-review-fixes`.

| Phase | State |
|---|---|
| 0 - structure | **done** - gates, includes, dead code, hygiene |
| 1 - test scaffold | partly done - site bindings and the RTU slave; map/image/pdu are task 15 |
| 2 - the field bus works | partly done - the register map, baud, merge gap, coil cap, signed pairs; tasks 1-7 |
| 3 - the TCP slave | tasks 8-11 |
| 4 - the simulated device model | partly done - FC16 and the CRC resync; task 12 |
| 5 - transaction pacing | task 13 |
| 6 - the readback surface | task 14 |
| 7 - simplification | task 16 |
| 8 - documentation | task 17 |

---

## Decisions taken

| # | Question | Decision |
|---|---|---|
| D1 | Object index vs raw register in `Binding::reg` | **One field, holding the real Modbus protocol address.** The object index survives as prose in the row's `notes` column. |
| D2 | Replacement for the blind RX flush | **Echo-matching discard** - swallow bytes while they match the frame just sent, frame from the first divergence. Correct whether or not the board echoes. |
| D3 | Bulk parameter write vs synchronous flash | **Both** - a bounded persist queue drained N per beat, plus `EX_SLAVE_BUSY` when the queue is already full. |
| D4 | The unread readback surface | **Finish it** - `RtuClient` onto `SystemContext`, a `modbus` shell command, drop the redundant `service_rx()`. |

---

## Correction to the review

`modbus-review.md` §1.1 diagnosed the meter addresses as "off by a factor of
two". That was wrong, and the plan below supersedes it.

`vars.h:29-34` carries a `@warning` stating that `Binding::reg` holds *"the
device-profile OBJECT INDEX on the meters"* and that *"the RTU master must
translate object index -> register per device model."* The site's numbers are
therefore self-consistent object indices, not broken registers. The defect is
that **the translation the warning mandates does not exist** - `plan.cpp` cuts
blocks on `bind.reg` and `image.cpp:37` offsets into the payload with it, both
treating it as a protocol address.

The outcome on the wire is the same (every 32-bit row after the first in a block
decodes half of its predecessor), but the fix is different, and two further
mismatches came out of the vendor documentation:

- **The SDM630's measurements are Input Registers, function code 04.** The
  manual: *"Each parameter is held in the 3X registers. Modbus Protocol function
  code 04 is used to access all parameters."* All 37 `grid_*` rows declare
  `fc = 3` (`FC_READ_HOLDING`).
- **The site's index order does not follow the device's parameter order**, so no
  arithmetic translation was ever possible. Site index 6 is `grid_L1_VA`, but
  SDM630 parameter 7 (`0x000C`) is Phase 1 *Watts*; VA is parameter 10
  (`0x0012`). This is what settled D1: a translation table would have to be
  hand-transcribed from the same PDF the addresses come from, so it buys a
  second copy of the map and no safety.

---

## Phase 0 - stop the bleeding (no behaviour change) [DONE]

Cheap, mechanical, and it makes every later phase testable.

**0.1 Feature gates** (review 4.1). `wire.h:24` gates on
`RTU_CLIENT || TCP_SERVER` while `tcp/pdu.h:36` gates on
`TCP_SERVER || TCP_CLIENT` and `rtu/slave.h:40` on `RTU_SERVER`, so either newer
flag alone compiles its own header against an empty vocabulary.

- `wire.h` gates on nothing. It is header-only `constexpr` with no cost when
  unused, and it is the shared vocabulary - gating it is what created the bug.
- `rtu/codec.h:20` widens to `RTU_CLIENT || RTU_SERVER` (`rtu/slave.cpp` already
  includes it for `crc16`).
- `libbotanik_features.h`: add the missing `MODBUS_RTU_SERVER` dependency rule
  and a `docs/integration/config.md` row.
- Verify with four single-flag configure+build runs, scripted, not by hand.

**0.2 Config aggregate** (4.2). Replace `#include "libbotanik_config.h"` with
`libbotanik/config/modbus.h` in `rtu/plan.h`, `rtu/slave.h`, `tcp/map.h`,
`tcp/slave.h`, `tcp/master.h`; `wire.h` and `tcp/pdu.h` use no `Config::` value
at all and want `libbotanik_features.h`.

**0.3 Dead code** (5.4). Delete `Planner::sweep_us()`, `merge_gap_used()`,
`Map::row()`, `Map::reg_lo/reg_hi()`, `Request::data_len`, and the `MB_TX`
enumerator with its two switch arms. All grepped, no consumers.

**0.4 Header hygiene** (4.4). Forward-declare `Alarms::AlarmController` in
`tcp/slave.h` and move the include to the `.cpp`; drop `hal/platform.h` from
`tcp/slave.cpp`; drop the `Bank*` parameter from `Map::build()`; add
`<cstring>` to `botanik_sim/fieldbus.h`.

**Test:** existing suite must pass unchanged. This phase is pure subtraction.

---

## Phase 1 - the test scaffold

Written before the fixes it pins, so each later change has something to fail
against. This is the phase that keeps everything after it fixed.

**1.1 `tests/test_site_bindings.cpp`** - new, over the `SiteBank` fixture
`test_site_names.cpp` already builds:

- no two `XP_MODBUS` rows on the same `(device, fc)` have overlapping
  `[reg, reg + regs)` spans;
- every `ROLE_INPUT` row declares a non-zero `timeout_ms` (pins 2.8);
- no planned read block exceeds the per-device register ceiling (pins 1.4);
- every row's `regs` matches the width its `wire` type needs.

**1.2 `tests/test_modbus_rtu_slave.cpp`** - the malformed-input cases, which is
where 2.1 lives: the FC16 truncation frame `01 10 00 00 00 80 00 68 90`, byte
count 255, quantity 0, a stream with one byte deleted, a CRC failure followed by
a good frame, a frame addressed to another unit.

**1.3 `tests/test_modbus_map.cpp`** - `to_wire`/`wire_of`/`from_wire`
round-trips (`map.h:170` already calls these tests the specification of the wire
format), a write covering half a row, an odd start address, a full 123-register
write, and the two-pass rollback claim at `map.cpp:241-255`, asserted nowhere
today.

**1.4 `tests/test_modbus_image.cpp`** - `F_INVERT` folding, the dirty
lifecycle (2.2), the signed-pair path (2.4), a short payload (2.8).

**1.5 `tests/test_modbus_tcp_pdu.cpp`** - protocol id != 0, `mlen` of
0/1/2/254/0xFFFF, a frame split across two segments, two frames in one buffer,
a stale transaction id.

---

## Phase 2 - the field bus works

### 2.1 The site table [DONE]

Per D1, rewrite the meter rows with real protocol addresses and the object index
moved to `notes`:

```c
MI("grid_L1_V",  T_F32, 5, 4,  0, 2, ..., "L1 phase voltage",  "SDM630 p1 30001"),
MI("grid_L2_V",  T_F32, 5, 4,  2, 2, ..., "L2 phase voltage",  "SDM630 p2 30003"),
MI("grid_L1_VA", T_F32, 5, 4, 18, 2, ..., "L1 apparent power", "SDM630 p10 30019"),
```

`fc` moves from 3 to 4 for both meters. All 43 rows were taken from the manuals
(`~/botanik-docs/`), and `tests/test_site_bindings.cpp` is the durable half: it
fails 43 times, by name, against the old table.

Update `vars.h`'s `@warning` to describe what `reg` now holds, and its header
line to `38400 baud` consistency (see 2.2).

### 2.2 Baud rate (review 1.2) [DONE]

`#define BOTANIK_MODBUS_BAUD 38400` in `sites/gh_mlb41/config.h`, guarded
`#ifndef` like every other site override so the command line still wins. Then
re-derive the figures in `modbus-budget.md`, which are computed at 19200, and
re-check `plan.h`'s `static_assert`s.

### 2.3 Echo-matching discard (1.3) [task 1]

Per D2. `RtuFramer::arm()` gains an echo argument; `feed()` swallows bytes while
they match the sent frame and starts framing at the first divergence or once the
echo is exhausted. `_send()` arms it, so listening starts the instant the first
byte could arrive rather than a beat later, and `fieldbus_flush_input()` leaves
the transaction path entirely.

Risk: a slave that answers with a frame whose first bytes coincidentally match
our own request. Bounded by `echo_len` and by the fact that byte 0 is the unit
and byte 1 the function code, which a response repeats anyway - so divergence is
detected at the address at the latest, and the framer's own length and CRC rules
catch the rest. The test is a `RtuFramer` fed (a) request+response, (b) response
alone, (c) request+partial-response.

### 2.4 Per-device read ceiling (new) [task 6]

The SDM630 manual: *"Each request for data must be restricted to 40 parameters
or less. Exceeding the 40 parameter limit will cause a Modbus Protocol exception
code to be returned."* That is 80 registers, against
`MODBUS_MAX_REGS_PER_BLOCK = 120`. With the addresses corrected, the 37 `grid_*`
rows span 74 registers and fit - but only just, and nothing enforces it.

Add a per-device register ceiling the planner honours. Simplest form that does
not reintroduce a device-profile table: a config knob defaulting to 120, plus
`span_cap()` already in the planner - and a site test asserting no planned block
exceeds it.

### 2.5 Writes and reads (3.3, 3.4, 2.6, 2.7) [merge gap and coil span DONE; priority is task 7]

- **Safe-state priority** (3.4): the control-withdrawal edge marks blocks
  `urgent`; `_pick()` bypasses `MODBUS_WRITE_MIN_MS` for those. This is what
  `modbus-budget.md:220-222` already requires.
- **Read floor** (3.3): a read overdue by more than one full period pre-empts
  the write queue, so continuously dirty outputs cannot starve the image.
- **Merge gap** (2.6): `const uint16_t allow = is_read(k.fc) ? _merge_gap : 0;`
  in `plan.cpp:171`. One line, and it stops the client commanding unclaimed
  registers to zero.
- **Coil span** (2.7): cap bit blocks at `MODBUS_MAX_REGS_PER_BLOCK * 16` in
  `span_cap()`, and log at plan time when a row is dropped or clamped - the
  silent `_faults++` is what made this invisible.

### 2.6 Dirty lifecycle (2.2) [task 3]

`collect_write()` records the `Scalar` it sent per row; `clear_dirty()` clears
only rows whose `raw()` still equals it. Storage: the client already owns a
per-block scratch buffer, so the sent values go beside it - no new allocation
and no parallel array to keep in step.

### 2.7 Echo value check (2.3) [task 4]

`Expect` gains the written value; `parse_response` compares it for FC05/FC06.
FC15/FC16 already echo address and quantity, which are checked.

### 2.8 Signed pairs and invalidation (2.4, 2.8) [signed pairs DONE; invalidation is task 5]

- `image.cpp:40, 84`: branch on a signed wire type exactly as line 43 does for
  `T_I16`.
- On `ERR_EXCEPTION`, invalidate the block's rows with `Q_STALE`; on a short
  payload, `Q_NO_DATA` for the rows past the end. A driver that knows a read
  failed should say so rather than leave `Q_GOOD` standing until `expire_stale`
  catches up.
- `plan.cpp:54`: refuse to plan a `ROLE_INPUT` with `timeout_ms == 0` and count
  it in `_rows_dropped`, which is already logged as a critical. Pinned by the
  Phase 1 site test.

### 2.9 Whole-frame send (3.5) [task 2]

`Rs485Raw` already tracks `_tx_done_us`; expose free TX space from it and have
`_send()` refuse the transaction before writing byte 0, honouring the contract
`client.h:45-53` already states. Test: a fake `Raw` that refuses mid-frame.

---

## Phase 3 - the TCP slave

### 3.1 Flash storm (review 3.1) [task 8]

Per D3, both halves:

- `Map::write_registers` applies to the bank live and pushes the row onto a
  bounded persist queue; a new task in `PHASE_SERVICE` drains at most
  `MODBUS_PERSIST_PER_STEP` entries per beat.
- When the queue is full, the write is refused with `EX_SLAVE_BUSY` - which is
  the honest answer and the one a SCADA already knows how to retry.

The queue holds `VarRef`s, so it is bounded by the parameter count and needs no
value copy: the bank already holds what to persist.

### 3.2 FC06 (2.5) [task 9]

Support it. A SCADA writing one setpoint is the common case, and refusing it is
a bigger surprise than the granularity. `_answer_write` merges an FC06 with the
row's other register (read the sibling from the bank, splice, validate the whole
row), so the two-register invariant holds internally while the wire sees a legal
single-register write. If validation fails the row is untouched and the
exception is `EX_ILLEGAL_VALUE`.

### 3.3 Ack block (3.6) [task 10]

Resolve the ack block once at bring-up into a `reg -> condition` array, the same
"resolve names once" rule the rest of the tree follows. This removes the
`strcmp` searches from a remote-driven loop that can legally run 2000 times in
`PHASE_PRESENT`.

### 3.4 Connection lifetime (3.6) [task 10]

- Stamp a deadline when `pending` is set and reap on lack of *send progress*,
  not only on idleness, so a peer that stops reading cannot hold a slot forever.
- `TcpSlave::healthy()` returns false when `tcp_listen()` failed, and the listen
  is retried on a timer rather than abandoned at `slave.cpp:80`.

### 3.5 TCP master (3.6) [task 11]

- A deadline on `MA_SENDING`.
- `disconnect()` on any framing result other than `ERR_OK` / `ERR_SHORT` /
  `ERR_EXCEPTION`, matching what the slave already does at `slave.cpp:155-161`.
- Fold `read_registers()` into a single `read(unit, fc, addr, qty)`.

---

## Phase 4 - the simulated device model

**4.1 FC16 truncation (2.1). [DONE]** Compare the byte count in 32-bit, and add the
`qty < 1 || qty > max_quantity(fc)` ceiling test that `_do_read` has and
`_do_write` lacks. Pinned by the Phase 1 test.

**4.2 Resync (3.6). [DONE]** On a CRC failure discard **one** byte and rescan, rather
than `want` bytes; and have the owner call the existing `reset()` when the wire
has been quiet for t3.5. One lost byte currently misaligns the slave forever.

**4.3 Spec conformance. [task 12]** Execute a broadcast (unit 0) without replying rather
than counting it as dropped; answer `EX_SLAVE_FAILURE` instead of silence when
`cap` is too small.

**4.4 Beat-rate stepping. [task 12]** Gate `Site::simulate()` and `fieldbus.step()` in
`botanik_sim/main.cpp:135-142` on the beat, not the spin - the simulated bus is
currently 50-200x more responsive than a real one, which is its own way of
hiding timing bugs.

---

## Phase 5 - transaction pacing [task 13]

`client.cpp` currently spends ~3 beats per transaction: `MB_GAP` enters
`MB_IDLE` and returns without picking, and `MB_RX` is armed at `PHASE_OUTPUT`
while `receive_step` runs at `PHASE_INPUT`.

- Fall through `MB_GAP -> MB_IDLE -> _pick()` within one beat.
- Let `receive_step` service `MB_DRAIN` as well, so the listening half is
  reached in the phase that runs first.
- Add the fixed scheduling overhead to `duty_ppm()`, or rename it to say it is
  wire time only. The boot log currently understates real bus occupancy by
  roughly 2.4x, which is the number a commissioning engineer will trust.

Deferred to its own phase because it changes timing everywhere and wants the
Phase 1 tests and a working bus (Phase 2) underneath it.

---

## Phase 6 - the readback surface [task 14]

Per D4, finish step 9 of `modbus-remaining.md`:

- `RtuClient` onto `SystemContext` beside `modbus_tcp`.
- A `modbus` shell command over both: the plan (rows, blocks, duty), the state
  and counters, and a line per peer with its miss count and backoff.
- Drop `service_rx()` and register `TcpSlave`'s input half the way `RtuClient`'s
  is registered, so the two components read the same.
- Accessors on `BotanikFirmware` (`modbus_rtu()`, `modbus_tcp()`), which every
  other owned component already has.

---

## Phase 7 - simplification [task 16]

Only now, with behaviour pinned by Phase 1. The 13 items in review §5, roughly
275 lines, in descending value: duplicated rationale (-60), the shared response
validator (-35), the collapsed RTU builders (-27), the `rd16`/`wr16` fork (-18),
`config/modbus.h`'s doc blocks (-40), then the small ones.

Two carry a behavioural risk and want their tests first: the shared validator
(the RTU side additionally rejects `want > PAYLOAD_MAX`, which must survive) and
`collect_write`'s branch merge (the integer paths differ subtly).

`build_write_coil` / `build_write_register` stay - they are tested API - but the
comment at `plan.cpp:32-34` claiming they are used gets corrected.

---

## Phase 8 - documentation [task 17]

**8.1 Code comments.** `client.h:12,19` (`transmit_step()` does not exist),
`client.cpp:173-174` (describes a buffer scheme that was replaced),
`modbus.h:17-20` (describes two corners of four, and includes 8 of 12 headers),
`wire.h:6,31,160` (calls the shared vocabulary RTU-only), and
`sites/gh_mlb41/config.h:135-139` (says nothing instantiates the client).

**8.2 Naming.** State the master/client and slave/server mapping once, in
`modbus.h`, rather than leaving four classes and four flags disagreeing in
silence. Rename `rtu/slave.cpp`'s `val` per case so it does not hold a quantity
under a name that says value. Normalise `0` to `nullptr` in `tcp/pdu.cpp` and
drop the two `using namespace Vars;`.

**8.3 Magic numbers.** Route the five sites that restate frame arithmetic
through `wire.h` (`codec.cpp:29,169,171,179`, `framing.cpp:101,106`,
`rtu/slave.cpp:54,59,121,193,201`), `pdu.cpp:134`'s bare `0x80` through
`EX_BIT`, `slave.cpp:324`'s `2 * 125` through `read_payload()`, and give the
`0xFF00`/`0x0000` coil pair a name in `wire.h`.

**8.4 The drift table.** All 24 rows in review §6.1: `CLAUDE.md` (both flags
called reserved, two flags where there are four, 22 flags where there are 24,
`config/modbus.h` missing from the concern list, 23 param rows where there are
27), `docs/integration/config.md`, `docs/design/README.md`, and the three design
notes written in the future tense about finished work.

**8.5 One dev page.** Fold the three `modbus-*.md` design notes into a
present-tense `docs/dev/modbus.md` and add a `modbus/` row to
`docs/dev/architecture.md`. `modbus-tcp-server.md:376` already asks for exactly
this. Add a row per modbus task to `beat-cost.md`, which today bounds the beat
without mentioning the two highest-variance tasks on it.

---

## What is blocked

**Nothing.** The SDM630 and MCT1-140 manuals were supplied and phase 2.1 is
done; copies live in `~/botanik-docs/` so the next reboot does not lose them
again.

One thing the manuals settled that would otherwise have been a bench trip: the
MCT1-140's "unite et precision" column (0.1V, 0.01A, 10W) reads like a scale
factor and is not one. Its own worked example decodes `43 68 99 9A` as 232.6 V,
which is the IEEE-754 single unscaled, high word first. Both meters are
therefore plain 1:1 float rows with no calibration block. That column IS a
multiplier for the `Long` energy counters lower down the same table.
