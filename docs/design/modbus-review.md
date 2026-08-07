# Modbus subsystem review

A six-agent review of `src/libbotanik/modbus/**` (~4750 lines), its config, its
simulator harness and its tests, across seven dimensions: simplification,
robustness, non-blocking behaviour, coupling, readability, interface quality and
the `vars` integration.

Findings marked **[verified]** were re-checked by hand against the code, not
taken on an agent's word. Findings reported independently by more than one
reviewer are marked with the count, because agreement between reviewers looking
from different angles is the strongest signal here.

The subsystem is, structurally, in good shape: `TARGET_OPTA` appears nowhere
under `modbus/`, RTU and TCP share exactly `wire.h` and nothing else, ownership
is unambiguous throughout, and every timeout is a `HAL::Clock` delta rather than
a loop count. The problems cluster in three places: **the site's register
table**, **the timing of the RTU transaction**, and **everything that was
written before the subsystem got wired up and never revisited**.

---

## 1. Blockers - the field bus cannot work as configured

### 1.1 The meter rows are object indices the master never translates [verified]

> **Corrected.** This finding first read as "the addresses are off by a factor
> of two". That diagnosis was wrong; the one below replaces it. The symptom on
> the wire is the same, the fix is not. See `modbus-remediation.md`.

`src/sites/gh_mlb41/vars.h:332-374`, and the file's own `@warning` at `:29-34`:

> `Binding::reg` holds the FIELD-DEVICE address: the coil number on the relay
> boards, and the **device-profile OBJECT INDEX** on the meters. The raw holding
> register is fixed by the device template, so **the RTU master must translate
> object index -> register per device model.**

The site's numbers are therefore self-consistent object indices (0, 1, 2, ...),
not broken registers. **The translation the warning mandates does not exist**:
`plan.cpp` cuts blocks on `bind.reg` and `image.cpp:37` offsets into the payload
with it, both treating it as a protocol address. Every row after the first in a
block decodes the high word of its predecessor and the low word of itself - all
37 `grid_*` rows and all 6 `ats_*` rows.

Two further mismatches came out of the vendor documentation:

- **The SDM630's measurements are Input Registers, function code 04** ("Modbus
  Protocol function code 04 is used to access all parameters"). All 37 `grid_*`
  rows declare `fc = 3`.
- **The site's index order does not follow the device's parameter order**, so no
  arithmetic translation was ever possible: site index 6 is `grid_L1_VA`, but
  SDM630 parameter 7 (`0x000C`) is Phase 1 *Watts*; VA is parameter 10
  (`0x0012`).

Nothing catches it. `tests/test_site_names.cpp` checks string-pool overlap
(295-307) and TCP-slave register uniqueness (329-370), but never the field-bus
`[bind.reg, bind.reg + bind.regs)` spans.

**Fix:** `reg` holds the real protocol address (the maintainer's call - one
number, checkable against the vendor PDF), the object index moves to the row's
`notes`, `fc` becomes 4 for both meters, and a site test asserts that no two
`XP_MODBUS` rows sharing a (device, fc) have overlapping spans.

### 1.2 The bus runs at the wrong baud rate [verified]

`src/sites/gh_mlb41/vars.h:12` documents the plant as *"RS485, ModbusRTU master,
38400 baud, none/8/1, on COM2"*. No site override for `BOTANIK_MODBUS_BAUD`
exists anywhere in `src/`, so `rtu/client.cpp:18` opens the port at the
`config/modbus.h:119` default of **19200**. Nothing on the segment will answer.

**Fix:** `#define BOTANIK_MODBUS_BAUD 38400` in `sites/gh_mlb41/config.h` - and
note that this also invalidates the timing figures in `modbus-budget.md`, which
are computed at 19200.

### 1.3 The master discards replies that arrive inside the beat [verified] (2 reviewers)

`src/libbotanik/modbus/rtu/client.cpp:231-242`

`MB_DRAIN` is entered in the beat the request is queued, so
`HAL::Serial::fieldbus_tx_idle()` is not tested until the *next* `on_step`,
roughly 10 ms later. `fieldbus_flush_input()` then drops everything the UART has
received - and at 19200 an 8-byte request has cleared the wire in ~4.6 ms, so a
device with a normal 2 ms turnaround is mid-reply or finished when the flush
lands. The result is a permanent timeout, `_missed()`, backoff and
`alarm_fieldbus` on a device that is answering correctly.

The simulator cannot see this: `botanik_sim/fieldbus.h:71` sets
`TURNAROUND_MS = 30` with a comment conceding that an answer arriving before the
flush is thrown away.

**Fix:** arm the framer immediately after `_send()` and consume the known echo
length, or flush in the same call that finishes the write. Do not blanket-flush
a beat later.

---

## 2. Correctness

### 2.1 FC16 byte-count check truncates to 8 bits [verified]

`src/libbotanik/modbus/rtu/slave.cpp:179` - `bc != (uint8_t)(val * 2)`.

Quantity 128 gives `256 & 0xFF == 0`, which matches a declared byte count of 0.
`MODBUS_SIM_REGS` is 256, so the range check passes as well, and the commit loop
reads `pdu[6 .. 261]` out of `_req[256]` and writes 128 registers from stack
garbage. Trigger: `01 10 00 00 00 80 00 68 90`.

Scope: `RtuSlave` is instantiated only by `botanik_sim/fieldbus.h:73`, so this is
the *simulated* device model, not a field-exposed surface on the OPTA. It is
still an out-of-bounds read that ASan would flag the moment a test fed it.

**Fix:** compare in 32-bit, and add the `qty > max_quantity(fc)` ceiling test
that `_do_read` has and `_do_write` lacks. The same function accepts quantity 0,
which the spec requires be refused with `EX_ILLEGAL_VALUE`.

### 2.2 An output demand that moves mid-transaction is lost for 5 seconds

`rtu/image.cpp:192`, `rtu/client.cpp:331`

`collect_write()` runs in `MB_IDLE`; `clear_dirty()` runs several beats later in
`_settle()` and clears `VF_FIELD_DIRTY` on every row of the block
unconditionally. A control program that moves a demand between those two points
sets the dirty bit, which `clear_dirty` then wipes - so the new value waits for
`MODBUS_REWRITE_MS` (5 s). On a valve coil that is five seconds of a stale
command.

**Fix:** capture the collected values at build time and clear only rows whose
`raw()` still matches what was sent.

### 2.3 A write echo is accepted without checking the value

`rtu/codec.cpp:180-189`

For FC05/FC06 only the address is compared. `client.cpp:331` then calls
`clear_dirty` on any non-exception response, so a relay board that clamps,
inverts or ignores a coil echoes a different value and the firmware records the
actuator as moved.

**Fix:** carry the written value in `Expect` and compare it.

### 2.4 Signed 32-bit wire values lose their sign

`rtu/image.cpp:40, 84` - the register-pair path casts through `u32_from_regs`
with no signed branch, so a `T_I32` wire pair reads -1 as 4294967295. The
single-register path gets this right (`image.cpp:43` special-cases `T_I16`).

### 2.5 FC06 can never succeed (2 reviewers)

`tcp/map.cpp:239` refuses any odd count with `EX_ILLEGAL_ADDRESS`, and
`tcp/slave.cpp:203` always calls `write_registers(addr, 1, ...)` for
`FC_WRITE_REGISTER`. Every write-single-register from a SCADA is refused - the
single most common way a master writes one setpoint - and refused with the wrong
exception code, since the address is fine and the granularity is the objection.
`map.h:26` documents FC06 as supported.

### 2.6 The read merge gap is applied to write blocks

`rtu/plan.cpp:171-172` does not test `is_read(k.fc)`, so up to 17 registers of
unclaimed gap can be absorbed into an FC16 block - and `collect_write` zero-fills
the whole span (`image.cpp:146`), so the client *commands those registers to
zero* on every rewrite. Reading a register nobody wants is free; writing one is
not. Latent on this site (its coils are contiguous), but it is a plant-damage
shape.

**Fix:** `const uint16_t allow = is_read(k.fc) ? _merge_gap : 0;`

### 2.7 A coil block wider than 1920 fails silently forever (3 reviewers)

`rtu/plan.cpp:87-92` lets a bit block span the protocol's 1968 coils, but
`_build`'s scratch buffer is 240 bytes = 1920 coils (`client.cpp:175`). Beyond
that, `collect_write` returns 0, `_build` returns false, the block is retired
every turn with `_faults++` and **no log line**, and those outputs are never
driven.

### 2.8 Nothing is invalidated when a read fails

`client.cpp:312-323` returns after counting `_refusals` on `ERR_EXCEPTION`;
`commit_read` skips rows outside a short payload and its `good` count is
discarded. Rows keep `Q_GOOD` until `expire_stale()` catches up - and a row
declared with `timeout_ms == 0` is skipped by `expire_stale` entirely
(`model.cpp:256`), so it holds its last value forever while `plan.cpp:54`
happily plans it.

---

## 3. Real-time behaviour

### 3.1 One FC16 can do 61 synchronous flash writes inside PHASE_INPUT (2 reviewers)

`tcp/map.cpp:257-270`

Each changed row commits through `Bank::set` -> `KvParamStore::save` ->
`HAL::Storage::set` -> Mbed TDBStore on the external QSPI NOR. `decode()` caps
FC16 at 123 registers and writes must cover whole rows, so **61 rows commit in
one call**, unpaced. A TDBStore write is a record append plus a page program
(~1-5 ms), and a garbage-collection pass adds a 4 KB sector erase (45 ms typical,
400 ms worst). A SCADA "download all setpoints" is therefore 60-300 ms typical
and seconds if GC lands mid-block - 6 to 30+ beats, repeatable every beat, and
doubled with two connections. The RTU client's 300 ms response deadline expires
inside it and the whole field bus goes into backoff.

The no-op guard at `map.cpp:263` prevents a *polling* master from doing this, not
a genuine bulk write.

**Fix:** apply live, mark dirty, and drain at most N persists per beat - the
`SERIAL_TX_PER_STEP` idiom `utils/serial.cpp:113` already uses.

### 3.2 The transaction FSM burns ~3 beats per transaction

`client.cpp:205-258`. `MB_GAP` enters `MB_IDLE` and returns without picking;
`MB_DRAIN` cannot be detected in the beat the frame was queued; and `MB_RX` is
armed at `PHASE_OUTPUT` while `receive_step` runs at `PHASE_INPUT`. A 2-register
read is 21.7 ms of wire but ~50 ms of wall clock. A 16-transaction sweep is
~800 ms against the 555 ms the budget document claims.

Consequently `plan.cpp:210-221`'s `duty_ppm()` - which the boot log reports as
"N% of the bus" - understates real occupancy by roughly 2.4x, because it models
wire time only.

### 3.3 Writes pre-empt reads with no floor

`client.cpp:136-157` returns the first eligible write before looking at reads at
all. With three or four continuously dirty output blocks (an `Actuator` timing a
stroke), writes can take the entire transaction budget and 1 s read rungs starve
until `expire_stale()` invalidates them.

### 3.4 The safe-state write is throttled like ordinary traffic

`disable_control()` marks every `ROLE_OUTPUT` dirty precisely so a transaction
carries the safe state out, but `_pick()` still enforces `MODBUS_WRITE_MIN_MS`,
so an actuator can stay energised ~250-300 ms after control is withdrawn.
`modbus-budget.md:220-222` explicitly requires the opposite.

### 3.5 A partial frame can reach a shared bus (2 reviewers)

`client.cpp:199-202` writes byte-by-byte and returns false on the first refusal,
leaving everything already accepted to go out as a CRC runt. `client.h:45-53`
promises "a frame goes out whole or not at all". Margin is thin but real: the
OPTA's mbed TX buffer is 256 bytes against a 249-byte maximum FC16.

### 3.6 Smaller

- `tcp/slave.cpp:253-267` - an ack-block read costs `count` x two `strcmp`
  searches, and `count` may legally be 2000: ~2 ms in `PHASE_PRESENT`, per
  connection, per beat, unbudgeted. Resolve the ack block once at bring-up.
- `tcp/slave.cpp:396` - the idle reaper skips any connection with `pending`, and
  `_flush()` never updates `last_ms` on a would-block, so two peers that stop
  reading hold both slots forever.
- `tcp/master.cpp:127-135` - `MA_SENDING` has no deadline at all.
- `tcp/master.cpp:146-168` - a framing error zeroes the buffer but keeps the
  socket, leaving a desynchronised stream. The slave gets this right
  (`slave.cpp:155-161`); the master should too.
- `rtu/slave.cpp:82` - a CRC failure discards exactly `want` bytes with no
  byte-wise resync, so one lost byte on the wire misaligns the slave
  permanently. `reset()` exists for this and has no caller.
- `client.cpp:266-275` and `_send()` - correctly bounded, but one mutex round
  trip per byte (~0.5-1.3 ms worst case). A bulk `read(buf,n)`/`write(buf,n)` on
  `Serial::Raw` would remove it.
- `botanik_sim/main.cpp:135-142` - `Site::simulate()` and `fieldbus.step()` run
  per loop *iteration*, not per beat, so the simulated bus is 50-200x more
  responsive than a real one.

---

## 4. Structure and coupling

Sound: no target branching under `modbus/`, the `library_vars.h` edges are the
sanctioned splice point, `Planner` takes the bank per call rather than holding
it, and the phase assignments are right for a field bus.

### 4.1 Two of the four feature flags cannot build alone [verified] (2 reviewers)

`wire.h:24` gates on `RTU_CLIENT || TCP_SERVER`, but `tcp/pdu.h:36` gates on
`TCP_SERVER || TCP_CLIENT` and `rtu/slave.h:40` on `RTU_SERVER`. Either newer
flag alone compiles its own header with the shared vocabulary switched off.
Same shape at `rtu/codec.h:20` (gated `RTU_CLIENT`, but `rtu/slave.cpp` includes
it for `crc16`). It builds today only because `gh_mlb41` turns all four on.
`libbotanik_features.h` has no dependency rule for `MODBUS_RTU_SERVER` at all.

**Fix:** gate `wire.h` on all four, or on nothing - it is header-only
`constexpr`. Add the missing `#error` rules and a flag-combination build.

### 4.2 `modbus/` is the last subsystem including the config aggregate

`wire.h:22`, `rtu/plan.h:50`, `rtu/slave.h:36`, `tcp/pdu.h:32`, `tcp/map.h:53`,
`tcp/slave.h:59`, `tcp/master.h:41` all include `libbotanik_config.h`. Those
seven plus the two sim headers are the only remaining consumers in the tree, so
editing the MQTT keep-alive recompiles the Modbus codec. `tcp/params.h:17` is the
one file that gets it right.

### 4.3 Neither component's readback surface has a reader [verified]

`TcpSlave::service_rx()` (`slave.h:156`) has zero callers - `firmware.h:539` uses
`add_component(...)`, which calls `Component::step()`, while the sibling
`RtuClient` *does* route through `receive_step()`. `SystemContext::modbus_tcp` is
assigned at `firmware.h:353` and read nowhere. `RtuClient`'s entire status API
(`status_str`, `peer_dead`, `dead_count`, `tx_short`, `refusals`, `txns`,
`faults`, `plan`, `framer`) has no caller and `RtuClient` is not on
`SystemContext` at all. There is no `modbus` shell command and no page.

This is step 9 of `modbus-remaining.md`. Until it lands, a large public
interface has nothing pinning it - and the two shapes have already diverged.

### 4.4 Smaller

- `tcp/slave.h:61` includes `alarms/alarms.h` for a pointer member; forward
  declare and move the include to the `.cpp`.
- `tcp/map.h:100,108` - the bank is injected twice, by constructor and by
  `build()`. Drop the parameter from `build()`.
- `RtuClient` hard-wires `HAL::Serial::FIELDBUS` (`client.cpp:18-19, 236, 241`),
  so a second RS-485 bus is not expressible and the client cannot be driven
  against a fake port - which is why there is no client test.
- `TcpSlave` does not override `healthy()`, so a failed `tcp_listen()`
  (`slave.cpp:80`, not retried) reports healthy forever.

---

## 5. Simplification - roughly 275 lines at medium-or-better confidence

The subsystem is 48% prose and blank lines, so the largest single lever is
duplicated *rationale*, not duplicated code.

| # | What | Lines | Confidence |
|---|---|---|---|
| 1 | Rationale written 2-3 times (the libmodbus argument in `modbus.h:49`, `tcp/master.h:13` and `modbus-remaining.md:32`; the dead-slave budget in `client.h:26`, `config/modbus.h:222` and the doc; `fieldbus.h:62` and `:78` ten lines apart) | -60 | high |
| 2 | One shared response validator - `rtu/codec.cpp:151-189` and `tcp/pdu.cpp:212-246` are the same check in two spellings, provably equivalent once expressed in body bytes | -35 | med-high |
| 3 | Collapse the five RTU request builders (`codec.cpp:46-132`); three write identical frames apart from four payload bytes | -27 | high |
| 4 | Dead code, all grepped: `Planner::sweep_us()`, `merge_gap_used()`, `Map::row()`, `Map::reg_lo/reg_hi()`, `Request::data_len` (written twice, never read), and the `MB_TX` state, which is **never entered** [verified] | -28 | high |
| 5 | One big-endian vocabulary - `tcp/pdu.h:163` `rd16`/`wr16` vs `wire.h:226` `hi8`/`lo8`/`be16`; the hand-packing idiom appears 11 times | -18 | high |
| 6 | `config/modbus.h` is 272 lines for 25 constants, every one of which restates a paragraph the budget doc owns (no knob is unreferenced - all 25 have consumers) | -40 | medium |
| 7 | `Map::_find()` in terms of `_lower_bound()` (`map.cpp:110-130`, two hand-written binary searches) | -9 | high |
| 8 | Promote `seal()` out of `codec.cpp` - the append-CRC idiom is written four times | -10 | high |
| 9 | `encode_read_request` in terms of `encode_echo` (`pdu.cpp:150-177`, identical bodies) | -8 | medium |
| 10 | `collect_write`'s two near-identical branches (`image.cpp:163-187`) | -8 | medium |
| 11 | Drop the `uint16_t regs[]` round-trip in `_build()` (`client.cpp:185`) - and with it a 240-byte stack array | -6 | medium |
| 12 | `_peer_of` const/non-const duplication, `_resolve()` restating `wire.h` arithmetic, `read_registers()` forwarding to `read_bits()` | -14 | high |
| 13 | Test duplication: `test_modbus_framing.cpp:28-32` re-declares three fixtures verbatim from `test_modbus_codec.cpp` and re-implements `seal()` | -12 | medium |

`build_write_coil` and `build_write_register` have no production caller
[verified] - `plan.cpp` coalesces every write under FC15/FC16. Worth keeping as
tested API, but the comment at `plan.cpp:32-34` claiming they are used is wrong.

---

## 6. Readability and documentation

No em-dash violations anywhere in the subsystem - the ` - ` convention holds
throughout. The per-file `@file` blocks are genuinely good, explaining why rather
than what.

The problem is staleness on a scale worth calling out separately: the subsystem
grew from two corners to four and from "not wired" to "wired", and none of the
prose moved.

### 6.1 Documentation that contradicts the code [verified in part]

- `CLAUDE.md:201` calls `MODBUS_TCP_SERVER` and `MODBUS_RTU_CLIENT`
  "**reserved** - no implementation exists yet". Both are constructed
  (`firmware.h:244, 252`) and stepped by four registered tasks. It also lists two
  field-bus flags where there are four, and "22 `BOTANIK_FEATURE_*` flags" where
  `BOTANIK_FEATURE_LIST` has 24.
- `CLAUDE.md` omits `config/modbus.h` from the config-concern list, and says
  `_PARAM_VARS` carries 23 rows where it carries 27 with the TCP server on.
- `docs/integration/config.md:66,68` - "Not instantiated yet - no task steps it",
  twice. `MODBUS_RTU_SERVER` has no row at all, which the header's own
  "adding a flag" rule requires.
- `docs/design/README.md:16-17`, `modbus-remaining.md:4-5` and all of
  `modbus-tcp-server.md`'s "Have / Need" table describe finished work in the
  future tense. `modbus-remaining.md` §1 and §2 are done.
- `sites/gh_mlb41/config.h:135-139` [verified] - "Only the protocol layer exists
  so far ... Nothing instantiates it and no task steps it, so turning this on
  today adds no work to the beat." It adds two tasks, a serial port and a bus
  plan.
- `docs/design/beat-cost.md:17-34`, the per-beat budget table, has **no row for
  any of the four modbus tasks** - the two highest-variance tasks on the beat are
  missing from the document that bounds it.

### 6.2 Comments that describe code that is not there [verified]

- `client.h:12, 19` document `transmit_step()`. No such function; the transmit
  half is `on_step()`.
- `client.cpp:173-174` - "Collect into the tail of the frame buffer, then build
  over it." The next line declares a local array; nothing collects into `_frame`.
- `modbus.h:17-20` - "`rtu/` = the MASTER, `tcp/` = the SLAVE". Both are now
  false, and the aggregate header includes 8 of the 12 headers, so
  `#include "modbus/modbus.h"` does not give you Modbus.
- `wire.h:6, 31, 160` - the shared vocabulary calls itself "the Modbus **RTU**
  vocabulary" and refers to its users as "this client". Four objects use it, two
  of them servers.

### 6.3 Naming

The four corners name themselves inconsistently and nothing states the mapping:

| class | flag | what it is |
|---|---|---|
| `RtuClient` | `MODBUS_RTU_CLIENT` | bus **master** |
| `RtuSlave` | `MODBUS_RTU_**SERVER**` | bus slave |
| `TcpSlave` | `MODBUS_TCP_SERVER` | server |
| `TcpMaster` | `MODBUS_TCP_**CLIENT**` | master |

Also: `rtu/slave.cpp:152`'s `val` holds a value for FC05/06 and a quantity for
FC15/16; `tcp/pdu.cpp` uses `0` for null pointers where every other file in the
subsystem uses `nullptr`; `map.cpp:19` and `slave.cpp:21` use
`using namespace Vars;` where `image.cpp` and `plan.cpp` qualify; and
`plan.h:308`'s `static_assert` message says "36.9 ms" for 36735 us.

### 6.4 Magic numbers where `wire.h` has the named constant

`wire.h:141` promises the frame arithmetic is written once "so a disagreement
between the three is not expressible". It is expressible in five places:
`codec.cpp:29, 169, 171, 179`, `framing.cpp:101, 106`, and
`rtu/slave.cpp:54, 59, 121, 193, 201`. Plus `pdu.cpp:134`'s bare `0x80` where
`EX_BIT` exists, `slave.cpp:324`'s `uint8_t payload[2 * 125]` under a comment
saying "wire.h owns both ceilings", and bare `0xFF00`/`0x0000` coil values in
two files.

---

## 7. Test coverage

This is the finding three reviewers arrived at independently, and it is why most
of section 2 could sit here undetected.

`tests/` builds `test_modbus_codec.cpp` and `test_modbus_framing.cpp` only. Both
include `modbus/modbus.h` and **never touch `Vars`**. There is no test for
`tcp/map.cpp`, `tcp/slave.cpp`, `tcp/pdu.cpp`, `tcp/master.cpp`, `rtu/image.cpp`,
`rtu/plan.cpp`, `rtu/client.cpp` or `rtu/slave.cpp`.

`map.h:170` documents `to_wire`/`wire_of`/`from_wire` as "exposed because the
tests are the specification of the wire format". Those tests do not exist.

Findings 1.1, 2.1-2.8 and 3.1 would each be caught by a test that does exist
elsewhere in shape - `test_site_names.cpp` already builds a `SiteBank` fixture
that a `test_modbus_map.cpp` and `test_modbus_image.cpp` could reuse directly.

Highest-value missing cases, in order:

1. **Site table** - no two `XP_MODBUS` rows overlap on a (device, fc). Catches 1.1.
2. **`RtuSlave::poll`** - the FC16 truncation frame, byte count 255, a stream
   with one byte deleted, quantity 0, a CRC failure followed by a good frame.
3. **`Map`** - a write covering half a row, an odd start address, a
   123-register write, and the two-pass rollback claim at `map.cpp:241-255`,
   which is asserted nowhere.
4. **`decode()`** - protocol id != 0, `mlen` of 0/1/2/254/0xFFFF, a frame split
   across two segments, two frames in one buffer.
5. **`rtu/image.cpp`** - `F_INVERT` folding, the dirty lifecycle (2.2), the
   signed-pair path (2.4).
6. **`_send()`** against a `Raw` that refuses mid-frame - currently zero coverage,
   because the sim's socketpair can never short-write.

---

## Suggested order of work

1. **1.1 and its test** - the field bus reads garbage until this is fixed, and
   the table is generated, so the test is the durable half.
2. **1.2, 1.3** - one is a define, the other is the transaction timing. Together
   with 1.1 they are the difference between a bus that works and one that does
   not.
3. **3.1** - the flash storm is the one finding that can trip a health fault from
   a remote master.
4. **2.2, 2.3, 2.5, 2.6, 2.7** - correctness, cheap individually.
5. **4.1** - the feature gates, before anyone tries a cut-down build.
6. **Section 7** - the test scaffold, which is what keeps 1-4 fixed.
7. **Section 5** - the simplification pass, once the behaviour is pinned by tests.
8. **Section 6** - the documentation sweep, ideally folding the three design
   notes into one present-tense `docs/dev/modbus.md`, which
   `modbus-tcp-server.md:376` already asks for.
