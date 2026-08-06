# The field bus: Modbus RTU

What it costs to read a register over RS-485, how many the plant can afford per
second, and the rules the client is built to. This page is the **budget** the
driver has to fit in; it was written before the client, so what it describes is
the design and not yet the code.

Every `XP_MODBUS` row in a site's table (@ref dev_vars) already carries its own
binding - `{device, fc, reg, regs}` in `Vars::Variable::Binding` - so the table
is the bus map. Nothing extra has to be declared to plan the traffic; it has to
be *grouped*.

## The cost of a transaction

RTU is half-duplex and strictly request/response: one transaction on the wire at
a time, no pipelining, no overlap. A transaction costs

```
 T = t3.5  +  (req + resp) x Tchar  +  T_turnaround
     silence   the bytes             the slave thinking
```

with 11 bits per character (start + 8 data + parity + stop; the no-parity
variant spends the same 11 on a second stop bit).

| | 19200 | 115200 |
|---|---|---|
| `Tchar`, 11 bits | 0.573 ms | 0.0955 ms |
| `t3.5` inter-frame silence | 2.005 ms | 1.750 ms (spec-fixed above 19200) |
| Slave turnaround | 2 - 30 ms, device dependent | *unchanged by baud* |

That last row decides everything. Turnaround is the slave firmware's own latency
and the link speed has no effect on it, so raising the baud rate buys far less
than the ratio suggests. The tables below use 10 ms, a fair figure for a modern
RS-485 meter; cheap ones are 20 - 50 ms.

An FC03 read of N registers puts `8 + (5 + 2N)` bytes on the wire. So:

| Registers | Bytes | 19200 | 115200 |
|---|---|---|---|
| 1 | 15 | 20.6 ms | 13.2 ms |
| 2 (one T/RH sensor) | 17 | 21.7 ms | 13.4 ms |
| 10 | 33 | 30.9 ms | 14.9 ms |
| 32 | 77 | 56.1 ms | 19.1 ms |
| 125 (FC03 max) | 263 | 162.7 ms | 36.9 ms |
| 32, written with FC16 | 81 | 58.4 ms | 19.5 ms |

Split that into the part that scales and the part that does not:

| | 19200 | 115200 |
|---|---|---|
| **Fixed, per transaction** | 19.45 ms | 12.99 ms |
| **Marginal, per register** | 1.15 ms | 0.19 ms |

A register costs a **17th** of a transaction at 19200, and a **68th** at 115200.
Transactions are the scarce resource. Registers are nearly free.

## Why single-register reads are not an option

Reading the grid meter's 150 registers, one FC03 each, against reading it in two
bulk blocks:

| | 19200 | 115200 |
|---|---|---|
| 150 x FC03(1 reg) | **3.09 s** | 1.98 s |
| FC03(125) + FC03(25) | **211 ms** | 54.7 ms |

15x at 19200, 36x at 115200. Single-register polling of the whole plant (211
registers) costs 4.4 s at 19200 and 2.8 s at 115200, which puts a 1 s update on
the grid meter out of reach *even at 115200*. Bulk transfer is not an
optimisation here, it is the difference between a working bus and a broken one.

## The plant, swept once

Every register the site reads and writes, in bulk, at a 10 ms turnaround:

| Group | Txns | 19200 | 115200 |
|---|---|---|---|
| Grid meter, 150 regs | 2 | 210.8 ms | 54.6 ms |
| Local meter, 5 regs | 1 | 25.2 ms | 14.0 ms |
| 12 x T/RH, 2 regs each | 12 | 260.9 ms | 160.5 ms |
| Outputs, 32 regs, one FC16 | 1 | 58.4 ms | 19.5 ms |
| **Total** | **16** | **555 ms** | **249 ms** |

6x the baud rate returns **2.2x** the sweep. At a 30 ms turnaround it is 1.5x
(874 ms against 568 ms). The twelve sensors dominate the fast link and are pure
transaction overhead: twelve slave addresses, two registers each, nothing to
coalesce.

## Poll groups, and the resulting duty cycle

Nothing is swept at one rate. Each group gets the period its physics asks for:

| Group | Regs | Period | Duty @19200 | @115200 |
|---|---|---|---|---|
| Grid live (P, I, V, PF) | ~12 | 1 s | 3.3 % | 1.6 % |
| Grid slow (energy, harmonics) | ~138 | 60 s | 0.4 % | 0.1 % |
| Local meter | 5 | 1 s | 2.5 % | 1.4 % |
| T/RH x 12 | 24 | 10 s | 2.6 % | 1.6 % |
| Outputs (worst case) | 32 | 500 ms | 11.7 % | 3.9 % |
| **Bus duty** | | | **~20 %** | **~9 %** |

**19200 carries the whole plant at a fifth of the bus, leaving 80 % of headroom.**
That is the answer to whether there is enough bandwidth: there is, on the slow
link, with room for the site to grow.

10 s on temperature and humidity is already generous against a greenhouse's
thermal time constant of minutes, and the function blocks take `dt_ms` rather
than assuming their nominal period (@ref int_tasks), so a slower group
costs them nothing. Energy totals belong at 60 s. Only the live power block
wants a second.

## The design that follows

Every serial Modbus master in a PLC - CODESYS channels, S7 `MB_MASTER`, an
OPC-UA server's tag groups - lands on the same four rules, and the numbers above
are why.

**1. A process image, not a bus read.** The scan never touches the wire. The
driver fills the variable bank asynchronously; tasks read `Var::raw()` and cannot
tell a bus exists. `Vars::Bank` *is* the process image already, and `XP_MODBUS`
already marks the rows the driver owns.

\dot "Bus and scan meet only at the bank"
digraph modbus_image {
  rankdir=LR; node [shape=box, fontsize=10]; edge [fontsize=9];
  bus   [label="Modbus client\n(one txn in flight)"];
  bank  [label="Vars::Bank\nprocess image", shape=box3d];
  scan  [label="Scan::Driver\ncontrol programs"];
  bus  -> bank [label="inputs, on response"];
  bank -> scan [label="raw() / valid()"];
  scan -> bank [label="demands"];
  bank -> bus  [label="outputs, on change"];
}
\enddot

**2. Poll groups with their own periods, planned into blocks.** Group the rows by
`(device, fc)`, coalesce contiguous `reg` runs, and give each block a period. The
coalescing rule falls out of the cost split: **merge across a gap of up to 17
unused registers at 19200, and up to 68 at 115200** (break-even is
`fixed / (2 x Tchar)`). Reading registers nobody wants is cheaper than issuing a
second transaction.

**3. Outputs are written on change, not polled.** Coalesce a slave's dirty
registers into one FC16, hold a minimum interval between writes (200 - 500 ms),
and re-write unconditionally every 5 - 10 s so a slave that rebooted gets driven
again. Writes take priority over reads in the queue.

**4. A group timeout is what makes a reading stale.** Each group has a response
deadline; on expiry its rows go invalid. The library does this already through
`ROLE_INPUT` expiry and `Bank::expire_stale()` - the only new obligation is to
declare each row's timeout at roughly **3x its group period**, or a 10 s sensor
group under a 5 s timeout flaps between valid and stale forever.

## Four traps specific to this firmware

**No bus I/O inside the beat.** A 125-register read is 163 ms at 19200, which is
16 beats of `TASK_PERIOD_MS`. The client has to be a `step()` state machine
holding one transaction in flight, like `NtpClient` (@ref dev_network) - which
RTU's half-duplex would force anyway. The protocol ceilings (125 registers per
FC03/FC04, 123 per FC16, 2000 coils per FC01/FC02) are nowhere near binding; the
beat is.

**The RX ring has one byte of margin.** `BufferedSerial::RX_BUF_SIZE` is 256, so
255 usable, and a maximum FC03 response is exactly 255 bytes (5 + 250). Drained
by polling, with no serial ISR. Either cap a planned block near 100 registers or
give the Modbus port a 512-byte ring.

**The 10 ms beat quantises every transaction.** Detecting "response complete"
once per beat adds up to a beat to each transaction - about 80 ms across a
16-transaction sweep, on average. That is 14 % on top at 19200 but 32 % at
115200, eroding the fast link's advantage further. Above 38400, service that UART
more than once per beat.

**Dead-slave backoff is the real bandwidth risk**, well ahead of register count.
One unplugged sensor spends a full response timeout on every poll; three dead
sensors at a 300 ms timeout with two retries burn 2.7 s per sweep and dwarf the
entire 555 ms budget. So: size the response deadline tightly (worst turnaround
plus the response time, so ~300 ms at 19200 and ~100 ms at 115200 - not the
1000 ms that libraries default to), allow one immediate retry, then back that
slave off exponentially to a 30 s probe and raise a comms alarm. This is what
actually takes field buses down.

## Recommendation

Run the bus at **19200**. It carries the plant at ~20 % duty at the poll rates
above, it survives long greenhouse cable runs with stubs, and every RS-485 meter
supports it. 115200 returns 2.2x on paper, under 2x after beat quantisation, and
close to nothing against a slow slave, in exchange for real signal-integrity
risk. 38400 is the step to take first if a site outgrows it.

Two numbers to measure before committing a site: **the grid meter's actual
turnaround** (a scope or a logic analyzer on one transaction; it moves these
tables more than the baud rate does), and **whether its 150 registers are
contiguous** - meters often scatter them, and 32-bit values spanning a register
pair bring word-order quirks that want a per-device flag.

## How the bus would meet the scan cycle

Cut from the manual's scan page when that page was trimmed to what exists. This
is the boundary the client has to implement.

**The cycle cannot read the wire.** The textbook PLC cycle is read inputs,
execute, write outputs, once per cycle, with I/O synchronous at the cycle
boundary. That works when I/O is backplane-local and costs microseconds. A sweep
of this plant is 555 ms at 19200, or 55 beats, so inputs cannot be refreshed at
the top of a 10 ms beat and no tuning changes that. The bus runs on its own clock
and maintains the process image; the cycle reads the image.

**Split the client like the serial pump.** `BufferedSerial` is one Component
whose halves the loop drives at the phases they belong to - RX at the top, TX
after the render. `receive()` parses completed responses and commits them, so a
program sees the freshest image; `transmit()` issues the next request, so a demand
written this beat reaches the queue without waiting for the next. Stepping it once,
at either end, costs a beat of latency in one direction or the other.

**Collect must not be gated with execute.** `disable_control()` drops every
`ROLE_OUTPUT` row to its safe state and inhibits the execute phase, but those safe
values are only in the image until a transaction carries them. Gating the
collector alongside the programs would leave the actuators energised at exactly
the moment control was withdrawn. A safe-state write should be queued ahead of
ordinary traffic.

**The reset path needs the slave's help.** Control is withdrawn before the loop
stops kicking the watchdog, precisely so the field is safe before a reset. With
local I/O that is instantaneous; over Modbus it is a transaction (~58 ms for an
FC16 of 32 registers at 19200) and a reset can beat it to the wire. Do not try to
win that race - configure the **slave-side failsafe**, so an output module that has
heard nothing within its own timeout drops its outputs itself. That covers a
severed cable and a wedged master too, and it is the only mechanism that works
when the master is the thing that failed.

**Write on change, not on poll.** The collector walks the `ROLE_OUTPUT` rows,
coalesces a slave's dirty registers into one FC16, and holds a minimum interval
between writes (200 - 500 ms). A periodic unconditional rewrite (5 - 10 s)
re-drives a slave that rebooted and doubles as the keepalive its failsafe timer
watches for. Dirt is marked in the `Value` on write rather than diffed against a
shadow: the struct is pinned at 32 bytes by a `static_assert`, but it already
carries a `uint8_t _rsv[3]`, so the flag is free.

**Collect at least as often as the fastest control program**, or a demand can be
superseded before it is ever collected. Harmless for `ROLE_OUTPUT` level demands
where last-value-wins is correct; it would quietly break anything edge- or
pulse-shaped.

**Poll groups are tasks.** A group is a name, a period and some work - the same
shape as everything else on the beat. `modbus_grid_live` at 1 s,
`modbus_sensors` at 10 s. An activation enqueues a transaction; the client's
state machine drains the queue from the fast group. One scheduler, not a second
one bolted alongside.
