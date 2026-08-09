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

## The master, end to end

```
 vars table ─▶ Planner ─▶ blocks ─▶ RtuClient ─▶ codec ─▶ framing ─▶ wire
                  ▲                    │
              DeviceTable              ▼
             (ceilings,            image.h ─▶ vars table
              alignment)
```

**Planning** (`plan.h`) groups the site's `XP_MODBUS` rows into read and write
**blocks**, one per contiguous run on one unit with one function code. It is
bounded by what the device can actually answer, which is why there is a device
table at all.

**The device table** (`device.h`) is a `DeviceModel` per part number - register
ceiling, coil count, address and quantity alignment, multi-write support,
minimum gap - and a `Device` per unit binding a model to an address and to the
plant's own **tag**. A unit nobody described plans against the protocol's own
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

`tcp/map.h` is the register map over the variable table. Registers are
**hand-assigned per row**, so inserting a parameter never renumbers the map an
external master holds. Parameters and status values share the space; the library
reserves 900-999 and a site's alarms sit at 800-899.

A write reaches `Bank::set()`, which validates against the row's declared band.
Persisting is queued rather than synchronous: a master writing forty parameters
in one transaction would otherwise be forty flash erases inside one beat. The
queue drains a few per beat and answers `EX_SLAVE_BUSY` when it is already full,
which is a slave saying "ask again" and not a rejection.

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
- `docs/design/modbus-review.md` and `modbus-remediation.md` - the review that
  produced most of the above, and the fix path
