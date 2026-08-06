# The Modbus TCP server (slave)

What a SCADA master sees when it polls the PLC, and how that view is derived
from things the firmware already declares. The other Modbus note
(`modbus-budget.md`) is about the RTU **client** reading the field; this one is
the **server** publishing our own image outward. They share nothing but the PDU
codec.

**Scope.** Read every `SCOPE_P` (parameter), `SCOPE_SV` (status value) and
`SCOPE_A` (alarm) row; write parameters. Nothing else: no outputs, no overrides,
no control authority. Driving the plant by hand stays a console job, where there
is an operator and a log line naming them, and an alarm is readable but not
writable - forcing one is an override, which is the same job.

## What already exists

The map is very nearly not a new declaration. Every parameter and status row
already carries a hand-assigned register in `Binding::reg` (`P()`, `PS()` and
`SV()` in `vars/declare.h`), assigned per row precisely so inserting a variable
never renumbers the map an external master holds.
`tests/test_site_names.cpp` already fails the gate on a row with `reg == 0` or on
two rows claiming the same number, and the PARAMS detail panel already shows a
row's register.

Alarms are the exception: `A()` takes no register, so the 23 site alarms and the
6 library ones have none. Publishing them means **giving `A()` a `reg` argument**
and widening that site test to cover `SCOPE_A` in the same register space. It is
a mechanical edit to 29 rows and the two factories, and it is the right one: the
alternative - a coil window addressed by the alarm's position in the table - puts
the map back at the mercy of a row insertion, which is exactly what
hand-assigning registers buys.

So the server has one input - the bank - and adds no table of its own.

| Have | Need |
|---|---|
| `reg` per parameter / status row | the same on `A()`, and an address -> row index built at bring-up |
| uniqueness enforced by the site test | the same check widened to alarms, and a span rule, since a value is wider than a register |
| `Bank::set()`: validate, apply, persist | a `Scalar` overload of it (today it takes display text), and the exception code its refusals map to |
| `Var::valid()` quality | somewhere to put quality, which Modbus has no room for |
| `HAL::Network` TCP **client** sockets | `tcp_listen()` / `tcp_accept()`, both backends |

## The register map

One register is 16 bits; most rows are wider. The declared numbers are dense and
consecutive (`cli_*` runs 100..136, all `T_F32`), so a row cannot simply span
`reg` and `reg+1` - it would land on its neighbour.

**The declared `reg` is a row number, not an address, and each of the four Modbus
tables derives its own address from it.**

| Table | FC | Address | Holds |
|---|---|---|---|
| Holding registers | 03, 06, 16 | `2 * reg` | the value, 2 registers, high word first |
| Input registers | 04 | `2 * reg` | the same, read-only alias |
| Discrete inputs | 02 | `reg` | the row's `valid()` |
| Coils | 01, 05, 15 | `reg` | `T_BOOL` rows, writable only when the row is a parameter (an alarm coil is read-only) |

`2 * reg` is injective, so the site test's uniqueness guarantee carries over
untouched, and 999 rows (the library reserves 900..999) reach address 1998 in a
65536-register space. Encoding by type:

| VarType | 2 holding registers hold |
|---|---|
| `T_F32` | IEEE-754 single, big-endian, high word first |
| `T_U8/U16/U32`, `T_ENUM` | zero-extended `uint32` |
| `T_I8/I16/I32` | sign-extended `int32` |
| `T_BOOL` | 0 or 1 (and a coil at `reg`); an alarm is one of these |
| `T_U64/I64` | saturated to 32 bits, and the row is listed by the `modbus` command as truncated |
| `T_STR` | not mapped; reads as zeros, writes are refused |

Rejected alternatives, and why:

- **One register per row, scaled by `digits`.** Matches the declared numbering
  exactly and needs no address arithmetic, but every row then needs a scale
  factor an external master has to be told about out of band, and rows that do
  not fit (`ntp_poll_sec`, 86400) saturate silently. A firmware that publishes
  engineering units everywhere else should not publish counts here.
- **A native span, 1 register for a `T_U16`, 2 for an `T_F32`.** Packs tighter,
  but the address of every row then depends on the widths of the rows before it,
  which is the renumbering that hand-assigning `reg` was meant to prevent.
- **A separate compact window beside the wide one.** Two ways to read the same
  row, so two ways for them to disagree. Add it if a master turns up that
  genuinely cannot do 32-bit reads.

**Sparse reads succeed.** A master reads blocks, and the map has gaps: the
registers of every number nobody assigned. A read overlapping a gap
returns zeros for the gap rather than an exception; only a request entirely
outside the mapped span is `02 ILLEGAL DATA ADDRESS`. A read that starts or ends
mid-row returns that row's corresponding half, so a master polling a block never
has to know where the rows begin.

**Quality is a discrete input, not a magic value.** Modbus has no quality bit, so
an invalid reading would otherwise be indistinguishable from a real zero. A row
whose `valid()` is false publishes its declared substitute (`def`, which is what
every other consumer sees) in the register, and reads 0 at discrete input `reg`.
A master that cares polls one FC02 block and gets the quality of every row in it.

## Where it sits in the beat

Two tasks, one component, split across phases the way the field bus client is
(`scheduler/tasks.h`):

\dot "Request in on one phase, answer out on another"
digraph modbus_beat {
  rankdir=LR; node [shape=box, fontsize=10]; edge [fontsize=9];
  rx    [label="modbus_rx\nPHASE_INPUT"];
  cfg   [label="params\nPHASE_CONFIG"];
  scan  [label="scan\nPHASE_EXECUTE"];
  tx    [label="modbus_tx\nPHASE_PRESENT"];
  bank  [label="Vars::Bank", shape=box3d];
  rx -> bank [label="writes (Bank::set)"];
  bank -> cfg [label="group serial"];
  bank -> scan;
  bank -> tx [label="reads, post-scan"];
  rx -> tx [label="pending request", style=dashed];
}
\enddot

- **`modbus_rx`, GROUP_FAST, PHASE_INPUT.** Accept at most one new connection,
  drain each socket, decode at most one complete ADU per connection, and apply
  writes **now** - so a setpoint written this beat is what the programs run on
  this beat, and `PHASE_CONFIG` picks up the group serial in the same cycle.
- **`modbus_tx`, GROUP_FAST, PHASE_PRESENT.** Encode the staged responses off the
  post-scan image and send them.

Cost of the split: one beat, 10 ms, of added response latency, against a SCADA
poll measured in seconds. Gain: a read reflects the beat that just ran rather
than the one before it, and no request is ever answered from a half-updated
image, because nothing writes the bank between `PHASE_OUTPUT` and the encode.

**Bounded per beat**, like every other task: at most `MODBUS_ACCEPT_PER_STEP`
accepts, one request decoded per connection, `MODBUS_READ_REGS_MAX` (125,
protocol) registers per read, and `MODBUS_WRITES_PER_STEP` **rows** committed per
beat. That last one is the only real cost: a commit is a flash write.

## Writes

A write reaches `Bank::set()` and nothing else, so it inherits the band check,
the persistence and the refusal codes the console and the PARAMS page already
get.

One gap there: `Bank::set()` takes display **text**, because both its callers so
far are an operator typing. A Modbus write arrives already typed, and rendering
four register bytes to a string only to parse them back would put the value
through `digits` rounding on the way. So `set()` wants a `Scalar` overload, with
the text form parsing and delegating to it - the same split `override_set()`
already has.

The mapping:

| Situation | Response |
|---|---|
| written value equals the current value | OK, no commit at all (see below) |
| `SET_OK` | OK |
| `SET_INVALID` (outside the declared band) | `03 ILLEGAL DATA VALUE` |
| `SET_NOT_PERSISTED` | OK, counted and logged; a refused persist is not a rejected edit |
| row is not a parameter (a status value, an alarm) | `02 ILLEGAL DATA ADDRESS` |
| address in no row | `02 ILLEGAL DATA ADDRESS` |
| `T_STR` row | `02 ILLEGAL DATA ADDRESS` |
| unsupported FC | `01 ILLEGAL FUNCTION` |
| more rows than the per-beat write budget | held, answered when the last one commits |
| writes disabled by config | `02` on every write address |

**A no-op write must cost nothing.** A SCADA that re-asserts its whole setpoint
block every poll is normal, and at one flash write per register per poll it would
wear the QSPI out and blow the beat besides. Compare first, commit only on a
change. This is the single most important rule here, and writes being **enabled
by default** is what makes it load-bearing rather than a nicety: the first master
that turns up gets the write path whether or not anyone configured it.

`mb_write_enable` still exists, defaulting on, so a site or a shift can make the
slave read-only without a rebuild. A commit is logged with the peer address, the
row and the old and new values - a setpoint that moved has to be attributable to
somebody, and over Modbus the address is all the identity there is.

## The HAL gap

`hal/net.h` has client sockets only. Two calls to add, in the same style
(non-blocking, poll model, id < 0 invalid):

```c++
TcpSocket tcp_listen(uint16_t port);          // bound + listening, or < 0
TcpSocket tcp_accept(TcpSocket listener);     // < 0 when nothing is pending
```

Mbed `TCPSocket::open/bind/listen/accept` on OPTA, BSD `listen`/`accept4` on the
simulator, both non-blocking. The simulator backend gets this for free and stays
hermetic: the listener binds `mock_loopback_ip()`, the per-process 127.x.y.z the
sim already uses, so two simulators never collide on port 502 and a test client
in the same process connects for real. That is what makes the server testable
without hardware.

## Shape of the code

```
src/libbotanik/modbus/
  pdu.h/.cpp      PDU + MBAP decode/encode, exception codes. Knows nothing about
                  vars or sockets; shared with the future RTU client.
  map.h/.cpp      Modbus::Map over Vars::Bank: reg -> VarRef, built once at
                  bring-up (scan for SCOPE_P/SCOPE_SV, sort by reg, binary
                  search - the same shape as the bank's name index), plus the
                  type encode/decode above.
  server.h/.cpp   Modbus::TcpServer: Component, listener + N connections, the two
                  step entry points, the counters.
```

Behind `BOTANIK_FEATURE_MODBUS_TCP_SERVER`, which is already declared with its
dependency rules (`NETWORK`, `PLC_SCAN`).

**Config** (`libbotanik_config.h`, its own group): `MODBUS_TCP_PORT` 502,
`MODBUS_UNIT_ID` 1, `MODBUS_MAX_CONN` 2, `MODBUS_ADU_MAX` 260 (the protocol
ceiling: 7 MBAP + 253 PDU), `MODBUS_IDLE_TIMEOUT_MS` 60000,
`MODBUS_ACCEPT_PER_STEP` 1, `MODBUS_WRITES_PER_STEP` 1.

Two connections at 520 bytes of buffer each is about 1 KB of RAM, plus the map's
one `uint16_t` per mapped row.

**Runtime config** is a parameter group like every other component's, so the
server is a `Component` with `param_serial()` over `group_serial(io, "mb")`:
`mb_serial`, `mb_enable`, `mb_port`, `mb_write_enable`. Changing the port
restarts the listener and drops the connections.

**Readback.** A `ModbusContext` on `SystemContext` (the `SchedulerContext`
pattern): state, port, live connection count, requests served, exceptions
raised, last peer, last request age. A `modbus` shell command prints it, plus
the map's size and any truncated rows. Optionally two library rows in
`BOTANIK_LIBRARY_VARS` (`mb_clients`, `mb_exceptions`) so the count is
publishable like anything else - which is also the first real use of the
`_STATUS_VARS` group.

## Robustness

The failure modes that matter for a server on a plant network, and the answer to
each:

- **A master that opens connections and never closes them.** Fixed connection
  slots; when they are full, accept and immediately close the newest, counting
  it. `MODBUS_IDLE_TIMEOUT_MS` reaps a socket with no complete request.
- **A dribbling or truncated frame.** The MBAP length field is authoritative;
  buffer until it is satisfied, and drop the connection on a length outside
  1..253 rather than trying to resynchronise (TCP is a stream, not a bus - a
  frame error means the peer is broken).
- **Pipelined requests.** One in flight per connection is enough for every SCADA
  in the field; extra bytes stay buffered and are decoded next beat.
- **Unit id.** Accept any and echo it back, counting mismatches against
  `MODBUS_UNIT_ID`. A TCP slave that refuses unit 0 or 255 fails against half the
  diagnostic tools out there.

## Tests

- `test_modbus_pdu.cpp` - framing, lengths, every exception path. No bank, no
  sockets.
- `test_modbus_map.cpp` - a fixture table: address arithmetic, one round trip per
  `VarType`, sparse and mid-row reads, write validation and refusal codes.
- `test_modbus_server.cpp` - the state machine against a real loopback socket in
  the simulator HAL: connect, request, response bytes, connection limits, idle
  reap. Requests hand-built from bytes; a 12-byte MBAP frame needs no library.
- The phase split deserves one test of its own: a write and a read in the same
  beat must show the written value.

## Staging

Each step builds and leaves the gate green:

1. `A()` gains its register, the library and site alarm rows get numbers, and the
   site test's uniqueness check widens to `SCOPE_A`. Pure table work, and it
   lands before anything reads the map.
2. `tcp_listen` / `tcp_accept` in the HAL, both backends, with a test that binds
   and accepts on the simulator.
3. `modbus/pdu` + its test. Nothing depends on the bank yet.
4. `modbus/map` over the bank + its test. Still no sockets.
5. `modbus/server`, the two tasks, the feature gate, the config group.
6. Wiring: the stack, the site config, the `modbus` command, the context,
   `docs/dev/` page. Fold whatever of this note turned out to be true into it and
   delete the rest.

## Settled, and what is left

Decided: the `2 * reg` layout (the site's existing numbers stand, nothing is
renumbered); writes enabled by default, gated by `mb_write_enable` and logged
with the peer; alarms in the map as read-only coils, which costs `A()` a register
argument.

Left open:

- Where the alarms' registers come from. A block of their own (say 800..899,
  below the library's reserved 900..999) keeps them legible to an integrator, and
  the library's six want the top of the library range.
- Whether an ACK belongs on the wire later. It is the one alarm interaction a
  SCADA operator actually performs, and it is a *write* to something the scope
  above says is read-only - so it would want its own coil block rather than the
  alarm's own bit.
- What the register map document for the integrator is generated from. The bank
  has everything; a `modbus map` shell command or a build-time CSV are both
  cheap, and one of them will be asked for the first time this is commissioned.
