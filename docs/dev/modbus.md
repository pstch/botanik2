# Modbus {#dev_modbus}

The field bus and the plant network. How a variable becomes a request, what
decides which request goes next, and why the two transports share more code than
they look like they should.

The per-class reference lives in the headers - `modbus/modbus.h` is the map. This
page is the shape and the decisions behind it.

## Four corners

Each transport has a master and a slave, and this firmware ships all four.

| class       | flag                | what it is                    |
|-------------|---------------------|-------------------------------|
| `RtuClient` | `MODBUS_RTU_CLIENT` | bus MASTER, polling the field |
| `RtuSlave`  | `MODBUS_RTU_SERVER` | a device model, answering     |
| `TcpSlave`  | `MODBUS_TCP_SERVER` | the SCADA-facing server       |
| `TcpMaster` | `MODBUS_TCP_CLIENT` | this unit polling another     |

The class names use the wire documentation's older master/slave spelling and the
flags the newer client/server one. **Read the flag as the authority.** The
mismatch is deliberate: the manuals on the bench say master, the specification
says client, and picking one would make the other unsearchable.

`MODBUS_RTU_SERVER` depends on nothing - it is a device model over a serial port
and a register image, with no variable table behind it.

## What is shared, and why

The two transports differ about **framing** and about nothing else. RTU frames
are length-driven with a CRC and no header; TCP frames carry an MBAP header and
no checksum. Everything inside the frame is the same protocol.

That was not obvious while both halves checked responses separately, in two
spellings that looked unrelated because one counts a whole ADU and the other
counts an MBAP length field. Written in BODY bytes - everything after the unit
byte, the transport's trailer already accounted for - they are one function:

```
             RTU (len)        body_len       TCP (mlen)      body_len
  exception  len == 5         5 - 3 = 2      mlen == 3       3 - 1 = 2
  read       len == 5 + want  2 + want       mlen == 3+want  2 + want
  write echo len == 8         8 - 3 = 5      mlen == 6       6 - 1 = 5
```

So `wire.h` holds the vocabulary (function codes, exceptions, quantity ceilings,
frame arithmetic - header-only `constexpr`, no state, no buffers) and
`response.h` holds the answer-check. Both transports agree there or nowhere. A
rule added to one used to reach only one of them.

## What the bus costs

Every number the planner and the client are built around comes from one split.
RTU is half-duplex and strictly request/response - one transaction in flight, no
pipelining - and a transaction costs the inter-frame silence, the bytes, and the
slave's own turnaround. Only the middle term scales with baud; turnaround is the
slave's firmware and does not move. At 19200 with a 10 ms turnaround:

| | fixed, per transaction | marginal, per register |
|---|---|---|
| 19200 | 19.45 ms | 1.15 ms |
| 115200 | 12.99 ms | 0.19 ms |

**A register costs a 17th of a transaction at 19200, a 68th at 115200.
Transactions are the scarce resource; registers are nearly free.** Three things
follow, and they are the whole design:

- **Bulk or nothing.** The grid meter's 150 registers cost 3.09 s read one at a
  time and 211 ms as two blocks. Single-register polling of the plant is 4.4 s,
  which puts a 1 s update out of reach even at 115200.
- **Merge across gaps.** Break-even is `fixed / (2 x Tchar)` - **17 unused
  registers at 19200, 68 at 115200**. Reading registers nobody wants is cheaper
  than a second transaction, which is why `plan.h` coalesces across a gap
  instead of splitting at one.
- **Baud buys less than it looks.** Sweeping the whole plant is 16 transactions,
  555 ms at 19200 against 249 ms at 115200 - 2.2x for 6x the wire, and under 2x
  once the beat quantises each transaction. At the declared poll periods that is
  **~20 % bus duty at 19200**, so the default stands and 38400 is the first step
  up. Above that, service the UART more than once per beat.

Two ceilings bound a block, and neither is the protocol's. `BufferedSerial`'s RX
ring is 256 bytes, 255 usable, and a maximum FC03 response is exactly 255 - one
byte of margin. And the beat: a 125-register read is 163 ms, 16 beats in which
nothing else would run, which is why the client is a `step()` state machine
holding one transaction rather than a call that blocks.

**The real bandwidth risk is a dead device, not register count.** One unplugged
sensor pays a full response timeout on every sweep; three of them at a 300 ms
timeout with two retries burn 2.7 s per sweep and dwarf the entire 555 ms
budget. So the deadline is sized tightly (worst turnaround plus the response,
~300 ms at 19200 - not the 1000 ms libraries default to), one immediate retry,
then per-device exponential backoff to a slow probe and a comms alarm. That is
what actually takes a field bus down.

## The master, end to end

@image html modbus-master.drawio.svg "The master, end to end. The planner is bounded by the device table, and what comes back off the wire lands in the same variable table the plan was built from."

**Planning** (`plan.h`) groups the site's `XP_MODBUS` rows into read and write
**blocks**, one per contiguous run on one unit with one function code. It is
bounded by what the device can actually answer, which is why there is a device
table at all.

**The device table** (`device.h`) is a `DeviceModel` per part number - register
ceiling, coil count, address and quantity alignment, multi-write support,
minimum gap - and a `Device` per unit binding a model to an address and to the
plant's own **tag**. A site declares both in `vars.cpp` (@ref int_variables) and the
generator emits them, so the checks can hold a row's address against the model
it lands on. A unit nobody described plans against the protocol's own
limits rather than acquiring invented ones: a model entry is only as good as the
manual behind it.

The constraints are not decoration. An SDM630 answers exception 02 to a request
that starts on an odd register, and a request longer than its 80-register
ceiling gets an exception rather than a short read - which on a bus reads exactly
like a bad cable. The planner pads to alignment and cuts at the ceiling, and
counts what it could not place: `rows_dropped()` and `rows_unscanned()` are both
criticals at bring-up, because a silently unscanned row is a sensor that is
simply never read.

The tag is what makes a log line say `dev 34 (SRR/TDC/RO-4) not answering`
instead of `dev 34`. Optional in the library; the reference site requires one for
every unit, since a nameless device is one somebody added and stopped halfway.

### Scheduling: one clock, no ladder {#dev_modbus_rank}

`client.h` picks the next block with a single scoring pass. Every block is
ranked on how far it is through the lateness it is allowed, **in permille**, and
the highest wins. Reads and writes differ only in how that pair is DERIVED
(`_slot_of()`: a poll period on one side, the minimum write interval and
`MODBUS_WRITE_DEADLINE_MS` on the other), never in how they are compared.

Relative and not absolute, because ranked by milliseconds the fast rungs hold
the bus for ever: they re-arm constantly while a slow block's lateness only
creeps up. In permille, 900 ms late on a 1 s rung and 8 s late on a 10 s rung
score the same. There is no tier ladder, and the per-block starvation floor is
not a jump between tiers but simply the point where urgency passes 1000.

The one absolute pre-empt sits AHEAD of the scale rather than on it: a dirty
write while a safe state is owed. A scale has no top, so a late enough block
could otherwise climb into any rank reserved above it.

### A silent device must not take the bus down

An unplugged device costs a full response timeout, and left alone that is paid
on EVERY sweep until the live devices lose their rates to the dead ones. Three
things stop it, and the third is the one that matters at scale:

- Each device carries its own health: consecutive failures, and the earliest
  time it may be spoken to again. One retry, then backoff doubling from
  `MODBUS_BACKOFF_MS` to `MODBUS_BACKOFF_MAX_MS`.
- A miss does not push a read's retry out by a whole poll period
  (`MODBUS_RETRY_DELAY_MS` caps it), so how long a device takes to be declared
  dead is a property of the fault and not of the rung it happens to sit on.
- At most ONE device that is not answering is offered the bus at a time (a
  cursor, round-robin), and not until `MODBUS_PROBE_SPACING_MS` has passed since
  the last transaction that failed (a gate). Their share of the bus is therefore
  `timeout / (timeout + spacing)`, INDEPENDENT of how many are down.

Without the interleave a probe is enormously late against its own period and
wins every comparison it enters, so the least valuable and most expensive
transaction on the bus goes first, and N dead devices arrive as one contiguous
burst of response timeouts every backoff interval.

The bound is on the STEADY STATE. A device that has never missed is presumed
good, so its first failure cannot be gated - there is no way to know a device is
down without trying it - and discovering N of them costs N timeouts once.

A REFUSAL clears the device's count: an exception means it heard us, understood
us and declined, so it is alive and its register map is the problem. The BLOCK
backs off instead, doubling to `MODBUS_REFUSAL_MAX_MS`, since a refused write
stays dirty and nothing else would ever slow the retry down.

### Why the master is stepped twice

`on_input()` runs at the top of the beat, so a control program sees the freshest
image; `on_output()` runs after the programs have executed, so a demand written
this beat reaches the wire without waiting for the next one. Stepping it once
would cost a beat of latency in one direction or the other.

`on_output()` runs whatever control authority says. Withholding control drops
every output to its safe state and stops the scan, but those safe values sit in
the image until a transaction carries them out: gating the collector too would
leave the actuators energised at exactly the moment control was withdrawn.

### A frame goes out whole or not at all

The driver enable is hardware-timed: the USART drops DE when its shift register
empties, so a frame handed to the UART in pieces would have DE fall mid-frame
and every slave on the bus would see a truncated frame - which reads as a wiring
fault, not a firmware one. A `static_assert` in `client.cpp` proves a full FC16
fits the transmit ring (`MODBUS_TX_RING_BYTES`), and `on_output()` will not begin
a transaction until `fieldbus_tx_idle()` says the ring has DRAINED, not merely
that it has room for one more byte. A refused byte is then impossible, and is
logged as a critical if it ever happens anyway.

**Values** move through `image.h`, both directions, and a read failure
invalidates the rows it covered (`Q_REFUSED`) rather than leaving the last good
value in place at `Q_GOOD` - a row declared with no timeout would otherwise never
be caught up with at all.

The scan never touches the wire. A sweep of this plant is hundreds of
milliseconds - tens of beats - so cycle-boundary I/O is out of reach: the bus
runs on its own clock and maintains a **process image** the cycle reads.
`Vars::Bank` already is that image and `XP_MODBUS` already marks the rows the
driver owns, so `image.h` builds nothing - it only moves values in two
directions.

## The RS-485 echo

The OPTA drives DE from `USART3_DE` and holds RE low, so the master **hears its
own transmission** on the way out. Every frame sent comes back as the first
bytes received.

The handling is a counted skip: `arm(e, now, timeout, echo_len)` tells the framer
exactly how many bytes to discard, and `HAL::Serial::fieldbus_echoes()` says
whether this backend does it at all - true on OPTA, false on the simulator, whose
socketpair does not echo. The count is not optional and not inferred. A
prefix-match heuristic was tried first and is a trap: with no echo present it
eats the real reply's unit and function-code bytes, which reads as a malformed
frame from a healthy device.

## The TCP slave

`tcp/map.h` is the register map over the variable table. The declared `reg` is a
**row number**, not an address: one register is 16 bits, most rows are wider, and
the declared numbers are dense, so a row cannot span `reg` and `reg+1` without
landing on its neighbour. Each Modbus table derives its own address from the same
number:

| Table | FC | Address | Holds |
|---|---|---|---|
| Holding registers | 03, 06, 16 | `2 * reg` | the value, 2 registers, high word first |
| Input registers | 04 | `2 * reg` | the same, read-only alias |
| Discrete inputs | 02 | `reg` | the row's `valid()` |
| Coils | 01, 05, 15 | `reg` | boolean rows; writable only if the row is a parameter |

`2 * reg` is injective, so the site test's guarantee that no two rows share a
register carries over to the addresses without further checking.

**Gaps read, they do not fault.** The map is sparse - every number nobody
assigned, plus the odd register of a row needing only one. A read overlapping a
gap returns zeros there, and only a request landing entirely outside the mapped
span is refused; a read starting or ending mid-row gets that row's corresponding
half, so a master polling a round block need not know where the rows begin. A
WRITE is the opposite: it must cover a row completely, or half a 32-bit value
would be committed as a whole one. FC06 is the exception - it addresses half a
row, so the addressed half is spliced into what the row already holds and the
whole row is validated and committed as a pair write would be, because writing
one setpoint is the commonest thing a SCADA does.

**Quality is a discrete input.** Modbus has no quality bit, so an invalid reading
would be indistinguishable from a real zero. An invalid row publishes its
declared substitute (`def`, what every other consumer sees through `Var::raw()`)
and reads 0 at discrete input `reg`. One FC02 block gives a master the quality of
everything it just read.

Registers are **hand-assigned per row**, so inserting a parameter never
renumbers the map an external master holds. What the number itself says is the
block it falls in:

| Range | Rows |
|---|---|
| 1-799 | site parameters and status values |
| 800-899 | site alarms |
| 900-985 | library parameters and status values |
| 986-999 | library alarms (the platform ones, and the three subsystem faults) |

Uniqueness alone would let an alarm take 750 and read as a parameter to
everyone downstream, so `test_site.cpp` asserts that every row sits in the block
its scope puts it in - not merely that no two rows collide.

A write reaches `Bank::set()`, which validates against the row's declared band.
Persisting is queued rather than synchronous: a master writing forty parameters
in one transaction would otherwise be forty flash erases inside one beat. The
queue drains a few per beat and answers `EX_SLAVE_BUSY` when it is already full,
which is a slave saying "ask again" and not a rejection.


## The TCP slave, in detail

### Two phases, one request {#server_beat}

The server is stepped TWICE per beat, from two different phases:

 - `step()` in `PHASE_INPUT` accepts, reads, decodes, and applies WRITES, so a
   setpoint written this beat is what the control programs run on this beat
   and `PHASE_CONFIG` picks up the group serial in the same cycle.
 - `service_tx()` in `PHASE_PRESENT` answers READS, off the image the scan just
   produced, and pushes the bytes out.

The cost is one beat of latency on a read, against a SCADA poll measured in
seconds. What it buys is that no request is answered from a half-updated
image: nothing writes the bank between the output phase and the encode.

### The acknowledgement block {#server_ack}

An alarm is published as a READ-ONLY coil at its row number (map.h), but
acknowledging one is a write and is what an operator does at a SCADA station.
It gets a block of its own at `MODBUS_TCP_ACK_COIL_BASE + reg` rather than a
second meaning on the alarm's own bit:

 - FC01 there reads the condition's `user_ack`, which clears itself on the
   next fresh ON edge, exactly as the ALARMS page shows it;
 - FC05 with 0xFF00 acks that condition - the same `AlarmController::ack()`
   the `a` key and `alarms ack` call;
 - FC05 at the BASE address acks every active condition. `reg == 0` is never
   assigned to a row, so the base is free and means "the lot";
 - FC05 with 0x0000 is refused: un-acknowledging is not something the model
   can express, and quietly ignoring it would look like it worked.

The block exists only when the server was given an `AlarmController`, and it
is gated on `mb_write_enable` like any other write, because it is one.

### Bounded per beat {#server_bounded}

Like every task: at most `MODBUS_TCP_ACCEPT_PER_STEP` accepts, ONE request decoded
per connection (a master may pipeline; the extra bytes wait), and the protocol
ceilings on the request itself. A partial send resumes next beat rather than
blocking.

## RTU framing

### Why not t3.5 {#fr_silence}

Modbus RTU delimits frames by 3.5 character times of silence: 2.005 ms at
19200, and a spec-fixed 1.75 ms above it. This firmware cannot observe that -
the framer is only LOOKED AT once per 10 ms beat, so every gap it could
measure has already closed. Three things replace it, and together they are
strictly stronger than a timer would be:

 - @ref RtuFramer::arm() drops whatever the previous transaction left behind,
   so every frame begins at a known offset rather than at a guessed boundary.
 - The LENGTH is the delimiter. A response's size is knowable from the
   function code plus, for a read, its byte count - and the byte count is
   checked against the request before it is believed.
 - The CRC confirms it. A frame that ends where we thought it did, and
   checksums, is the frame that was sent.

The response deadline is a backstop for a slave that says nothing at all, not
a frame boundary. Nothing here treats elapsed time as evidence about where a
frame ends.

It takes bytes and a clock reading, and has never heard of BufferedSerial,
HAL::Serial, the beat or RS-485: so it is tested with array literals and a
fake @c now_ms, and servicing the UART more than once per beat (which the
budget doc wants above 38400) is a change in the caller.


### Framing without a length field {#sl_framing}

A slave knows nothing until it has read the function code, and then the length
follows from it: eight bytes for the single-value functions, and for the
multi-writes a byte count that is itself inside the frame. A request is
complete when `request_len()` says it is, never because a gap in the bytes
suggested it.

It answers from a flat register image and a flat coil image, addressed exactly
as the wire addresses them. No variable table, no bank: whoever owns the
instance decides what the registers mean, and the simulator fills them from
the site's own table (botanik_sim/fieldbus.h).

## Text rows, and the IPv4 quad (planned)

Modbus has no string type: only 16-bit registers and 1-bit coils. Text is carried
by convention, and every vendor answers three questions differently - byte order
inside a register, termination, and fixed against length-prefixed.

Today a `T_STR` row is **reserved, not served**: `Map::published()` excludes it,
so the slave answers `EX_ILLEGAL_ADDRESS` rather than the zero it used to return
for ever. The register stays assigned so numbering does not shift. Text is set
over the console or the shell.

The numbers are why. All eight text rows are `BOTANIK_HOST_STR_LEN` = 64 bytes,
so packed two bytes to a register they need 32 registers each - **256 in total**,
against a library parameter block (900-985) that is 86 wide and already holds 53
rows. Packed text does not fit without a new region.

**Decided, not yet built:** the five `STR_IPV4` rows (`net_ip`, `net_mask`,
`net_gw`, `net_dns1`, `net_dns2`) get a quad of **one register per octet**, four
registers each, twenty in total. One octet per register rather than two packed
into one is deliberate: a register then holds a plain 0-255 and there is no byte
order for a tool to render backwards, which is the ambiguity that makes packed
text an integration surprise. Validation stays exact - four octets, nothing to
parse. The write must be FC16 across the whole quad and atomic, since a
single-register write of one octet is a half-formed address.

The three hostname rows (`ntp_srv1`, `ntp_srv2`, `mqtt_host`) stay console-only:
they genuinely need packed text, and they are the rows least worth reconfiguring
from a master. They also stop carrying a default of their own - **an empty host
falls back to the gateway**, resolved in `firmware/bindings.cpp`, which is already
the one file that sees a component and the bank at the same time. On a plant
network behind a 4G modem the router is the sensible default for both.

## The simulated field

The simulator does not stand in for the driver - it exercises it.
`botanik_sim/fieldbus.h` builds an `RtuSlave` per unit the site addresses,
copies each beat's simulated values into those register images, and carries real
framed bytes over a socketpair. The master runs its true state machine against
devices that turn around, answer exceptions, and can be absent.

Which makes the harness's own limits load-bearing. It caps at 64 units and
**refuses to start** if the site names more, because a sim quietly missing four
devices reports them as unanswering, and that is the one diagnosis that sends an
engineer to the cabinet rather than to the code. It sat at 16 while the site grew
to 20, and that is exactly what happened.

## See also

- @ref dev_scan - the beat the master is stepped in (`PHASE_INPUT` / `PHASE_OUTPUT`)
- @ref dev_vars - the variable model the whole thing reads and writes
- `modbus/tcp/map.h` - the addressing, encoding and gap rules in full
