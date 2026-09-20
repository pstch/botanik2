# Modbus {#dev_modbus}

<!-- source of truth: modbus/wire.h, modbus/rtu/, modbus/tcp/ -->

![](modbus-master.drawio.svg)

## The four corners

Modbus appears here four times, and it is worth being clear that these are four
different programs sharing a frame format rather than one library with options.

| | what it is | why it exists |
|---|---|---|
| `rtu/master` | RS-485 master, polling the field | the process image comes from here |
| `rtu/slave` | RS-485 device model | the simulator's field devices; on hardware, how this unit appears to somebody else's master |
| `tcp/slave` | TCP server over the register map | how SCADA reads the plant |
| `tcp/master` | TCP client | the simulator's self-check against its own map, and a way for a site to read a second PLC |

Each is behind its own `BOTANIK_FEATURE_MODBUS_*` flag, and a build takes only
the corners it uses. What they share is `wire.h` - the frame arithmetic - and
`response.h`, the checks that are the same function on both transports written in
body-relative coordinates.

`wire.h` states each length once, as a single-expression `constexpr`:
`request_len`, `response_len`, `read_payload`. The builder, the parser, the
framer, the slave, the TCP codec and the bus cost model all derive from those
same expressions, so **a disagreement about how long a frame is is not
expressible**.

## RTU framing

### The silent interval {#fr_silence}

The spec delimits an RTU frame with 3.5 character times of silence. This
platform cannot hear that. The beat is 10 ms and a character at 19200 baud is
572 us, so by the time a task looks at the UART, any gap it might have measured
is long gone - and the OPTA's buffered serial does not timestamp bytes.

So the framer does not look for the end of a frame. **It computes where the end
must be, from the request it sent**, and reads exactly that many bytes. One
transaction is in flight at a time - RTU is half-duplex and strictly
request/response - so there is never a second frame to disambiguate.

Two things fall out of that, both load-bearing:

- **The exception bit is checked before the request is consulted at all.** A
  refusal is five bytes whatever was asked, so a framer that sized from the
  request would sit waiting for a tail that is never coming.
- **A byte count that disagrees with the request is refused before anything is
  sized from it.** The length field arrives from the wire; trusting it to size a
  buffer is how a device with a stuck bit becomes a memory fault.

Time is still evidence of one thing: a slave that has said nothing at all. That
is the timeout, and it is the only place a deadline appears in the framer.

On a CRC failure the receiver resyncs **by one byte**, not by dropping the frame:
a stray byte from a second master or a mid-frame reset should cost one byte of
resynchronisation, not a whole transaction.

## The field-bus master

The master is one transaction in flight, stepped in two phases of the beat -
input before the bank's expiry sweep, so the sweep judges this beat's answers
rather than the ones from before them.

The premise underneath everything else here: **transactions are the scarce
resource and registers are nearly free.** At 19200 baud with a 10 ms turnaround a
transaction costs about 19.4 ms before it carries anything, and each additional
register adds about 1.1 ms. So the planner reads in bulk and merges across a gap
up to the point where the skipped registers would cost a transaction's worth of
bytes.

That is not a small effect. On a real site of 75 field rows across 16 devices, one
transaction per row is 75 transactions a sweep and about 63% bus duty; the plan
is 19 transactions and about 32%. **The planner halves the bus load.**

A poll period comes from the row's own staleness timeout, not from a tuning
knob: a group polled slower than a third of its `timeout_ms` would flap between
valid and stale forever, so the period is `timeout_ms/3` floored onto a short
fixed ladder. The ladder is what bounds the plan at devices x function codes x
rungs.

`plan.h` ends with `static_assert`s pinning that arithmetic to the figures
published on this page. They are not testing the compiler - they are what stops
the codec and the budget document drifting apart.

### Ranking the plan {#dev_modbus_rank}

Which block goes out next is ranked on **lateness in permille of its own
period**, not on a tier or a priority number. A block due every second that is
500 ms late and a block due every minute that is 30 s late are equally late, and
that is the comparison that matters: a fast group does not get to starve a slow
one merely for being fast.

A silent device is the harder problem. Each unanswered transaction costs a full
timeout, so a naive poller with four dead sensors on a one-second rung spends
more than a second per second waiting, and the bus stops. Two mechanisms bound
it:

- **A suspect is probed, not polled.** A device that has missed is taken off the
  normal rotation and retried on a round-robin cursor, so the cost of N dead
  devices does not scale with N.
- **The retry interval backs off, doubling to a cap - and it is capped BEFORE it
  doubles.** Capping after would let an operator who set an unbanded override
  wrap the interval to zero.

Together these hold the dead-device cost at `timeout/(timeout + spacing)` -
about 17% of the bus - **independent of how many devices are quiet**.

The image is written back per row against the payload that was actually sent, not
against what the row holds now, so a demand a control program moved mid-flight is
not silently marked clean.

## The RTU slave

### Framing {#sl_framing}

The mirror image of the master, and the asymmetry is the point: a slave has no
request of its own to size from, because the request is what is arriving.

So the slave sizes from the function code as the bytes come in, and caps the
expected length at `ADU_MAX` before it is used. Without that cap, an impossible
length field makes the device **permanently deaf** - it waits forever for a tail
that cannot arrive, and no amount of correct traffic afterwards recovers it.

Broadcast (unit 0) is executed and not answered. A broadcast READ is refused
rather than answered to nobody, because a master that broadcasts a read has made
a mistake worth surfacing.

## The TCP server

### Bounded work {#server_bounded}

Every loop here is bounded, because this is the one subsystem a remote peer can
drive. The accept path is bounded by `ADU_MAX`, the drain by a fixed byte budget
per beat, and a flush that does not complete resumes on the next beat rather than
spinning.

Connections are a small fixed table, not a list. A connection past the ceiling is
accepted and closed immediately and counted in `rejects` - so the refusal is
visible on the SYSTEM page rather than appearing as a silent failure to the peer.

### One beat {#server_beat}

`pdu.h` owns the framing, `map.h` owns the meaning, and the slave owns the
timing. It is a `Component` like every other config-driven subsystem: it reads
the `mb` parameter group and restarts its listener when that group's commit
serial moves, which is how a port change takes effect without a reboot.

A write of many registers is validated **completely before any of it is
applied**. Modbus has no partial-success answer, so a request that half-succeeds
leaves the master with no way to know which half.

Writes that must persist are queued and paced rather than committed inline. A
single 61-register write would otherwise mean 61 synchronous flash erases inside
one beat, remotely triggerable by anybody who can reach the port.

### Acknowledgement {#server_ack}

Alarms are acknowledged over the wire through a dedicated coil block above the
register map, resolved by lookup rather than by search. It is separate from the
map because acknowledging an alarm is not writing a value - it is an operator
action that happens to arrive over Modbus, and folding it into the register space
would make it indistinguishable from one.

## The TCP master

The smallest corner, and mostly a test instrument: it is how the simulator checks
that the map it publishes actually serves what it claims. A site may also use it
to read a second controller, which is why it is a feature flag and not a
simulator-only file.

## What this does not do

See @ref dev_blind_spots for the field bus's own list. The one most likely to be
felt: **the RS-485 line format is compile-time.** Baud, parity, data bits and
stop bits come from the build, while timeout, retries, backoff and the write
cadence are all live parameter rows. Commissioning is exactly when the line
format is least settled, and closing this is not free - the planner's cost model
is derived from the baud, so changing it has to re-plan the sweep.
