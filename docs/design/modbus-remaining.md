# Modbus: what is left

Both halves are merged and building - the RTU master (`modbus/rtu/`) and the TCP
slave (`modbus/tcp/`) over one shared vocabulary (`modbus/wire.h`). Neither is
instantiated by `BotanikFirmware` yet, and nothing steps either of them. This is
the list from there, in the order the pieces unblock each other.

## The layout they landed in

```
modbus/
  wire.h          the vocabulary and the frame arithmetic - BOTH halves agree
                  here or nowhere: function codes, exceptions, the quantity
                  ceilings, the byte order, the bit packing
  rtu/            the field-bus MASTER: this unit polling meters and relay boards
    codec         CRC, the builders, the parser
    framing       accumulating a response, deciding when it is complete
    plan          what the bus costs, and the blocks the table turns into
    image         a response becomes readings; demands become a frame
    client        RtuClient: one transaction in flight, driven from the beat
  tcp/            the plant-network SLAVE: this unit answering a SCADA master
    pdu           what MBAP adds - a frame out of a stream, an answer back in
    map           what an address MEANS: the register map over Vars::Bank
    slave         TcpSlave: the listener, the connections, the two beat phases
```

The simulator's opposite ends land beside them when they are written:
`tcp/master.{h,cpp}` and `rtu/slave.{h,cpp}`. That is what the directories are
for - `Slave` under `rtu/` and `Slave` under `tcp/` are different objects, and
the path is what says which one a file means.

**Nothing calls a Modbus library, and nothing will.** Every published one for
this platform is synchronous - Arduino's wraps libmodbus, whose
`modbus_read_registers()` sends, waits and receives inside a single call. One
transaction is 21 ms at 19200, a full-length read 163 ms, and a dead slave the
whole response timeout: 2, 16 and 50 beats of a 10 ms loop in which nothing else
runs. The `step()` state machines here exist for exactly that reason.

## 1. Map the TCP slave to parameters and status values

The map is written (`tcp/map.h`) and the addresses derive from each row's
declared `reg`. What is missing is the wiring: `BotanikFirmware` owning a
`TcpSlave`, registering its two tasks (`service_rx` in `PHASE_INPUT`,
`service_tx` in `PHASE_PRESENT`), handing it the `AlarmController` for the ack
block, and publishing its `SlaveContext` on `SystemContext` for the shell and
the status UI. See `modbus-tcp-server.md`.

## 2. Map the RTU master to the Modbus I/O variables

The planner reads the table and the image writes the bank; what joins them to a
running master is the same wiring job as (1), plus the phase split the field bus
wants: refresh in `PHASE_INPUT`, drive in `PHASE_OUTPUT`. See `modbus-budget.md`.

## 3. Dead-slave policy for the RTU master

**Not started, and it is the one that decides whether the bus works.** The budget
note is explicit about why: one unplugged sensor spends a full response timeout
on every sweep, and three of them at a 300 ms timeout with two retries burn 2.7 s
per sweep against a 555 ms budget for the whole plant. Backoff is not a polish
pass; it is what keeps a single dead device from taking the bus down.

What it needs:

- a response deadline sized to the link (~300 ms at 19200, ~100 ms at 115200),
  not the 1000 ms libraries default to;
- one immediate retry, then exponential backoff for that slave alone, out to a
  ~30 s probe - the rest of the plan keeps running at its own rates;
- a comms alarm per slave, raised on the transition into backoff and cleared on
  the probe that answers. Alarms are `SCOPE_A` rows, so these are table rows like
  any other, in the site's 800-899 block;
- the counters kept apart, because they point at different faults: CRC errors are
  a cable or a missing terminator, a wrong unit id is a second master or a
  mis-set device, a timeout is an unplugged one, and an exception is a working
  device saying no. `RtuClient` already distinguishes them in `Error`; the policy
  is what has to act on the difference.

## 4. A Modbus TCP master in the simulator

`tcp/master.{h,cpp}`, ours and non-blocking like the rest, driving the slave over
the sim's loopback listener: read a status value, write a parameter, read it
back. The HAL work is already done - the sim shifts privileged ports up by 10000
and does not divert a connect aimed at a port this process serves, so a master in
the same process reaches the slave for real.

## 5. Modbus RTU slaves in the simulator

`rtu/slave.{h,cpp}`, several instances, one per simulated field device. This is
the one that changes how the simulator works: today `Site::simulate()` writes the
bank directly, and once the slaves exist **the simulated values arrive through
Modbus RTU instead** - the same path the hardware uses, so the master's framing,
its planner, its backoff and its dead-slave handling are all exercised by
unplugging a simulated device rather than by a test that mocks the answer.

That also settles what the simulated serial port has to be: two `RtuFramer`s
facing each other need a byte pipe, not a UART, and the sim's third port is where
that lands.
