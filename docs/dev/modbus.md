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

**Scheduling** (`client.h`) picks the next block with a single scoring pass:

```
 safe state   4000000   an output must reach its declared safe value
 starved      3000000   a block that has missed its period
 write        2000000   a demand the operator or a program has made
 read         1000000   the routine scan
```

Priority is a number, not a place in a sequence, so changing one is an edit to a
constant rather than a reordering that has to be re-argued.

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
